# OS-6 — Stage 3 PIC + PIT + timer IRQ (`src-os/` tutorial tree)

This page walks through **milestone 3** after OS-5: keep the same **boot GDT**, **IDT**, and **`kLinear` / `idtTable` discipline**, then bring up the legacy **8259 PIC** (QEMU’s default for PC compat), program the **PIT channel 0** for periodic ticks, install an **IDT gate for vector 32** (IRQ0 after remap), **`sti`**, and prove the **hardware interrupt path** on **COM1**—including mandatory **EOI** before `iretq`.

We extend the boot story:

`… -> STAGE 2: done -> STAGE 3: begin -> PIC -> PIT -> IDT[32] -> sti -> irq ticks -> STAGE 3: done -> cli; hlt`

## What we had before (after OS-5)

From [OS-5-idt-study.md](OS-5-idt-study.md):

1. Stage 1 paging and serial markers.
2. Boot **GDT** on the stack + **`lretq`** to **`CS=0x08`**, **`DS`/`ES`/`SS=0x10`**.
3. **`idtTable(kernelPhys)`**, **`lidt`**, **`int3` → #BP**, **`isr_breakpoint` → `breakpoint_dispatch`**, **`iretq`** with patched saved **`SS`**.

At that point the CPU can deliver **exceptions** through *your* IDT, but **external IRQs** are still useless until:

- the **PIC** knows which **IDT vectors** to use for each line (remap away from the x86 **exception** range 0–31),
- the **timer** actually **pulses IRQ0** (PIT),
- you **unmask** the line you care about and **`sti`**,
- and your handler **acks the PIC** (**EOI**) so the next edge can arrive.

## Stage 3 goal

Milestone 3 is:

1. **Remap** the master/slave **8259** so IRQ0..IRQ15 map to vectors **0x20–0x2F** (decimal **32–47**), matching the usual “`irq = intNo - 32`” convention (see Skift’s `_intDispatch` and [C-4-Interrupts-exceptions-and-frames.md](C-4-Interrupts-exceptions-and-frames.md)).
2. **Mask** every IRQ line except **IRQ0** (timer on the first PIC): master **IMR** `0xFE`, slave **`0xFF`**.
3. Program **PIT channel 0** in **square-wave** mode with a **16-bit divisor** derived from the classic **1.193182 MHz** base (same idea as Skift’s [`pit.h`](src-os-skift/skift/src/kernel/hal-x86_64/pit.h)).
4. Add an **IDT gate for vector 32** pointing at **`kLinear(&isr_irq32)`**, type **64-bit interrupt gate** (`0x8E`): hardware clears **`IF`** for the duration of the handler (nested IRQs off unless you re-enable inside).
5. **`sti`**, then **`hlt` in a loop** until **three** timer IRQs have fired (counted in **`g_timer_irq_count`**), **`cli`**, print **`STAGE 3: done`**, and halt.
6. In **`timer_irq_dispatch`**, send **master EOI** (`outb(0x20, 0x20)`) **before** returning through **`iretq`** (Skift’s `DualPic::ack` does the same idea after dispatch).

### IRQ vs exception (why vector 32)

