# OS-3 — Roadmap to a real OS (was milestone 16)

This guide is intentionally written to match the structure and detailing style of `OS-4-paging-study.md`.

Milestone 16 turns “hello-init” into a foundation for a real kernel by expanding what the kernel can do, while keeping the same boot boundary learning goal:

`UEFI -> ExitBootServices -> kernel entry -> serial log`

The milestone list below mirrors Skift-like components, but you implement them from scratch for the tutorial.

## Changed and new files

This chapter is a **roadmap** only: it does not introduce a separate tree by itself. Concrete **new and changed files** for each milestone live in the later study pages—especially [`OS-4-paging-study.md`](OS-4-paging-study.md) through [`OS-8-page-fault-study.md`](OS-8-page-fault-study.md)—and in [`src-os/`](src-os/).

## Step 0: How to use the progression (the “contract boundary” habit)

Each milestone should end with:

- one new kernel log line visible in QEMU serial output
- one clearly testable behavior difference

Concrete QEMU test principle:
- After each subsystem is introduced, print `STAGE X: done` over serial.
- If QEMU halts unexpectedly, the last printed marker tells you which boundary failed.

## Roadmap: how each milestone extends memory and CPU state

This file does not add a new kernel stage; it names the **sequence** implemented in [OS-4](OS-4-paging-study.md)–[OS-8](OS-8-page-fault-study.md). Use this diagram as an index of **what gets defined where**.

```text
                    ┌─────────────────────────────────────────────────────────┐
  RAM               │  UEFI: kernel blob, stack, mmap buffer (phys pages)      │
  (always)          └──────────────────────────┬────────────────────────────┘
                                               │
     OS-4  Stage 1  │  BUILD: PML4→PDPT→PD→PT  pages in RAM
                     │  FILL:   identity + UPPER_HALF aliases for kernel,
                     │          stack, mmap, (later LAPIC page in OS-7 tree)
                     │  CPU:    CR3 ← phys(PML4)   [every access now via YOUR tables]
                     ▼
     OS-5  Stage 2  │  BUILD: boot GDT[3] in mapped RAM, GDTR; g_idt[256], IDTR
                     │  CPU:    lgdt → lretq → CS=0x08, DS/ES/SS=0x10; lidt
                     │  PATH:   int3 → IDT[3] → isr → dispatcher → iretq
                     ▼
     OS-6  Stage 3  │  BUILD: PIC remap+mask, PIT rate; IDT[32] → irq stub
                     │  I/O:     outb PIC/PIT ports (not MMIO)
                     │  PATH:   IRQ0 → vector 32 → EOI(out 0x20) → iretq; IF via sti
                     ▼
     OS-7  Stage 4  │  NEED:    LAPIC page mapped @ 0xFEE00000 (CR3 already)
                     │  BUILD:   mask PIC; SVR enable; LVT timer → vector 34
                     │  PATH:   LAPIC tick → IDT[34] → LAPIC EOI(MMIO 0xB0) → iretq
                     ▼
     OS-8  Stage 5  │  BUILD:   IDT[14] → pf_entry.S
                     │  CPU:     #PF pushes ERROR + 5 qw; CR2 ← faulting linear
                     │  PATH:    dispatch adjusts saved RIP; drop error code; iretq
```

**Register “ownership” summary:**

| Milestone | New or critical CPU state |
| --- | --- |
| OS-4 | **`CR3`**, **`CR0.PG`** (paging on with your tables) |
| OS-5 | **`GDTR`**, **`IDTR`**, **`CS`/`SS`** selectors from **your** GDT |
| OS-6 | **`RFLAGS.IF`** (sti/cli), PIC **IMR** via I/O ports |
| OS-7 | LAPIC **MMIO** at mapped **`0xFEE00000`**, **SVR**, **LVT**, **EOI** register |
| OS-8 | **`CR2`** on #PF, **IDT[14]**, exception frame **+ error code** |

## Step 1: Paging / VMM (make the memory model real)

### Goal

Build a minimal page table setup so the kernel can run in a predictable virtual address space.

### Teaching tasks

- Use the UEFI memory map to decide which physical pages are free/reserved.
- Establish minimal identity mapping (or dual mapping) long enough to enable paging.
- Re-establish stack + kernel text mapping after switching address spaces.

### Code pointer (activate paging)

