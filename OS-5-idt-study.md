# OS-5 — Stage 2 IDT + breakpoint study (`os/`, hello-init track)

This page walks through **milestone 2** after Stage 1 paging: install an **IDT** (Interrupt Descriptor Table), trigger a **known CPU exception** (`int3` → **#BP**, vector 3), and prove the **exception path** runs by printing over **COM1 serial** from a small C++ **dispatcher**.

We keep the same overall boot story:

`UEFI -> ExitBootServices -> kernel_entry -> HELLO INIT -> Stage 1 paging markers -> Stage 2 IDT markers`

## What we had before (after Stage 1)

In `os/`, after Stage 1:

1. The kernel prints `HELLO INIT`.
2. It builds minimal 4-level page tables, switches `CR3`, touches memory via an upper-half alias, and prints:
   - `STAGE 1: build paging`
   - `STAGE 1: touch memory`
   - `STAGE 1: paging done`

At that moment, the CPU can still **deliver exceptions/interrupts**, but we have not installed *our own* handler table yet. Firmware may have left some IDT state behind, but a real OS kernel will replace that with a predictable IDT owned by the kernel.

## Stage 2 goal

Milestone 2 is:

1. **Install an IDT** (`lidt`) pointing at a table of gate descriptors in kernel memory.
2. **Trigger a known exception**: execute `int3`, which raises **#BP** (breakpoint), vector **3**.
3. **Confirm the dispatcher logs it over serial**:
   - assembly stub saves registers and calls `breakpoint_dispatch()` (C linkage),
   - dispatcher prints `STAGE 2: #BP dispatcher` (character-by-character, like Stage 1),
   - execution resumes after `int3` and prints `STAGE 2: done`.

### What “IDT” means (and what it is not)

- The **IDT** is a CPU table in memory. It tells the CPU **where to jump** when a given **vector number** happens (exceptions like divide-by-zero, page fault, breakpoint; external interrupts like timer IRQ; etc.).
- The IDT is **not** a single register like `CR3`. You load its location with the `lidt` instruction, which points the CPU at:
  - a **base address** (where the table lives in virtual memory), and
  - a **limit** (size in bytes minus one).

Each entry is an **IDT gate descriptor** (in long mode, commonly **16 bytes** per entry). For this milestone we only need to understand enough to install **one** working gate for vector 3.

### Vector 3, `int3`, and “breakpoint exception”

- The `int3` instruction (one-byte opcode `0xCC`) is commonly used as a **breakpoint**.
- On x86_64 it raises **#BP**, which uses **vector 3**.
- For #BP with no privilege change, the CPU pushes an **exception frame** onto the stack (same privilege, no error code): **`RIP`, `CS`, `RFLAGS`** (in that order on the stack; see Intel SDM exception delivery details).

This matters because your interrupt return instruction is `iretq`, which expects that frame to still be present under your saved registers when the stub finishes.

### Trap gate vs interrupt gate (why we pick a trap gate here)

IDT gates have a **type** field. Two common 64-bit gate types:

- **Interrupt gate** (`0x8E` in our formatting): typically clears **`IF`** (interrupt flag) on entry.
- **Trap gate** (`0x8F`): typically does **not** clear **`IF`** on entry.

For a simple **#BP** handler that only prints on serial, either can work. This tutorial uses a **64-bit trap gate** (`0x8F`) for vector 3 because #BP is classically a “trap” (the saved `RIP` points **after** the `int3` instruction).

> If you want the authoritative gate layout bitfields, use the Intel SDM “IDT Descriptors” section; the psABI document is about calling conventions, not IDT encoding.

## Where this is implemented

Files:

- `[os/kernel/idt_entry.S](/run/media/dimmus/dev1/os/0_brutal_skift/os_book/os/kernel/idt_entry.S)` — low-level `isr_breakpoint` stub (`iretq` return path).
- `[os/kernel/init.cpp](/run/media/dimmus/dev1/os/0_brutal_skift/os_book/os/kernel/init.cpp)` — `IdtEntry`, `g_idt[]`, `lidt`, Stage 2 serial markers, `breakpoint_dispatch()`.
- `[os/kernel/Makefile](/run/media/dimmus/dev1/os/0_brutal_skift/os_book/os/kernel/Makefile)` — compiles and links `idt_entry.o` into `kernel.elf`.

## Step 0: add an assembly object file to the kernel link

We need raw assembly because the exception entry path must end with **`iretq`**, and you must preserve the exception frame that the CPU pushed.

### Code (from `os/kernel/Makefile`)

```make
KERNEL_OBJ := $(BUILD_DIR)/kernel_init.o $(BUILD_DIR)/idt_entry.o
KERNEL_ASM := idt_entry.S
...
$(CLANG) ... -c "$(KERNEL_ASM)" -o "$(BUILD_DIR)/idt_entry.o"
$(LLD) -T "$(KERNEL_LD)" -o "$(KERNEL_ELF)" $(KERNEL_OBJ)
```

Line-by-line explanation:

1. `KERNEL_OBJ := ... idt_entry.o`: the kernel ELF is linked from **both** the C++ object and the assembly object.
2. `-c idt_entry.S`: compiles the assembly file to an object file.
3. `ld.lld ... $(KERNEL_OBJ)`: links everything into one `kernel.elf`, then `objcopy` produces `kernel.raw` for UEFI loading.

Deeper explanation:

Freestanding kernels often mix **C++** (for clarity) with a tiny amount of **assembly** for ABI-correct trap entry/exit. The linker combines them into one blob loaded at `kernelPhys`.

## Step 1: the ISR stub saves GPRs, aligns stack, calls the dispatcher

### Code (from `os/kernel/idt_entry.S`)

```text
.globl isr_breakpoint
isr_breakpoint:
    pushq %rax
    ...
    pushq %r15
    sub $8, %rsp
    call breakpoint_dispatch
    add $8, %rsp
    popq %r15
    ...
    popq %rax
    iretq
```

Line-by-line explanation:

1. `.globl isr_breakpoint`: exports a symbol the C++ side can take the address of (`&isr_breakpoint`) when filling the IDT gate.
2. `pushq %rax` … `pushq %r15`: saves general-purpose registers so the C++ dispatcher can run without clobbering caller state.
3. `sub $8, %rsp`: fixes **16-byte stack alignment** before the `call` (SysV AMD64 requires `RSP % 16 == 0` immediately before `call`).
4. `call breakpoint_dispatch`: runs the C++ logging function.
5. `add $8, %rsp`: undoes the alignment adjustment.
6. `popq` sequence: restores GPRs.
7. `iretq`: returns from the exception using the **exception frame** the CPU pushed for #BP.

Deeper explanation:

When the CPU delivers vector 3, it pushes an exception frame. Your stub pushes extra registers **on top of** that frame. `iretq` expects the top of the stack to look like the CPU’s original exception layout *after* you remove your saved registers—so you must restore GPRs in the reverse order, then `iretq`.

### psABI pointer (stack alignment inside the stub)

The SysV AMD64 rule that the stack must be 16-byte aligned before a `call` is documented in the x86-64 psABI “Function Calling Sequence” / stack frame sections. You can use the same GitLab links already cited in Stage 1 docs, e.g.:

- `low-level-sys-info.tex` — “The Stack Frame” / stack alignment before `call`

## Step 2: define the IDT table + gate bytes in C++

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
struct IdtEntry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} __attribute__((packed));