- **Exceptions** (#DE, #BP, #PF, …) use vectors **0–31** (with reserved holes). That is why the IBM PC convention **remaps** the PIC to start at **0x20**: you must not overlap PIC IRQs with CPU exception vectors.
- **IRQ0** on the master PIC is the **timer**. After remap, it becomes **INT 0x20 = 32**.

### PIC vs APIC (scope of this milestone)

Real hardware and modern VMs often use the **APIC/IOAPIC** path. QEMU’s **PC** machine still models a **legacy PIC** that drives timer IRQs in the way this chapter uses. A later milestone can switch to **Local APIC** timer programming; the **IDT + EOI + `sti`/`hlt` pattern** stays the same—only the **ack** mechanism changes.

## Where this is implemented

Files (under `src-os/kernel/` unless noted):

- `irq_entry.S` — `isr_irq32`: same GPR save/restore and **saved `SS`** patch as `isr_breakpoint`, calls `timer_irq_dispatch`, `iretq`.
- `init.cpp` — `outb` / `io_wait`, `pic_remap_and_mask`, `pit_set_frequency`, Stage 3 serial markers, `idtSetGate(..., 32, ..., 0x8E)`, second `lidt`, `sti` / `hlt` wait, `timer_irq_dispatch` + **EOI**.
- `Makefile` — links `irq_entry.o` with `kernel_init.o` and `idt_entry.o`.

Skift parallels (same repo tree as [OS-5](OS-5-idt-study.md) “Next milestone” table):

| Our tutorial | Skift |
| --- | --- |
| `pic_remap_and_mask` | [`DualPic::init`](src-os-skift/skift/src/kernel/hal-x86_64/pic.h) + IMR writes |
| `pit_set_frequency` | [`Pit::init`](src-os-skift/skift/src/kernel/hal-x86_64/pit.h) |
| `isr_irq32` + `timer_irq_dispatch` | [`ints.s`](src-os-skift/skift/src/kernel/hjert/x86_64/ints.s) stub → [`_intDispatch`](src-os-skift/skift/src/kernel/hjert/x86_64/arch.cpp) + `_pic.ack` |

## Step 0: link a second ISR object file

### Code (`src-os/kernel/Makefile`)

```make
KERNEL_OBJ := $(BUILD_DIR)/kernel_init.o $(BUILD_DIR)/idt_entry.o $(BUILD_DIR)/irq_entry.o
...
	$(CLANG) ... -c idt_entry.S -o "$(BUILD_DIR)/idt_entry.o"
	$(CLANG) ... -c irq_entry.S -o "$(BUILD_DIR)/irq_entry.o"
```

We keep **one ISR per assembly file** for readability (Skift generates 256 stubs from one `.s` file with macros—that is the production approach).

## Step 1: I/O ports — `outb` and `io_wait`

The PIC and PIT are reached with **`in`/`out` to fixed I/O ports** (not MMIO). After some `outb` sequences the hardware needs a tiny delay; we use the same **jmp chain** Skift uses in `Pic::wait()`.

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline void io_wait() {
    asm volatile("jmp 1f\n1:\tjmp 1f\n1:" ::: "memory");
}
```

**Why `"memory"` clobbers:** ordering—so the compiler does not reorder unrelated memory ops around port writes.

## Step 2: remap and mask the 8259 PIC

### Ports (master / slave)

| Role | Command | Data |
| --- | --- | --- |
| Master PIC | `0x20` | `0x21` |
| Slave PIC | `0xA0` | `0xA1` |

### Initialization sequence (ICW1–ICW4)

We follow the standard **cascade** setup (slave attached to master **IRQ2**), **8086/8088 mode**, then program the **Interrupt Mask Register** (data port):

- Master mask **`0xFE`**: all lines masked **except IRQ0** (timer).
- Slave mask **`0xFF`**: fully masked (we do not handle slave IRQs in this milestone).

This matches the **intent** of Skift’s `DualPic::init()` (same ICW ordering); we then **restrict** IRQs with explicit masks instead of leaving lines fully unmasked.

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
static void pic_remap_and_mask() {
    constexpr uint16_t PIC1_CMD = 0x20;
    constexpr uint16_t PIC1_DATA = 0x21;
    constexpr uint16_t PIC2_CMD = 0xA0;
    constexpr uint16_t PIC2_DATA = 0xA1;

    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();
    outb(PIC1_DATA, 1);
    io_wait();
    outb(PIC2_DATA, 1);
    io_wait();
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);
}
```

**Teaching point:** if you forget **`0xFE`** and leave the master fully unmasked, **spurious IRQ7** and other lines can fire before you are ready—debugging becomes noisy.

## Step 3: program the PIT (channel 0)

**Ports:** command `0x43`, channel 0 data `0x40`.

**Command byte `0x36`:** channel 0, access low then high byte, mode 3 (square wave), binary.

**Divisor:** `1193182 / hz` clamped to **`[2, 65535]`** (16-bit). The tutorial uses **`100` Hz** so the wait loop finishes quickly in QEMU.

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
static void pit_set_frequency(unsigned hz) {
    constexpr uint16_t PIT_CMD = 0x43;
    constexpr uint16_t PIT_CH0 = 0x40;
    constexpr uint32_t PIT_BASE_HZ = 1193182;

    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }
    if (divisor < 2) {
        divisor = 2;
    }
    outb(PIT_CMD, 0x36);
    io_wait();
    outb(PIT_CH0, static_cast<uint8_t>(divisor & 0xFF));
    io_wait();
    outb(PIT_CH0, static_cast<uint8_t>(divisor >> 8));
    io_wait();
}
```

## Step 4: IDT gate for vector 32 — interrupt vs trap

For IRQ handlers, a **64-bit interrupt gate** (`0x8E`) is conventional: the CPU clears **`IF`** on entry so the handler is not immediately re-entered by another IRQ. A **trap gate** (`0x8F`, as used for #BP in OS-5) does **not** auto-clear **`IF`**.

We **reload `lidt`** after patching entry 32 (same table base as Stage 2—only the entry changes).

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
constexpr uint8_t kVectorIRQ0 = 32;
constexpr uint8_t kIdtTypeInt64 = 0x8E;
uint64_t irqHandler = kLinear(reinterpret_cast<const void*>(&isr_irq32));
idtSetGate(idt, kVectorIRQ0, irqHandler, csSel, kIdtTypeInt64);
idtLoad(reinterpret_cast<uint64_t>(idt), static_cast<uint16_t>(sizeof(g_idt) - 1));
```

