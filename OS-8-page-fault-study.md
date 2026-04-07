# OS-8 — Stage 5 page fault #PF (`src-os/` tutorial tree)

This page walks through **OS-8** (the fifth **boot stage**) following [OS-7](OS-7-lapic-study.md): install an **IDT gate for vector 14** (#PF), handle the **error code** the CPU pushes on the stack, read **`CR2`** (faulting **linear** address), print proof on **COM1**, **advance `RIP`** past the faulting instruction, and return with **`iretq`**.

## What we had before

1. **Stage 1** paging: only some ranges are mapped (kernel, stack, mmap touch page, LAPIC).
2. **IDT** without #PF: exceptions and IRQs covered, but **no** handler for **page faults**.

When code touches a **linear address** with **no present leaf PTE** (or protection violation), the CPU raises **#PF**, loads **`CR2`** with the faulting address, and pushes an **error code** (details in Intel SDM / [OSDev #PF](https://wiki.osdev.org/Page_Fault)).

## Stage 5 goal

1. Add **`pf_entry.S`**: **`isr_page_fault`** saves GPRs, passes a pointer to the **hardware frame** (starting at the **error code**) into **`page_fault_dispatch`**, then restores GPRs, **drops the error code** from the stack, patches **saved `SS`**, **`iretq`**.
2. In **`page_fault_dispatch`**: log **`STAGE 5: #PF dispatch`**, read **`CR2`**, confirm it matches the **probe VA** (`kProbeUnmappedVa` = **`0xABC00000`**) and print **`cr2 ok`** (else **`cr2 bad`**).
3. **Skip** the faulting instruction by **`frame[1] += 2`** for the **`movb (%rax), %al`** probe (encoding **`8A 00`** — **two** bytes; verify with `llvm-objdump` if you change the probe).
4. Install a **64-bit trap gate** (`0x8F`, same as #BP) for vector **14**, **`kLinear(&isr_page_fault)`**, **`lidt`**.

### Stack layout vs OS-5 / OS-6 / OS-7

For **#BP** and **IRQ0** without privilege change, the CPU pushes **five** quadwords: **`RIP`, `CS`, `RFLAGS`, `RSP`, `SS`** (see OS-5).

For **#PF**, the CPU pushes **six** quadwords: **`error code`**, then those **five**. The **saved `SS`** is at **`32(%rsp)`** **after** you **`addq $8, %rsp`** to discard the error code — same **`movw $0x10, 32(%rsp)`** pattern as the no-error-code stubs.

### Why a fixed probe VA?

The tutorial picks **`0xABC00000`**, which is **outside** the Stage 1 mappings on typical **`src-os/`** QEMU runs. If your firmware or future mappings cover it, pick another **unmapped** canonical address and update **`kProbeUnmappedVa`** and the **`kPfProbeInsnLen`** check in the disassembly.

---

## Memory and CPU state snapshot (Stage 5 — when #PF fires)

**Context:** [OS-4](OS-4-paging-study.md) left you with **`CR3` → your PML4**; Stages 2–4 installed **IDT** gates (3, 32, 34, …). Stage 5 adds **IDT[14]** → **`isr_page_fault`**. The probe **`movb (%rax), %al`** uses **`rax = kProbeUnmappedVa`**; the MMU finds **no present leaf** → **#PF**.

**`CR2` (control register):**

```
CR2  ←  faulting linear address  (here: 0xABC00000 — same as probe VA for “not present”)
```

Only the **faulting** effective address is recorded; **`CR2` is not** the physical frame.

**Page tables (unchanged during the fault):**

```text
  Linear VA 0xABC00000
       │
       ▼
  Walk PML4 → PDPT → PD → PT  ──►  absent or not-present PTE  ──►  #PF (not successful translation)
```

**Hardware stack frame (#PF pushes an error code first):** growing **down** in memory (low address = top of stack after push):

```text
  RSP at handler entry (before stub pushes GPRs):

      ┌────────────────────┬────────────────────────────────────────────┐
      │ Offset from RSP    │ Quadword pushed by CPU (vector 14)        │
      ├────────────────────┼────────────────────────────────────────────┤
      │ +0                 │ error code (PF flags: P/U/R/W, …)          │
      │ +8                 │ saved RIP  (points at faulting insn)      │
      │ +16                │ saved CS                                  │
      │ +24                │ saved RFLAGS                              │
      │ +32                │ saved RSP                                 │
      │ +40                │ saved SS   (patch 0x10 before iretq)      │
      └────────────────────┴────────────────────────────────────────────┘

  isr_page_fault then pushes RAX..R15 (+ alignment gap);  leaq 128(%rsp),%rdi
  passes &frame[0] to page_fault_dispatch  with  frame[0]=error, frame[1]=RIP, …
```

**After `page_fault_dispatch` returns:** stub **pops GPRs**, **`addq $8,%rsp`** discards error code so **`iretq`** sees the **5-qword** frame; **`movw $0x10, 32(%rsp)`** fixes **`SS`**; **`iretq`** resumes at **`RIP + kPfProbeInsnLen`** (skips **`8A 00`**).

**Relations / paths:**

```text
  user/kernel insn fetch or load  ──►  MMU walk  ──►  fail
       │
       ▼
  #PF: vector 14, CR2=VA, error code on stack
       │
       ▼
  IDT[14] ──► isr_page_fault ──► page_fault_dispatch(frame)
       │                              │
       │                              ├─► read CR2, compare kProbeUnmappedVa
       │                              └─► frame[1] (RIP) += 2
       ▼
  iretq  ──►  continue after probe asm
```

**Compared to OS-5 / OS-6 / OS-7:** those use **5** pushes from the CPU (no error code). **#PF** uses **6** pushes; the stub’s **`addq $8,%rsp`** before **`iretq`** reconciles the frame with the **no-error-code** return path.

---

## Walkthrough for beginners (with code excerpts)

This section is for readers who are new to **page faults**, **`CR2`**, and **assembly ISRs**. Read it top to bottom; each block matches real files under [`src-os/kernel/`](src-os/kernel/).

### 1. What the CPU does on a page fault (in one minute)

- Your program runs an instruction that **reads or writes memory**, e.g. “load one byte from the address in **`rax`**.”
- The MMU walks the **page tables** ([OS-4](OS-4-paging-study.md)). If there is **no valid mapping** for that **linear address** (or a **protection** violation), the CPU does **not** complete the load normally.
- Instead it raises **#PF**, **vector 14**. It saves **`CR2`** = the **faulting linear address** (the address the program tried to use). It also pushes an **error code** (a small integer with **bits** describing *why* it faulted—present/user/R/W, etc.).
- Your **IDT[14]** must point to **`isr_page_fault`**. That code runs **instead of** the faulting instruction finishing. Later, **`iretq`** returns to user/kernel code—but **only if** you fix up the saved **`RIP`** (and stack) so execution **continues past** the faulting instruction, or you map the page, etc.

In this tutorial we **intentionally** fault, then **skip** exactly **two bytes** of machine code so **`iretq`** resumes **after** the bad load.

### 2. The probe address (constant in `init.cpp`)

We need **one** linear address we are sure is **not mapped** in Stage 1:

```cpp
// Deliberately unmapped VA for Stage 5 #PF probe (not covered by Stage 1 tables).
static constexpr uintptr_t kProbeUnmappedVa = 0xABC00000ull;
```

- **`static constexpr`** — known at **compile time**, shared by the probe and the **`CR2`** check.
- **`0xABC00000`** — picked to sit in a range our minimal page tables usually **do not** cover. If your run maps it by accident, change the constant and rebuild.

### 3. Install gate 14, then deliberately fault (`kernel_entry` in `init.cpp`)

After Stage 4 finishes, Stage 5 runs this sequence (comments shortened):

```cpp
    // Stage 5: #PF with error code on stack; CR2 holds faulting linear address.
    write_stage5_begin();
    constexpr uint8_t kVectorPageFault = 14;
    uint64_t pfHandler = kLinear(reinterpret_cast<const void*>(&isr_page_fault));
    idtSetGate(idt, kVectorPageFault, pfHandler, csSel, kIdtTypeTrap64);
    idtLoad(reinterpret_cast<uint64_t>(idt), static_cast<uint16_t>(sizeof(g_idt) - 1));
    write_stage5_pf_gate();
    write_stage5_probe();
    {
        uintptr_t probe = kProbeUnmappedVa;
        asm volatile("movb (%%rax), %%al" : : "a"(probe) : "memory");
    }
    write_stage5_done();
```

Line-by-line:

1. **`write_stage5_begin()`** — prints `STAGE 5: begin` on serial (same “char by char” style as other stages).
2. **`kVectorPageFault = 14`** — Intel assigns **#PF** to **vector 14** (decimal). This number is **fixed by the architecture**, not chosen by us.
3. **`kLinear(&isr_page_fault)`** — the IDT must contain the **physical/runtime** address of the handler. The kernel is linked at VMA 0 but loaded at **`kernelPhys`**; **`kLinear`** adds **`kernelPhys`** to the symbol’s offset ([OS-5](OS-5-idt-study.md)).
4. **`idtSetGate(idt, 14, pfHandler, csSel, kIdtTypeTrap64)`** — fill **IDT entry 14**. **`kIdtTypeTrap64`** is **`0x8F`** (64-bit **trap** gate), same family as #BP in OS-5: **`IF`** is **not** automatically cleared on entry (fine for this test).
5. **`idtLoad(...)`** — **`lidt`**: CPU now uses our table for exceptions.
6. **`write_stage5_pf_gate()` / `write_stage5_probe()`** — serial markers so you see order in QEMU.
7. **`probe = kProbeUnmappedVa`** — put the bad address into **`rax`** (the **`"a"`** constraint).
8. **`asm volatile("movb (%%rax), %%al" ...)`** — **one** machine instruction: “read a byte from memory at **`[rax]`** into **`al`**.” That load **faults** → CPU jumps to **`isr_page_fault`**.
9. After **`iretq`** returns from the handler, execution continues **after** this **`asm`** block; then **`write_stage5_done()`** runs.

**Why a block `{ ... }` around the asm?**  
Scopes the **`probe`** variable; it’s a small way to make the “faulting instruction” visually obvious in source.

### 4. The C dispatcher: `page_fault_dispatch` (`init.cpp`)

The assembly stub passes **`rdi`** = pointer to a **mini array** of **`uint64_t`** values that **mirror** the CPU’s stack frame (error code + saved **`RIP`**, **`CS`**, …):

```cpp
// `movb (%rax), %al` after #PF — skip this many bytes before `iretq` (see OS-8).
static constexpr uint64_t kPfProbeInsnLen = 2;

extern "C" void page_fault_dispatch(uint64_t* frame) {
    (void)frame[0];
    write_stage5_dispatch();
    uint64_t cr2 = 0;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    if (cr2 == static_cast<uint64_t>(kProbeUnmappedVa)) {
        write_stage5_cr2_ok();
    } else {
        write_stage5_cr2_bad();
    }
    frame[1] += kPfProbeInsnLen;
}
```

Line-by-line:

1. **`frame[0]`** — the **error code** pushed by the CPU. We ignore it here (`(void)`), but a real kernel would decode **bits** (not present, user/supervisor, etc.).
2. **`write_stage5_dispatch()`** — prints `STAGE 5: #PF dispatch` so you know the C handler ran.
3. **`mov %%cr2, %0`** — **`CR2`** is a **control register**; only assembly can read it into **`cr2`**. That value should be the **same** linear address as **`kProbeUnmappedVa`** for a “not present” fault on this probe.
4. **Compare `cr2` to `kProbeUnmappedVa`** — if equal, print **`cr2 ok`**; otherwise **`cr2 bad`** (wrong fault, or unexpected mapping).
5. **`frame[1] += kPfProbeInsnLen`** — **`frame[1]`** is the **saved `RIP`** (return address). Adding **2** skips the **2-byte** instruction **`8A 00`** (`mov al, [rax]`). **After** `iretq`, the CPU resumes at **`RIP + 2`**, i.e. **after** the faulting load. If you change the probe instruction, **disassemble** (`llvm-objdump`) and update **`kPfProbeInsnLen`**.

### 5. The assembly stub: `pf_entry.S` (full excerpt)

```asm
.globl isr_page_fault
isr_page_fault:
    pushq %rax
    /* ... pushq %rcx ... %r15 — same order as idt_entry.S / irq_entry.S */
    subq $8, %rsp
    leaq 128(%rsp), %rdi
    call page_fault_dispatch
    addq $8, %rsp
    popq %r15
    /* ... popq %r14 ... %rax */
    addq $8, %rsp
    movw $0x10, 32(%rsp)
    iretq
```

What’s going on:

- **Push all GPRs** — so **`page_fault_dispatch`** can use C calling conventions without clobbering the interrupted program’s registers.
- **`subq $8, %rsp`** — keeps the stack **16-byte aligned** before **`call`** (SysV AMD64 requirement).
- **`lea 128(%rsp), %rdi`** — first argument to C = **`rdi`** = address where **`frame[0]`** = error code, **`frame[1]`** = **`RIP`**, etc. **`128`** = **15 × 8** (pushed registers) **+ 8** (alignment gap), because **`rsp`** now points **below** the CPU’s original frame; **see** the stack picture in [OS-5](OS-5-idt-study.md) and the “error code” note above.
- **`call page_fault_dispatch`** — runs the C code; it may adjust **`frame[1]`** (`RIP`).
- **`addq $8, %rsp`** after return — undo the alignment **`subq`**.
- **Pop all GPRs** — restore the interrupted context.
- **`addq $8, %rsp`** — **pop the error code** off the stack so **`rsp`** points at the **saved `RIP`** (what **`iretq`** expects **after** a fault with error code).
- **`movw $0x10, 32(%rsp)`** — same **boot GDT** fix as OS-5/OS-6/OS-7: saved **`SS`** must be **`0x10`** in our tiny GDT.
- **`iretq`** — return from exception: restores **`RIP`, `CS`, `RFLAGS`, `RSP`, `SS`** from the stack and **also** consumes the error code because we adjusted **`rsp`**.

### 6. “Not simple” vs this tutorial

| This tutorial | A bigger kernel might add |
| --- | --- |
| One fixed probe VA | **Demand paging**, **COW**, **user** faults |
| Ignore `frame[0]` bits | Decode **error code**: not-present vs protection, user vs supervisor |
| Bump **`RIP`** by 2 | **Emulate** insn, **map** a page and retry, or **kill** the task |
| **Trap** gate **`0x8F`** | Might use **interrupt** gate or **IST** for double-fault |

## Where this is implemented

- `pf_entry.S` — **`isr_page_fault`**, **`lea 128(%rsp), %rdi`**, **`call page_fault_dispatch`**, **`addq $8, %rsp`** before **`iretq`**.
- `init.cpp` — **`kProbeUnmappedVa`**, Stage 5 writers (`write_stage5_*`), **`page_fault_dispatch`**, **`idtSetGate(..., 14, ..., 0x8F)`**, inline asm probe.
- `Makefile` — **`pf_entry.o`**.

## Changed and new files

| Change | Path |
| --- | --- |
| **New** | [`src-os/kernel/pf_entry.S`](src-os/kernel/pf_entry.S) — `isr_page_fault` |
| **Changed** | [`src-os/kernel/init.cpp`](src-os/kernel/init.cpp) — `kProbeUnmappedVa`, `page_fault_dispatch`, IDT gate 14, Stage 5 serial |
| **Changed** | [`src-os/kernel/Makefile`](src-os/kernel/Makefile) — link `pf_entry.o` |

## Order of operations in `kernel_entry` (Stage 5 only)

After **`write_stage4_done()`**:

1. **`write_stage5_begin()`**
2. **`idtSetGate`** vector **14**, **`kLinear(&isr_page_fault)`**, type **`0x8F`**
3. **`idtLoad`**
4. **`write_stage5_pf_gate()`**
5. **`write_stage5_probe()`**
6. **`movb (%rax), %al`** with **`rax = kProbeUnmappedVa`** → **#PF**
7. **`page_fault_dispatch`** runs → **`write_stage5_dispatch`**, **`CR2` check**, **`frame[1] += 2`**
8. **`iretq`** resumes after the **2-byte** load
9. **`write_stage5_done()`**
10. **`for (;;) { cli; hlt; }`**

## Expected serial output (QEMU)

After Stage 4:

- `STAGE 5: begin`
- `STAGE 5: pf gate`
- `STAGE 5: probe`
- `STAGE 5: #PF dispatch`
- `STAGE 5: cr2 ok`
- `STAGE 5: done`

## Troubleshooting

- **Triple fault**: wrong **error-code** stack math (check **`128(%rsp)`** vs your push sequence), wrong **`iretq`** **`SS`** offset, or **`kPfProbeInsnLen`** not matching the real probe (falls back into the same faulting insn).
- **`cr2 bad`**: probe VA is actually mapped, or **`CR2`** does not match expectations (e.g. different fault type).
- **Hang on probe**: **IDT[14]** missing or gate points at unmapped code.

## Next milestone hint

Useful follow-ons: **install a real #PF policy** (grow stack, demand-zero pages, copy-on-write), parse **ACPI MADT** and program the **I/O APIC**, or **MSI** for PCI. The constant skills are **frame layout**, **`CR2`**, and **page tables** that match every access your kernel makes.