static IdtEntry g_idt[256] __attribute__((aligned(16)));

static void idtClearAll() {
    for (unsigned i = 0; i < 256; ++i) {
        g_idt[i].offset_lo = 0;
        g_idt[i].offset_mid = 0;
        g_idt[i].offset_hi = 0;
        g_idt[i].selector = 0;
        g_idt[i].ist = 0;
        g_idt[i].type_attr = 0;
        g_idt[i].reserved = 0;
    }
}

static void idtSetGate(uint8_t vector, uint64_t handlerVirt, uint16_t cs, uint8_t typeAttr) {
    IdtEntry* e = &g_idt[vector];
    e->offset_lo = static_cast<uint16_t>(handlerVirt & 0xffffu);
    e->offset_mid = static_cast<uint16_t>((handlerVirt >> 16) & 0xffffu);
    e->offset_hi = static_cast<uint32_t>(handlerVirt >> 32);
    e->selector = cs;
    e->ist = 0;
    e->type_attr = typeAttr;
    e->reserved = 0;
}
```

Line-by-line explanation:

1. `struct IdtEntry { ... } __attribute__((packed))`: describes the **in-memory byte layout** of one IDT gate without compiler-inserted padding.
2. `g_idt[256]`: reserves **256 entries** (full IDT size) so the `lidt` limit can be `sizeof(g_idt) - 1` safely.
3. `__attribute__((aligned(16)))`: places the table on a **16-byte boundary** (reasonable for fixed descriptor tables; also avoids `alignas` portability issues with some `clang++` default standard settings).
4. `idtClearAll`: explicitly clears every entry so “present” gates aren’t left as garbage (important because BSS/zero-init behavior for raw kernel blobs can be subtle).
5. `idtSetGate`: splits a 64-bit handler virtual address into the low/mid/high pieces the IDT encoding expects.

Deeper explanation:

Even though we only *use* vector 3 in this milestone, keeping a **256-entry** IDT is a common “bring-up” choice: your `limit` covers the whole standard vector range, and you don’t have to reason about partial tables while debugging.

<details style="border:1px solid #ccc;border-radius:6px;padding:0.5em 0.75em;background:#f7f7fb;margin:0.75em 0;">
<summary style="font-weight:bold;cursor:pointer;">C++ note: <code>__attribute__((packed))</code> and <code>reinterpret_cast</code> (skip if known)</summary>

- `packed` asks the compiler not to insert padding between fields; IDT layout is bit-exact.
- `handlerVirt` is built using `reinterpret_cast<uint64_t>(&isr_breakpoint)` elsewhere: it treats the function’s address as an integer so we can split it into IDT fields.

</details>

## Step 3: load the IDT with `lidt`

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
struct IdtPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static void idtLoad(uint64_t baseVirt, uint16_t limit) {
    IdtPointer p{};
    p.limit = limit;
    p.base = baseVirt;
    asm volatile("lidt %0" : : "m"(p) : "memory");
}
```

