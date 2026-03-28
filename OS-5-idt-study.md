# OS-5 — Stage 2 IDT + breakpoint study (`src-os/` tutorial tree)

This page walks through **milestone 2** after Stage 1 paging: install a **minimal boot GDT** (so exception delivery and `iretq` agree with your tables), then an **IDT** (`lidt`), trigger **`int3` → #BP** (vector 3), and prove the **exception path** over **COM1** from a small C++ **dispatcher**.

We keep the same overall boot story:

`UEFI -> ExitBootServices -> kernel_entry -> HELLO INIT -> Stage 1 paging markers -> boot GDT + lretq -> Stage 2 serial markers -> lidt -> int3 -> #BP -> done -> Stage 3 PIC/PIT/IRQ0 -> done` (Stage 3: [OS-6-irq-pic-study.md](OS-6-irq-pic-study.md))

## What we had before (after Stage 1)

In `src-os/`, after Stage 1:

1. The kernel prints `HELLO INIT`.
2. It builds minimal 4-level page tables, switches `CR3`, touches memory via an upper-half alias, and prints:
   - `STAGE 1: build paging`
   - `STAGE 1: touch memory`
   - `STAGE 1: paging done`

At that moment, the CPU can still **deliver exceptions/interrupts**, but we have not installed *our own* handler table yet. Firmware may have left some IDT state behind, but a real OS kernel will replace that with a predictable IDT owned by the kernel.

## Stage 2 goal

Milestone 2 is:

1. **Install a minimal boot GDT** in mapped memory (`lgdt`), reload **`CS`** with **`lretq`**, and set **`DS`/`ES`/`SS`** to your **data** selector (`0x10`).
2. **Install an IDT** (`lidt`) whose base and the **handler** address use **`kernelPhys + link offset`** (`idtTable` / `kLinear`), not bare link-time addresses.
3. **Trigger a known exception**: execute `int3`, which raises **#BP**, vector **3**.
4. **Confirm over serial**:
   - markers include `STAGE 2: begin`, `STAGE 2: install idt`, `STAGE 2: int3`,
   - the stub calls `breakpoint_dispatch()` → `STAGE 2: #BP dispatcher`,
   - after **`iretq`**, `STAGE 2: done` (requires a valid saved **`SS`** on the frame if you shrank the GDT—see Step 1).

### What “IDT” means (and what it is not)

- The **IDT** is a CPU table in memory. It tells the CPU **where to jump** when a given **vector number** happens (exceptions like divide-by-zero, page fault, breakpoint; external interrupts like timer IRQ; etc.).
- The IDT is **not** a single register like `CR3`. You load its location with the `lidt` instruction, which points the CPU at:
  - a **base address** (where the table lives in virtual memory), and
  - a **limit** (size in bytes minus one).

Each entry is an **IDT gate descriptor** (in long mode, commonly **16 bytes** per entry). For this milestone we only need to understand enough to install **one** working gate for vector 3.

### Vector 3, `int3`, and “breakpoint exception”