Skift’s VMM conceptually “goes live” by loading the paging root:

```cpp
void activate() override {
    x86_64::wrcr3(root());
}
```

Line-by-line explanation:

1. `activate()`: the moment when the VMM becomes active.
2. `wrcr3(root())`: writes the paging root physical address into the `CR3` CPU register.

C++ note: what `override` means

- `override` tells the compiler this method is intended to override a virtual method from a base class/interface.
- If the signature doesn’t match what the base expects, the compiler will error. This helps catch wiring mistakes early in complex kernel code.

Deeper explanation:

Before `CR3` updates, address translation comes from whatever paging root was active (or might not exist in your tutorial). After `CR3` updates, every instruction fetch and memory access uses your newly installed page tables. That’s why the paging milestone is always followed by a quick serial-visible sanity check.

### Verification

- Print `STAGE 1: done`.
- In QEMU, confirm paging works by doing at least one mapped read/write sanity check (a “touch memory” test).

## Step 2: Interrupt / exception vectors and register frames

### Goal

Turn hardware events into kernel-visible frames, so you can log interrupt causes and faults.

### Teaching tasks

- Install an IDT.
- Program a basic interrupt controller (PIC/PIT for a first step, later APIC).
- Provide a dispatcher surface so you can log interrupt causes over serial.

### Code pointer (load IDT)

```asm
_idtLoad:
    lidt  [rdi]
    ret
```

Line-by-line explanation:

1. `_idtLoad:` entry label for the loader stub.
2. `lidt [rdi]`: loads the IDT register with the IDT descriptor whose address is in `rdi`.
3. `ret`: return to the caller.

Deeper explanation:

After the IDT is loaded, the CPU can translate an “interrupt vector number” into a handler entry point. Without this, faults/IRQs may triple-fault or appear as “mystery halts”. This step is the gateway from “CPU exceptions are just crashes” to “exceptions become structured kernel events.”

### Verification

- Print `STAGE 2: done`.
- Trigger a known exception (commonly `int3`) and confirm your dispatcher logs it.

## Step 3: Syscalls (control-flow boundary inside the kernel)

### Goal

Add a syscall entry path (a trap into the kernel) and dispatch syscalls by syscall number, returning a value in an agreed register.

### Teaching tasks

- Implement a syscall trampoline that builds a register frame.
- Dispatch based on syscall number.
- Return a value in the agreed register.

### Code pointer (dispatch)

```cpp
extern "C" usize _sysDispatch(usize sp) {
    auto* frame = reinterpret_cast<Frame*>(sp);

    auto result = Core::doSyscall(
        (Hj::Syscall)frame->rax,
        {
            frame->rdi,
            frame->rsi,
            frame->rdx,
            frame->r10,
            frame->r8,
            frame->r9,
        }
    );
    ...
}
```

Line-by-line explanation:

1. `_sysDispatch(usize sp)`: syscall dispatcher entry; `sp` points to a saved register frame.
2. `frame = reinterpret_cast<Frame*>(sp)`: treat the stack/register frame as the architecture-specific `Frame` layout.
3. `Core::doSyscall((Hj::Syscall)frame->rax, {...})`: choose the syscall number from `rax`, pass arguments from registers.

C++ note: `extern "C"` + `reinterpret_cast` here

- `extern "C"`: prevents C++ name mangling and keeps the entry symbol/ABI predictable for the trap/assembly boundary.
- `reinterpret_cast<Frame*>(sp)`: tells the compiler to treat the raw pointer value `sp` as pointing to a `Frame` struct, so `frame->rax`, `frame->rdi`, etc. can read the saved register fields.

Deeper explanation:

This is the key “boundary contract” inside the kernel: CPU trap -> register frame -> kernel dispatch -> defined return path. Once you can log and validate this mapping, later kernel services (scheduler, IPC, capabilities) become “normal kernel functions” behind a syscall surface.

### Verification

- Print `STAGE 3: done`.
- Implement one syscall end-to-end and confirm the return value over serial.

## Step 4: Scheduler + context switching

### Goal

Replace the “single endless kernel loop” with task scheduling and context save/restore at the assembly boundary.

### Teaching tasks

- Define task states (RUNNABLE / BLOCKED / EXITED).
- Implement context save/restore switching `RSP` + registers.
- Add a timer tick source to drive time slicing.

### Code pointer (scheduler trigger)