Line-by-line explanation:

1. `IdtPointer`: the 10-byte operand format expected by `lidt` in 64-bit mode (16-bit limit + 64-bit base).
2. `lidt %0`: loads `IDTR` from memory (`p`), activating the IDT for exception delivery.

Deeper explanation:

After `lidt`, if an exception occurs and the corresponding gate is present and valid, the CPU can vector to your handler address.

Because this kernel uses **identity mappings** for kernel memory in Stage 1, the virtual address `&g_idt[0]` refers to the same underlying physical memory as in the loader’s identity map.

## Step 4: fill vector 3 with the handler’s address + current `CS`

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
uint16_t csSel = 0;
asm volatile("mov %%cs, %0" : "=r"(csSel));

idtClearAll();
constexpr uint8_t kVectorBreakpoint = 3;
constexpr uint8_t kIdtTypeTrap64 = 0x8F;
uint64_t bpHandler = reinterpret_cast<uint64_t>(&isr_breakpoint);
idtSetGate(kVectorBreakpoint, bpHandler, csSel, kIdtTypeTrap64);
idtLoad(reinterpret_cast<uint64_t>(&g_idt[0]), static_cast<uint16_t>(sizeof(g_idt) - 1));
```

Line-by-line explanation:

1. `mov %%cs, %0`: reads the **current code segment selector** into `csSel`.
2. `kVectorBreakpoint = 3`: #BP vector number for `int3`.
3. `kIdtTypeTrap64 = 0x8F`: selects a **64-bit trap gate** encoding for the gate’s type/present/DPL bits (tutorial choice).
4. `reinterpret_cast<uint64_t>(&isr_breakpoint)`: converts the handler symbol address into a plain integer for splitting into IDT fields.
5. `idtSetGate(...)`: writes gate descriptor bytes for vector 3.
6. `idtLoad(...)`: installs the table with limit `sizeof(g_idt) - 1`.

Deeper explanation:

The gate’s **code segment selector** must be consistent with how your kernel is executing. Reading `CS` at runtime avoids hard-coding a selector value that might differ across firmware/QEMU setups.

<details style="border:1px solid #ccc;border-radius:6px;padding:0.5em 0.75em;background:#f7f7fb;margin:0.75em 0;">
<summary style="font-weight:bold;cursor:pointer;">C++ note: <code>reinterpret_cast</code> for function addresses (skip if known)</summary>

`reinterpret_cast<uint64_t>(&isr_breakpoint)` is used because the IDT packing code wants an integer type to shift/mask. The underlying meaning is still “virtual address of the handler function”.

</details>

## Step 5: trigger `int3`, then print “done” after return

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
write_stage2_int3();
asm volatile("int3");
write_stage2_done();
```

