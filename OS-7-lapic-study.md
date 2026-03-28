# OS-7 — Stage 4 Local APIC timer (`src-os/` tutorial tree)

This page walks through **milestone 4** after [OS-6](OS-6-irq-pic-study.md): **stop using the PIC/PIT path** for ticks, **map the Local APIC MMIO page**, enable the APIC in the **Spurious Interrupt Vector Register (SVR)**, arm the **APIC timer** in **periodic** mode on a **new IDT vector (34)**, acknowledge interrupts with **LAPIC EOI** (MMIO write), and prove the path on **COM1**.

We extend the boot story:

`… -> STAGE 3: done -> STAGE 4: begin -> PIC fully masked -> LAPIC MMIO -> SVR -> timer armed -> sti -> three LAPIC ticks -> mask LVT -> STAGE 4: done -> cli; hlt`

## What we had before (after OS-6)

1. **PIC** remapped to vectors **32–47**, **PIT** on **IRQ0**, **IDT[32]** → **`isr_irq32`**, **PIC EOI** (`outb(0x20, 0x20)`).
2. Stage 1 paging maps **kernel**, **stack**, **mmap** scratch, and **upper-half aliases** — but **not** device MMIO unless we add it.

The **Local APIC** is controlled through **memory-mapped registers** at the **default physical base `0xFEE00000`** (first 4KiB of the APIC window). Firmware may not list that range as “conventional RAM” in the UEFI memory map, so **your** page tables must map it explicitly if you run with **`CR3`** pointing at your tables.

## Stage 4 goal

Milestone 4 is:

1. **Map** physical page **`0xFEE00000`** (identity and upper-half alias, same pattern as Stage 1).
2. **Mask the PIC completely** (`0xFF` on both master and slave data ports) so **IRQ0** no longer delivers **vector 32** — the PIT may still run, but it does not reach the CPU through the PIC.
3. **Enable the local APIC** by setting **SVR** (`offset 0xF0`): **bit 8** = APIC software enable, **bits 7–0** = spurious vector (this tutorial uses **`0xFF`** combined as **`0x1FF`**).
4. Program the **APIC timer**:
   - **Divide Configuration Register** (`0x3E0`): **`3`** → divide by **16** (Intel encoding; see SDM “Divide Configuration Register”).
   - **LVT Timer** (`0x320`): **vector 34**, **periodic** (**bit 17**), **unmasked** (clear **bit 16**).
   - **Initial Count** (`0x380`): a **large** value so ticks are slow enough for serial (here **`0x80000`**; tune for your VM).
5. Install **IDT[34]** as a **64-bit interrupt gate** (`0x8E`) to **`kLinear(&isr_lapic_timer)`** (new assembly stub in `lapic_entry.S`).
6. **`sti`**, **`hlt`** until **three** interrupts, **`cli`**, then **mask** the LVT timer (**bit 16** in `0x320`) so the halt loop does not keep printing.
7. In **`lapic_timer_dispatch`**, send **LAPIC EOI**: write **`0`** to **`0xB0`** (not the PIC command port).

### PIC EOI vs LAPIC EOI

| Source | Acknowledge |
| --- | --- |
| 8259 PIC (OS-6) | `outb(0x20, 0x20)` (master; slave first if needed) |
| Local APIC | MMIO write **`0`** to **EOI** at offset **`0xB0`** |

Mixing them is a common bug: after moving the timer to the LAPIC, **PIC EOI** is **wrong** for that interrupt.

### Why vector **34** (not **32**)

**32** was the **PIC IRQ0** vector after remap. Keeping **LAPIC** on **34** makes logs and debugging clearer: **32** = legacy path, **34** = **APIC timer** path. (You could reuse **32** once the PIC is fully masked; this chapter keeps them distinct.)

## Where this is implemented

Files (under `src-os/kernel/` unless noted):

- `init.cpp` — **`kLapicMmioPhys`**, **`mapPageDual`** for the LAPIC page in **Stage 1**, **`pic_mask_all`**, **`lapic_mmio_read` / `lapic_mmio_write`**, Stage 4 serial markers, **`lapic_timer_dispatch`**, LVT mask after three ticks.
- `lapic_entry.S` — **`isr_lapic_timer`**: same GPR discipline and **saved `SS`** patch as OS-5/OS-6, calls **`lapic_timer_dispatch`**, **`iretq`**.
- `Makefile` — links **`lapic_entry.o`**.

Skift in this repo wires timers through its HAL init ([`arch.cpp`](src-os-skift/skift/src/kernel/hjert/x86_64/arch.cpp) uses **`DualPic`** + **`Pit`**); a production tree often adds **x2APIC** or **APIC MMIO** drivers elsewhere. This tutorial stays with **MMIO at `0xFEE00000`**, which matches **QEMU’s default PC** model well enough for teaching.

## Step 0: map the LAPIC page in Stage 1