- The `int3` instruction (one-byte opcode `0xCC`) is commonly used as a **breakpoint**.
- On x86_64 it raises **#BP**, which uses **vector 3**.
- In **64-bit mode**, with **no privilege change** and **no error code**, the CPU still pushes a **fixed five-quadword** frame onto the stack (low address → high): **`RIP`, `CS`, `RFLAGS`, the pre-exception `RSP`, and `SS`**. See Intel SDM / [OSDev ISR stack layout](https://wiki.osdev.org/Interrupt_Service_Routines) for the exact ordering.

`iretq` pops those five values (when returning at the same privilege level it restores `RIP`, `CS`, `RFLAGS`, `RSP`, and `SS`). Your stub must leave that frame intact under the saved GPRs, and any **boot GDT** change must keep the **saved `SS`** valid for the table limit (see Step 1 and troubleshooting).

### Trap gate vs interrupt gate (why we pick a trap gate here)

IDT gates have a **type** field. Two common 64-bit gate types:

- **Interrupt gate** (`0x8E` in our formatting): typically clears **`IF`** (interrupt flag) on entry.
- **Trap gate** (`0x8F`): typically does **not** clear **`IF`** on entry.

For a simple **#BP** handler that only prints on serial, either can work. This tutorial uses a **64-bit trap gate** (`0x8F`) for vector 3. Intel documents the saved `RIP` for `#BP` as the address of the `int3` byte; this milestone does not adjust it in assembly (return lands on the instruction after `int3` in the build under QEMU—if you ever re-execute `int3` in a loop, bump the saved `RIP` on the stack before `iretq`).

> If you want the authoritative gate layout bitfields, use the Intel SDM “IDT Descriptors” section; the psABI document is about calling conventions, not IDT encoding.

## Where this is implemented

Files (under `src-os/kernel/` unless noted):

- `idt_entry.S` — `isr_breakpoint`: saves GPRs, calls `breakpoint_dispatch`, fixes saved **`SS`** on the exception frame, `iretq`.
- `init.cpp` — boot **GDT** + `lretq` to `CS=0x08`, `IdtEntry`, `g_idt[]`, `idtTable(kernelPhys)`, `kLinear`, `lidt`, Stage 2 serial markers, `breakpoint_dispatch()`.
- `ld.ld` — single `PT_LOAD` (`PHDRS`) so the linked image is one blob for `objcopy`.
- `pad_kernel_raw.py` + `Makefile` — pad `kernel.raw` to the ELF segment **`MemSiz`** so `.bss` (including `g_idt`) is present in the UEFI-loaded blob (`llvm-objcopy` alone often truncates at `FileSiz`).
- `Makefile` — builds `kernel_init.o` + `idt_entry.o` → `kernel.elf` → `kernel.raw`.

## Step 0: add an assembly object file to the kernel link

We need raw assembly because the exception entry path must end with **`iretq`**, and you must preserve the exception frame that the CPU pushed.

### Code (from `src-os/kernel/Makefile`)

```make
KERNEL_OBJ := $(BUILD_DIR)/kernel_init.o $(BUILD_DIR)/idt_entry.o
KERNEL_ASM := idt_entry.S
...
$(CLANG) ... -c "$(KERNEL_ASM)" -o "$(BUILD_DIR)/idt_entry.o"
$(LD) -T "$(KERNEL_LD)" -o "$(KERNEL_ELF)" $(KERNEL_OBJ)
```

Line-by-line explanation:

1. `KERNEL_OBJ := ... idt_entry.o`: the kernel ELF is linked from **both** the C++ object and the assembly object.
2. `-c idt_entry.S`: compiles the assembly file to an object file.
3. `ld.lld` (or `$(LD)`) links into `kernel.elf`, then `llvm-objcopy -O binary` writes `kernel.raw`, then **`pad_kernel_raw.py`** extends the raw file to **`MemSiz`** (see “Where this is implemented”).

Deeper explanation:

Freestanding kernels often mix **C++** (for clarity) with a tiny amount of **assembly** for ABI-correct trap entry/exit. The linker combines them into one blob loaded at `kernelPhys`.

## Boot GDT on the stack + `lretq` (before Stage 2 IDT)

**Why:** On `#BP`, the CPU loads **`CS`** from your IDT gate and must read that segment’s descriptor from the **current GDTR**. UEFI’s GDT usually lives in physical pages you **never mapped** under your Stage 1 `CR3` → **#PF** (often with `CR2` pointing into the firmware GDT) and then **#DF**. Mapping that region can also **burn page-table pages** from your tight mmap scratch pool.

**What we do instead:** Before `lidt`, install a **3-entry GDT** in **stack memory** (already identity-mapped), `lgdt` it, then **`lretq`** to reload **`CS = 0x08`** (64-bit code). Immediately reload **`DS`/`ES`/`SS` to `0x10`** so data/stack segments refer to descriptors inside **your** table, not stale UEFI selectors that are now out of **GDT limit**.

The literals `0x00AF9A000000FFFF` / `0x00AF92000000FFFF` are standard long-mode **code** / **data** descriptors (see OSDev GDT examples / Intel layout).

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
alignas(16) uint64_t bootGdt[3] = {
    0,
    0x00AF9A000000FFFFull,
    0x00AF92000000FFFFull,
};
struct BootGdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) bootGdtr = {
    static_cast<uint16_t>(sizeof(bootGdt) - 1),
    reinterpret_cast<uint64_t>(bootGdt),
};
uint64_t code64Sel = 8;
asm volatile(
    "lgdt %[gdtr]\n\t"
    "pushq %[sel]\n\t"
    "leaq 1f(%%rip), %%rax\n\t"
    "pushq %%rax\n\t"
    "lretq\n\t"
    "1:\n\t"
    "movl $0x10, %%eax\n\t"
    "movw %%ax, %%ds\n\t"
    "movw %%ax, %%es\n\t"
    "movw %%ax, %%ss\n\t"
    :
    : [gdtr] "m"(bootGdtr), [sel] "r"(code64Sel)
    : "rax", "memory");