## Step 5: `sti`, wait with `hlt`, `cli`

**`hlt`** stops the core until an interrupt wakes it—**provided `IF=1`**. After **`sti`**, the main loop:

```cpp
g_timer_irq_count = 0;
asm volatile("sti" ::: "memory");
while (g_timer_irq_count < 3) {
    asm volatile("hlt" ::: "memory");
}
asm volatile("cli" ::: "memory");
```

**Volatile counter:** `g_timer_irq_count` is **`volatile`** so the compiler does not optimize the loop into an infinite wait.

## Step 6: assembly stub + C dispatcher + EOI

### `irq_entry.S` (same discipline as OS-5)

Hardware IRQ0 pushes the **same 5-quadword frame** as #BP (no error code). We reuse the **`movw $0x10, 32(%rsp)`** fix before **`iretq`**.

```text
.globl isr_irq32
isr_irq32:
    pushq %rax
    ...
    subq $8, %rsp
    call timer_irq_dispatch
    ...
    movw $0x10, 32(%rsp)
    iretq
```

### `timer_irq_dispatch` — EOI is not optional

```cpp
extern "C" void timer_irq_dispatch(void) {
    constexpr uint16_t PIC1_CMD = 0x20;
    outb(PIC1_CMD, 0x20);   // non-specific EOI to master
    uint64_t n = ++g_timer_irq_count;
    if (n <= 3) {
        write_stage3_irq_tick(static_cast<char>('0' + static_cast<int>(n)));
    }
}
```

If you skip **EOI**, the PIC thinks IRQ0 is still in service—the line stays **in-service** and you stop seeing ticks (or you see only one). Slave IRQs would need **`0xA0` EOI** first, then master—Skift’s `DualPic::ack` encodes that for **`intno >= 40`**.

## Order of operations in `kernel_entry` (Stage 3 only)

This list is the **source order** after **`write_stage2_done()`** in `init.cpp`:

1. **`write_stage3_begin()`**
2. **`pic_remap_and_mask()`** → **`write_stage3_pic_ok()`**
3. **`pit_set_frequency(100)`** → **`write_stage3_pit_ok()`**
4. **`idtSetGate`** for vector **32**, type **`0x8E`**
5. **`idtLoad`** (second load; refreshes IDTR)
6. **`write_stage3_sti()`**
7. **`g_timer_irq_count = 0`**, **`sti`**, **`while (count < 3) hlt`**, **`cli`**
8. **`write_stage3_done()`**
9. **`for (;;) { cli; hlt; }`**

## Expected serial output (QEMU)

After the OS-5 lines, you should see (in order):

- `STAGE 3: begin`
- `STAGE 3: pic ok`
- `STAGE 3: pit ok`
- `STAGE 3: sti`
- `STAGE 3: irq 1`
- `STAGE 3: irq 2`
- `STAGE 3: irq 3`
- `STAGE 3: done`

## Troubleshooting

- **`STAGE 3: sti` then hang (no `irq`)**: wrong **IDT** gate (handler address must be **`kLinear(&isr_irq32)`**), **PIC not remapped**, **timer masked**, **PIT not programmed**, or **EOI missing** so the line never rearms (less common on first IRQ—more obvious after one tick).
- **Triple fault / reset after `sti`**: bad **`iretq` frame** (same **`SS`** issue as OS-5 if you drop the **`movw $0x10, 32(%rsp)`** patch in `irq_entry.S`), or garbage IDT entry for vector 32.
- **Flood of IRQ prints**: master mask wrong (not **`0xFE`**), or **IF** left on with a very high PIT rate.
- **Stale output / old Stage 2 only**: rebuild **kernel and UEFI** from `src-os/` (`make clean && make build`) so `kernel_blob.inc` matches `kernel.raw`.

## Next milestone hint

When you outgrow the legacy PIC, move to **Local APIC + I/O APIC** (and **MSI** on PCI later). The tutorial pieces that carry over are: **IDT gates**, **clear EOI policy**, and **disciplined `kLinear`/`idtTable` addressing** for anything the CPU dereferences.
