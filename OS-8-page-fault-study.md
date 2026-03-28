# OS-8 — Stage 5 page fault #PF (`src-os/` tutorial tree)

This page walks through **OS-8** (the fifth **boot stage** after Stages 1–4) following [OS-7](OS-7-lapic-study.md): install an **IDT gate for vector 14** (#PF), handle the **error code** the CPU pushes on the stack, read **`CR2`** (faulting **linear** address), print proof on **COM1**, **advance `RIP`** past the faulting instruction, and return with **`iretq`**.

Serial markers use **`STAGE 5:`** — the same numbering scheme as **`STAGE 1`–`STAGE 4`** (stage = boot phase). The study file is still **OS-8** in the book series.

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

## Where this is implemented

- `pf_entry.S` — **`isr_page_fault`**, **`lea 128(%rsp), %rdi`**, **`call page_fault_dispatch`**, **`addq $8, %rsp`** before **`iretq`**.
- `init.cpp` — **`kProbeUnmappedVa`**, Stage 5 writers (`write_stage5_*`), **`page_fault_dispatch`**, **`idtSetGate(..., 14, ..., 0x8F)`**, inline asm probe.
- `Makefile` — **`pf_entry.o`**.

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