Skift uses an interrupt to force scheduling:

```cpp
void yield() {
    asm volatile("int $100");
}
```

Line-by-line explanation:

1. `yield()`: cooperative scheduling primitive.
2. `int $100`: software interrupt that transfers control into interrupt/trap dispatch.

C++ note: what `asm volatile` is doing in `yield()`

- `asm volatile("int $100")` embeds an `int` instruction into the generated machine code.
- `volatile` prevents the compiler from optimizing/reordering away the instruction, which would break the scheduling trigger.

Deeper explanation:

Even though it looks “simple”, the architecture detail matters: calling into the scheduler requires a mechanism that already saves enough CPU state to safely resume another context. This is why interrupts and traps (Step 2/3) come before scheduler context switching (Step 4).

### Verification

- Print `STAGE 4: done`.
- Confirm two cooperative tasks alternate observable behavior (e.g., printing over serial).

## Step 5: Capabilities / pledges / domains

### Goal

Build permissioned kernel objects so syscalls enforce “what a task is allowed to do”.

### Teaching tasks

- Define `Cap`, `Pledge`, and domain slot tables.
- Enforce permission checks inside syscall handlers.
- Make address spaces explicit (even if initially simplistic).

### Code pointer (permission check)

```cpp
Res<> ensure(Flags<Hj::Pledge> pledge) {
    ObjectLockScope _(*this);

    if (not _pledges.has(pledge)) {
        return Error::permissionDenied("task does not have pledge");
    }
    return Ok();
}
```

Line-by-line explanation:

1. `ensure(...)`: enforce that the current task has a required pledge.
2. `if (not _pledges.has(pledge))`: permission check fails if the pledge isn’t present.
3. `permissionDenied(...)`: returns an error instead of performing the operation.

Deeper explanation:

Capabilities/pledges make kernel object access explicit and testable. Once you can force a syscall to fail because of missing permission, you can validate that your permission model works as a first-class security contract, not an afterthought.

### Verification

- Print `STAGE 5: done`.
- Demonstrate a failing permission check with a serial log.

## Step 6: IPC primitives (channels/pipes/listeners)

### Goal

Introduce message/byte-stream communication primitives with correct wakeups.

### Teaching tasks

- Implement one IPC primitive end-to-end:
  - `SEND/RECV` via a ring buffer for message caps, or
  - `WRITE/READ` via a ring buffer for bytes, or
  - `POLL` for readiness.

### Code pointer (send/recv)

```cpp
Res<Hj::SentRecv> send(Domain& dom, Bytes bytes, Slice<Hj::Cap> caps) {
    ObjectLockScope scope{*this};
    try$(_ensureOpen());
    ...
    for (usize i = 0; i < bytes.len(); i++)
        _bytes.pushBack(bytes[i]);
    _sr.pushBack({bytes.len(), caps.len()});
    _updateSignalsUnlock();
    return Ok<Hj::SentRecv>(bytes.len(), caps.len());
}

Res<Hj::SentRecv> recv(Domain& dom, MutBytes bytes, MutSlice<Hj::Cap> caps) {
    ObjectLockScope scope{*this};
    try$(_ensureOpen());
    ...
    if (_sr.len() == 0)
        return Error::wouldBlock("no messages available");
    ...
    _sr.popFront();
    ...
    _updateSignalsUnlock();
    return Ok<Hj::SentRecv>(expectedBytes, expectedCaps);
}
```

Line-by-line explanation:

1. `ObjectLockScope scope{*this}`: synchronize access to shared channel state.
2. `try$(_ensureOpen())`: refuse operations on a closed channel.
3. `send`: verifies space, appends bytes/caps into internal buffers, pushes a “message size record”, updates signals, and returns success.
4. `recv`: checks whether messages exist; if none, returns would-block; otherwise pops the message and copies bytes/caps out.

Deeper explanation:

IPC isn’t just storage—it’s also wakeup semantics. The “signal update” step is what lets blocked receivers become runnable when a sender makes progress. That’s why this milestone is typically tested with a producer/consumer pair.

### Verification

- Print `STAGE 6: done`.
- Confirm producer/consumer wakes correctly and no message corruption occurs.

## Summary

Milestone 16 is a roadmap with a repeated testing pattern:
- add one boundary,
- immediately test it under QEMU with serial markers,
- and use the last printed stage marker to locate failures.