```

### Order of operations in `kernel_entry` (`src-os/kernel/init.cpp`)

This list mirrors the **exact source order** in `kernel_entry` (not the pedagogical Step 1–6 order elsewhere in this chapter). Line numbers are for `src-os/kernel/init.cpp` in the tree today; if you edit the file, re-check them.

1. **`serial::init_115200()`** — L322  
2. **`write_hello_init()`** — L323  

**Stage 1 — paging**

3. **`write_stage1_build_paging()`** — L344  
4. **Paging setup** — L346–L596: constants, descriptor scan, `allocPage`, `mapPageDual` / `mapRangeDual`, `mapUpperHalfAliases` — **no** extra serial markers here.  
5. **`asm volatile("mov %0, %%cr3" …)`** — L600  
6. **`write_stage1_touch_memory()`** — L601  
7. **Upper-half touch** (`testPtr` xor/read/write) — L603–L608  
8. **`write_stage1_done()`** — L610  

**Boot GDT**

9. **`bootGdt[]` / `bootGdtr` / `code64Sel`** — L617–L629  
10. **`asm volatile`**: `lgdt` → `pushq` `0x08` → `leaq 1f(%rip), %rax` → `pushq %rax` → **`lretq`** → label `1:` → **`movw %ax` to `ds` / `es` / `ss`** — L630–L643  

**Stage 2**

11. **`write_stage2_begin()`** — L647 (**after** boot GDT; **first** `STAGE 2:` line on serial).  
12. **Comment block** (linked-at-0 / `kernelPhys`) — L649–L655  
13. **`auto kLinear = [kernelPhys](…) { … };`** — L656–L658  
14. **`IdtEntry* idt = idtTable(kernelPhys);`** — L660  
15. **`uint16_t csSel = 0;`** — L662  
16. **`asm volatile("mov %%cs, %0" …)`** — L663  
17. **`asm volatile("cli" …)`** — L665  
18. **`idtClearAll(idt);`** — L667  
19. **`constexpr uint8_t kVectorBreakpoint` / `kIdtTypeTrap64`** — L668–L669  
20. **`uint64_t bpHandler = kLinear(&isr_breakpoint);`** — L670  
21. **`idtSetGate(…);`** — L671  
22. **`idtLoad(reinterpret_cast<uint64_t>(idt), …);`** — L672  
23. **`write_stage2_install_idt();`** — L674  
24. **`write_stage2_int3_marker();`** — L675  
25. **`asm volatile("int3" …)`** — L676  
26. **`write_stage2_done();`** — L677 (runs after **`iretq`** from `isr_breakpoint`)  
27. **`for (;;) { cli; hlt; }`** — final halt loop (today immediately after Stage 3 in `init.cpp`; line numbers drift—use the source).  

**Takeaway:** **`write_stage2_begin()`** is **after** **`lretq`** and **`ds`/`es`/`ss` reload**, not before. **Stage 3** (PIC/PIT/IRQ0) runs **after** **`write_stage2_done()`**; see [OS-6-irq-pic-study.md](OS-6-irq-pic-study.md) for the ordered list. A hang after **`STAGE 1: paging done`** but before **`STAGE 2: begin`** is in the **boot GDT** block (L617–L646); after **`STAGE 2: begin`**, look at **IDT / `int3` / ISR / `iretq`**.

## Step 1: the ISR stub saves GPRs, aligns stack, calls the dispatcher

### Code (from `src-os/kernel/idt_entry.S`)

```text
.globl isr_breakpoint
isr_breakpoint:
    pushq %rax
    ...
    pushq %r15
    subq $8, %rsp
    call breakpoint_dispatch
    addq $8, %rsp
    popq %r15
    ...
    popq %rax
    movw $0x10, 32(%rsp)   /* saved SS in the 5-qword frame */
    iretq