Line-by-line explanation:

1. `write_stage2_int3()`: prints a marker **before** executing `int3` (proves we reached the trigger site).
2. `int3`: raises #BP vector 3, which vectors through IDT entry 3 into `isr_breakpoint`.
3. `write_stage2_done()`: runs **after** the handler returns (proves we successfully executed `iretq` back to normal flow).

Deeper explanation:

If the IDT entry is wrong, the CPU may **triple-fault** or hang before printing. If the stub mis-uses the stack, return may crash. Seeing `STAGE 2: done` is a strong end-to-end check.

## Step 6: dispatcher prints over serial (the “proof”)

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
extern "C" void breakpoint_dispatch(void) {
    write_stage2_dispatcher_bp();
}
```

Line-by-line explanation:

1. `extern "C"`: ensures the symbol name is **`breakpoint_dispatch`** (no C++ name mangling), matching `call breakpoint_dispatch` in `idt_entry.S`.
2. `write_stage2_dispatcher_bp()`: prints `STAGE 2: #BP dispatcher` using `serial::write_char` (same style as Stage 1: no reliance on `.rodata` strings).

Deeper explanation:

This is the milestone’s “known exception path observable on serial”: you’re not guessing whether interrupts “kind of work”—you printed from the trap handler.

<details style="border:1px solid #ccc;border-radius:6px;padding:0.5em 0.75em;background:#f7f7fb;margin:0.75em 0;">
<summary style="font-weight:bold;cursor:pointer;">C++ note: why <code>extern "C"</code> for <code>breakpoint_dispatch</code> (skip if known)</summary>

Assembly calls a symbol by **name**. `extern "C"` exports a predictable symbol name so the linker can connect `idt_entry.o` → `init.cpp`.

</details>

## Expected serial output (QEMU)

You should see (in order):

- `HELLO INIT`
- `STAGE 1: build paging`
- `STAGE 1: touch memory`
- `STAGE 1: paging done`
- `STAGE 2: install idt`
- `STAGE 2: int3`
- `STAGE 2: #BP dispatcher`
- `STAGE 2: done`

## Troubleshooting

- **Reboot loop / instant reset**: often a **bad IDT gate** or bad `iretq` stack layout (exception frame destroyed).
- **Hang with no `STAGE 2: #BP dispatcher`**: IDT not loaded, vector 3 not routed to `isr_breakpoint`, or crash before serial prints.
- **`STAGE 2: #BP dispatcher` but no `STAGE 2: done`**: return path is wrong (`iretq` / stack).

## Next milestone hint

After you trust exceptions, the next step is usually **IRQs** (PIC/APIC), **masking**, and **EOI**—but this milestone intentionally stays on a CPU **exception** path first because it doesn’t require interrupt controller setup.