Without this mapping, any **`mov`**/`store` to **`0xFEE00000`** under your **`CR3`** will **#PF**.

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
static constexpr uint64_t kLapicMmioPhys = 0xFEE00000ull;
// … after other identity mappings:
mapPageDual(kLapicMmioPhys, kLapicMmioPhys, PTE_WRITABLE);
// … inside mapUpperHalfAliases:
mapPageDual(kLapicMmioPhys + UPPER_HALF, kLapicMmioPhys, PTE_WRITABLE);
```

**Note:** For real hardware you should also consult **IA32_APIC_BASE** (`MSR 0x1B`) for the APIC base address and **enable** semantics; QEMU’s **default** matches **`0xFEE00000`**.

## Step 1: mask the PIC

### Code (excerpt)

```cpp
static void pic_mask_all() {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}
```

Call this **before** arming the LAPIC timer so you do not receive **spurious** legacy IRQs on **vector 32** while testing **vector 34**.

## Step 2: MMIO accessors

APIC registers are **32-bit** locations at **fixed offsets** from the base. This tutorial uses simple **`volatile uint32_t*`** loads/stores.

```cpp
static inline void lapic_mmio_write(uintptr_t reg, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(kLapicMmioPhys + reg) = value;
}
```

## Step 3: SVR — enable the local APIC

```cpp
lapic_mmio_write(0xF0, 0x1FF);
```

Until **bit 8** is set, many LAPIC features (including sensible timer behavior) are not fully active.

## Step 4: timer divide, LVT, initial count

```cpp
lapic_mmio_write(0x3E0, 3);                     // divide by 16
lapic_mmio_write(0x320, 34u | (1u << 17));      // vector 34, periodic
lapic_mmio_write(0x380, 0x80000);               // initial count
```

**Periodic** (**bit 17**): when the current count reaches zero, it reloads from **Initial Count** and fires again.

## Step 5: IDT gate and assembly stub

Same rules as OS-5/OS-6: handler address **`kLinear(&isr_lapic_timer)`**, **`movw $0x10, 32(%rsp)`** before **`iretq`**.

## Step 6: EOI in C

```cpp
extern "C" void lapic_timer_dispatch(void) {
    lapic_mmio_write(0xB0, 0);
    // … bump counter, print …
}
```

## Step 7: mask the LVT when finished

```cpp
lapic_mmio_write(0x320, 1u << 16); // mask bit only
```

This stops periodic timer interrupts before the final **`cli; hlt`** loop (optional but keeps the serial log quiet).

## Order of operations in `kernel_entry` (Stage 4 only)

After **`write_stage3_done()`**:

1. **`write_stage4_begin()`**
2. **`pic_mask_all()`** → **`write_stage4_pic_masked()`**
3. **`lapic_mmio_read(0x30)`** (version; result discarded) → **`write_stage4_lapic_mmio()`**
4. **`lapic_mmio_write(SVR, 0x1FF)`** → **`write_stage4_svr()`**
5. Divide, LVT, Initial Count → **`write_stage4_timer_arm()`**
6. **`idtSetGate(..., 34, …, 0x8E)`**, **`idtLoad`**
7. **`write_stage4_sti()`**, **`g_lapic_irq_count = 0`**, **`sti`**, **`while (count < 3) hlt`**, **`cli`**
8. **Mask LVT**
9. **`write_stage4_done()`**
10. Final **`for (;;) { cli; hlt; }`**

## Expected serial output (QEMU)

After the OS-6 lines:

- `STAGE 4: begin`
- `STAGE 4: pic mask`
- `STAGE 4: lapic mmio`
- `STAGE 4: svr`
- `STAGE 4: timer`
- `STAGE 4: sti`
- `STAGE 4: irq 1`
- `STAGE 4: irq 2`
- `STAGE 4: irq 3`
- `STAGE 4: done`

## Troubleshooting

- **Hang after `STAGE 4: sti`**: LAPIC page **not mapped** (check **`kLapicMmioPhys`** mapping and **`CR3`**), **SVR** not enabled, **LVT** still masked, or **Initial Count** too large for your patience (lower **`0x80000`**).
- **Triple fault**: bad **IDT[34]** or missing **`SS`** patch in **`lapic_entry.S`** (same class of bug as OS-5).
- **Vectors on 32 instead of 34**: **PIC not fully masked** or spurious legacy path.
- **Stale image**: `make clean && make build` from **`src-os/`** so **`kernel_blob.inc`** matches **`kernel.raw`**.

## Next milestone hint

The `src-os/` tutorial continues with **[OS-8-page-fault-study.md](OS-8-page-fault-study.md)** (#PF, **`CR2`**, error-code stack, **`iretq`**). After that, **I/O APIC**, **MSI/MSI-X**, and **x2APIC** remain the natural hardware topics; the constant pieces are **IDT discipline**, **correct EOI**, **page tables for every access**, and **honest stack layouts per vector**.