```

Line-by-line explanation:

1. `.globl isr_breakpoint`: exports a symbol the C++ side can take the address of (`&isr_breakpoint`) when filling the IDT gate (via `kLinear`; see Step 4).
2. `pushq %rax` … `pushq %r15`: saves general-purpose registers so the C++ dispatcher can run without clobbering caller state.
3. `subq $8, %rsp`: fixes **16-byte stack alignment** before the `call` (SysV AMD64 requires `RSP % 16 == 0` immediately before `call`).
4. `call breakpoint_dispatch`: runs the C++ logging function.
5. `addq $8, %rsp`: undoes the alignment adjustment.
6. `popq` sequence: restores GPRs so `%rsp` points at the **saved `RIP`** (bottom of the CPU’s 5-qword frame).
7. `movw $0x10, 32(%rsp)`: overwrites the **saved `SS`** in that frame. After the boot GDT, UEFI’s **`SS` (e.g. `0x30`)** is outside the new GDT limit; **`iretq` would #GP** if left unchanged.
8. `iretq`: returns from the exception, restoring `RIP`, `CS`, `RFLAGS`, `RSP`, and `SS`.

Deeper explanation:

When the CPU delivers vector 3, it pushes the **64-bit exception frame** (see the note above). Your stub pushes extra registers **below** that frame in memory. After you `pop` back to the frame, **`movw …, 32(%rsp)`** targets the **`SS` slot** (assuming no error code). Then `iretq` consumes the full frame.

### Order of operations in `isr_breakpoint` (`src-os/kernel/idt_entry.S`)

**Before the first instruction of the stub**, the CPU has already pushed the **5-quadword** frame onto the stack (see the x86-64 exception note earlier): from **`(%rsp)`** upward **`RIP`**, **`CS`**, **`RFLAGS`**, saved **`RSP`**, **`SS`**. The stub then runs in **source line order** (line numbers for `idt_entry.S` today):

1. **Comment** (file header) — L1–L2  
2. **`.text`** — L4  
3. **`.globl isr_breakpoint`** — L5  
4. **`.balign 16`** — L6  
5. **`isr_breakpoint:`** label — L7  

**Save GPRs** (each `pushq` subtracts 8 from `%rsp`; grows **down** over the CPU frame in memory):

6. **`pushq %rax`** — L8  
7. **`pushq %rcx`** — L9  
8. **`pushq %rdx`** — L10  
9. **`pushq %rbx`** — L11  
10. **`pushq %rbp`** — L12  
11. **`pushq %rsi`** — L13  
12. **`pushq %rdi`** — L14  
13. **`pushq %r8`** — L15  
14. **`pushq %r9`** — L16  
15. **`pushq %r10`** — L17  
16. **`pushq %r11`** — L18  
17. **`pushq %r12`** — L19  
18. **`pushq %r13`** — L20  
19. **`pushq %r14`** — L21  
20. **`pushq %r15`** — L22  

**Call C dispatcher (SysV alignment)**

21. **`subq $8, %rsp`** — L23 — `%rsp mod 16 == 0` at the `call` boundary.  
22. **`call breakpoint_dispatch`** — L24 — runs `write_stage2_dispatcher_bp()` in `init.cpp`.  
23. **`addq $8, %rsp`** — L25 — undo alignment slot.  

**Restore GPRs** (`%rsp` moves back toward the CPU frame; order is **reverse** of save):

24. **`popq %r15`** — L26  
25. **`popq %r14`** — L27  
26. **`popq %r13`** — L28  
27. **`popq %r12`** — L29  
28. **`popq %r11`** — L30  
29. **`popq %r10`** — L31  
30. **`popq %r9`** — L32  
31. **`popq %r8`** — L33  
32. **`popq %rdi`** — L34  
33. **`popq %rsi`** — L35  
34. **`popq %rbp`** — L36  
35. **`popq %rbx`** — L37  
36. **`popq %rdx`** — L38  
37. **`popq %rcx`** — L39  
38. **`popq %rax`** — L40 — after this, **`%rsp`** points at the **saved `RIP`** (slot 0 of the hardware frame).  

**Fix frame for `iretq`**

39. **Block comment** (`UEFI SS` / boot GDT) — L41–L42 (non-executed).  
40. **`movw $0x10, 32(%rsp)`** — L43 — overwrites **saved `SS`** (byte offset **32** from **`RIP`** = 4×8 bytes: `CS`, `RFLAGS`, `RSP`, `SS`).  
41. **`iretq`** — L44 — pops **`RIP`**, **`CS`**, **`RFLAGS`**, **`RSP`**, **`SS`**; resumes after `int3` in `kernel_entry`.  

**Takeaway:** If **`STAGE 2: #BP dispatcher`** prints but **`STAGE 2: done`** does not, suspect **`iretq`** (wrong frame, bad **`SS`**, or wrong **`32(%rsp)`** offset if you add an error-code vector later). If the dispatcher never runs, the fault is **before** **`call`** (very early **`push`**, alignment, or **`breakpoint_dispatch`** link).

### psABI pointer (stack alignment inside the stub)

The SysV AMD64 rule that the stack must be 16-byte aligned before a `call` is documented in the x86-64 psABI “Function Calling Sequence” / stack frame sections. You can use the same GitLab links already cited in Stage 1 docs, e.g.:

- `low-level-sys-info.tex` — “The Stack Frame” / stack alignment before `call`

## Step 2: define the IDT table + gate bytes in C++

### Code (excerpt from `src-os/kernel/init.cpp`)

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

// Storage only: linked at VMA 0, loaded at kernelPhys — never dereference &g_idt[i] directly.
static IdtEntry g_idt[256] __attribute__((aligned(16)));

static IdtEntry* idtTable(uint64_t kernelPhys) {
    return reinterpret_cast<IdtEntry*>(
        kernelPhys + reinterpret_cast<uintptr_t>(static_cast<void*>(&g_idt[0])));
}

static void idtClearAll(IdtEntry* t) {
    for (unsigned i = 0; i < 256; ++i) {
        t[i].offset_lo = t[i].offset_mid = t[i].offset_hi = 0;
        t[i].selector = t[i].ist = t[i].type_attr = 0;
        t[i].reserved = 0;
    }
}

static void idtSetGate(IdtEntry* t, uint8_t vector, uint64_t handlerVirt,
                       uint16_t cs, uint8_t typeAttr) {
    IdtEntry* e = &t[vector];
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

1. `struct IdtEntry … packed`: **in-memory byte layout** of one IDT gate.
2. `g_idt[256]`: reserves **256 entries**; `lidt` limit stays `sizeof(g_idt) - 1`.
3. **`idtTable(kernelPhys)`**: the kernel ELF is linked at **VMA 0** but UEFI copies the blob to **`kernelPhys`**. Identity mapping gives **linear = `kernelPhys` + link offset**. Using `&g_idt[0]` directly would touch **low addresses like `0x20c0`**, which are **not mapped** → hang or fault. The helper forms the **correct linear pointer**.
4. `idtClearAll(t)` / `idtSetGate(t, …)`: mutate the table through that pointer.

Deeper explanation:

Ensure **`kernel.raw` includes `.bss`**: `llvm-objcopy -O binary` often stops at **`FileSiz`**. This tree uses **`ld.ld` `PHDRS`** (one `PT_LOAD`) and **`pad_kernel_raw.py`** so the raw file is padded to **`MemSiz`** and `g_idt` is actually present in RAM.

<details style="border:1px solid #ccc;border-radius:6px;padding:0.5em 0.75em;background:#f7f7fb;margin:0.75em 0;">
<summary style="font-weight:bold;cursor:pointer;">C++ note: <code>__attribute__((packed))</code> and handler addresses (skip if known)</summary>

- `packed` keeps IDT layout bit-exact.
- The **handler address** written into the gate must be **`kLinear(&isr_breakpoint)`** (Step 4), not a bare link-time `reinterpret_cast` of the symbol by itself.

</details>

## Step 3: load the IDT with `lidt`

### Code (excerpt from `src-os/kernel/init.cpp`)

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

Pass **`reinterpret_cast<uint64_t>(idt)`** (with `idt = idtTable(kernelPhys)`), not `reinterpret_cast<uint64_t>(&g_idt[0])`: the latter is still a **link-time low address**, not the **loaded** linear address.

## Step 4: fill vector 3 with the handler’s linear address + current `CS`

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
auto kLinear = [kernelPhys](const void* p) -> uint64_t {
    return kernelPhys + static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
};

IdtEntry* idt = idtTable(kernelPhys);

uint16_t csSel = 0;
asm volatile("mov %%cs, %0" : "=r"(csSel));

asm volatile("cli" ::: "memory");

idtClearAll(idt);
constexpr uint8_t kVectorBreakpoint = 3;
constexpr uint8_t kIdtTypeTrap64 = 0x8F;
uint64_t bpHandler = kLinear(reinterpret_cast<const void*>(&isr_breakpoint));
idtSetGate(idt, kVectorBreakpoint, bpHandler, csSel, kIdtTypeTrap64);
idtLoad(reinterpret_cast<uint64_t>(idt), static_cast<uint16_t>(sizeof(g_idt) - 1));
```

Line-by-line explanation:

1. **`kLinear(p)`**: turns any **link-time symbol address** `p` (VMA 0 + offset) into **`kernelPhys + offset`**, the **identity-mapped linear address** the CPU actually runs from.
2. **`idtTable(kernelPhys)`**: **linear pointer** to the IDT array in RAM.
3. `mov %%cs, %0`: reads **`CS`** after the boot GDT path, so you typically get **`0x08`** (your 64-bit code selector), not a firmware-only value.
4. `cli`: keeps the window predictable (optional but used in-tree).
5. `kLinear(&isr_breakpoint)`: **gate target** must use the same **load address** fix as the IDT base.
6. `idtLoad(reinterpret_cast<uint64_t>(idt), …)`: `lidt` with the **real** table location.

<details style="border:1px solid #ccc;border-radius:6px;padding:0.5em 0.75em;background:#f7f7fb;margin:0.75em 0;">
<summary style="font-weight:bold;cursor:pointer;">C++ note: <code>reinterpret_cast</code> for function addresses (skip if known)</summary>

You still take **`&isr_breakpoint`** as a `void*` / symbol; **`kLinear`** adds **`kernelPhys`**. The integer passed into `idtSetGate` is the **linear** handler address for the gate encoding.

</details>

## Step 5: trigger `int3`, then print “done” after return

### Code (excerpt from `src-os/kernel/init.cpp`)

```cpp
write_stage2_install_idt();
write_stage2_int3_marker();
asm volatile("int3" ::: "memory");
write_stage2_done();
```

Line-by-line explanation:

1. `write_stage2_int3_marker()`: prints a marker **before** executing `int3` (proves we reached the trigger site).
2. `int3`: raises #BP vector 3, which vectors through IDT entry 3 into `isr_breakpoint`.
3. `write_stage2_done()`: runs **after** the handler returns (proves we successfully executed `iretq` back to normal flow).

Deeper explanation:

If the IDT entry is wrong, the CPU may **triple-fault** or hang before printing. If the stub mis-uses the stack, return may crash. Seeing `STAGE 2: done` is a strong end-to-end check.

## Step 6: dispatcher prints over serial (the “proof”)

### Code (excerpt from `src-os/kernel/init.cpp`)

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
- `STAGE 2: begin`
- `STAGE 2: install idt`
- `STAGE 2: int3`
- `STAGE 2: #BP dispatcher`
- `STAGE 2: done`

## Troubleshooting

- **Reboot loop / instant reset**: often a **bad IDT gate** or bad `iretq` stack layout (exception frame destroyed).
- **No `STAGE 2: begin` after `STAGE 1: paging done`**: almost always a **stale** `kernel.raw` inside `BOOTX64.EFI` — run `make clean && make build` from `src-os/` so UEFI is rebuilt after the kernel.
- **`STAGE 2: begin` then hang before `install idt`**: often **touching `g_idt` at a link address** (not `kernelPhys + offset`) or **`.bss` missing from `kernel.raw`** — see Step 2 (`idtTable`, `pad_kernel_raw.py`, `ld.ld`).
- **`STAGE 2: #BP dispatcher` but not `STAGE 2: done`**: after the **boot GDT**, `iretq` reloads **`SS`** from the frame; if it still holds UEFI’s **`SS` (e.g. `0x30`)** outside your GDT limit, **`iretq` #GP** — fix saved **`SS`** in `idt_entry.S` or extend the GDT (see Step 1).
- **Hang with no `STAGE 2: #BP dispatcher`**: bad gate / wrong **`kLinear`** handler address, **IDT base** not `idtTable`, firmware GDT path if you skip the boot GDT, or crash before serial prints.

## Next milestone hint

After you trust exceptions, the next step is **IRQs** (PIC/APIC), **masking**, and **EOI**. The full tutorial chapter for `src-os/` is **[OS-6-irq-pic-study.md](OS-6-irq-pic-study.md)** (PIC remap, PIT, vector 32, `sti`/`hlt`, EOI).

**Skift reference (this repo):** [`src-os-skift/skift/`](src-os-skift/skift/) — quick map:

| Topic | File |
| --- | --- |
| 8259 remap + `0x20` master EOI | [`src-os-skift/skift/src/kernel/hal-x86_64/pic.h`](src-os-skift/skift/src/kernel/hal-x86_64/pic.h) |
| PIT tick setup | [`src-os-skift/skift/src/kernel/hal-x86_64/pit.h`](src-os-skift/skift/src/kernel/hal-x86_64/pit.h) |
| 256 IDT stubs → C | [`src-os-skift/skift/src/kernel/hjert/x86_64/ints.s`](src-os-skift/skift/src/kernel/hjert/x86_64/ints.s) |
| `init()` wires PIC+PIT; `_intDispatch` does `irq = intNo - 32` and `_pic.ack` | [`src-os-skift/skift/src/kernel/hjert/x86_64/arch.cpp`](src-os-skift/skift/src/kernel/hjert/x86_64/arch.cpp) |
