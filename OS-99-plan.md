# OS-99 — Plan: toward a small user-space test shell (`ls`, `uname`)

This file is a **roadmap**, not an implemented milestone. It answers: **what to build and document** (in order) so that—eventually—you can run a **minimal interactive shell** in **user mode** and execute simple programs like **`ls`** and **`uname`**.

**Scope:** “Small” means: **one architecture** (x86_64), **one QEMU machine** (e.g. `q35`), **no** full POSIX compliance. **“Test shell”** means: read a line, parse a command name, **spawn** a process, wait for exit, print output—enough to prove **kernel ↔ user ↔ filesystem** integration.

**Starting point:** The [`src-os/`](src-os/) tree through [OS-8](OS-8-page-fault-study.md): UEFI handoff, paging, IDT, PIC/PIT, LAPIC, #PF, serial logging. **No** userspace, **no** syscalls, **no** file system yet.

---

## End state (definition of done)

- **User mode** tasks run with **restricted** privilege (RPL 3 segments, user page tables, `syscall`/`sysret` or `int`/IRET path—pick one and document it).
- A **shell** binary (or a kernel-builtin stub that **mimics** a shell for bring-up) **reads** a line from **stdin** (serial or virtio-console) and **executes** `ls` and `uname`.
- **`ls`** lists at least one directory (e.g. `/` or a mounted **ramfs** root) using a **real VFS path** (even if the backend is RAM-only).
- **`uname`** prints **something** (kernel name, version string) via a **syscall** or **read-only** pseudo-file (e.g. `/proc/version` or a dedicated syscall).

Until all pieces exist, you can use **stub** programs (fixed output) to **validate the syscall/exec path** before implementing real directory reading.

---

## Phase A — Kernel foundations (extend `src-os` kernel)

### A1. Syscall entry and calling convention

**Create / describe:**

- **Trap path:** `syscall`/`sysret` **or** `int 0x80`-style + **IRET** (64-bit user return is easier with **`syscall`/`sysret`** once STAR/LSTAR/SFMASK are set).
- **ABI**: syscall number in **`rax`**, arguments in **`rdi, rsi, rdx, r10, r8, r9`** (match Linux x86_64 for sanity) or document a **minimal custom** ABI.
- **MSR** programming (if using `syscall`): `IA32_EFER`, `STAR`, `LSTAR`, `SFMASK`.
- **Kernel C++ dispatcher:** `dispatch_syscall(uint64_t n, ...) → uint64_t` with a **switch** on `n`.

**Docs:** New study chapter (e.g. **OS-9-syscall-study.md**) with **serial markers** (`STAGE …: syscall ok`) proving one **noop** or **write** syscall returns.

### A2. User vs kernel segments and TSS

**Create / describe:**

- **GDT** entries: **user code 32/64**, **user data**, **kernel code/data** (you already have a minimal boot GDT; extend or replace with a **static** table in memory).
- **TSS** + **IST** optional for double-fault; at minimum **one** **RSP0** stack for ring transitions.
- **Trampoline** for returning to user: **IRET** frame or **`sysret`** discipline.

**Docs:** Diagram: ring 0 ↔ ring 3, **CPL** vs **DPL**, **why** `#GP` on bad jumps.

### A3. Per-process or per-task address space

**Create / describe:**

- **Separate page tables** for user (or **one** shared kernel upper half + **per-user** lower PML4 entries—classic pattern).
- **User mappings:** **RX** text, **RW** stack + heap arena, **no** execute on user data if you enable **NX**.
- **Copy-on-write** optional later; **first** bring-up can **map** pages **eagerly**.

**Docs:** How **`CR3`** switch relates to **scheduling** (even if you only have **two** tasks).

### A4. Program loading (ELF)

**Create / describe:**

- **ELF64** parser (minimal): **PT_LOAD** segments, **entry** `RIP`, **BSS** zeroing.
- **Loader** source: **embed** test ELF in initrd, **or** read from **virtio-blk**, **or** **memory-map** a **single** binary blob in the kernel image for the first milestone.

**Docs:** “First user program” = **static** PIE or fixed load address, **no** dynamic linker.

---

## Phase B — Processes and scheduling (minimal)

### B1. Thread / process skeleton

**Create / describe:**

- **Struct** `Task`/`Process`: **CR3**, **kernel stack**, **user stack**, **saved GPRs**, **state** (running/runnable/blocked).
- **Context switch:** save **callee-saved** regs + **RIP** (or **switch** page tables + **jump** to saved context). Tie to **timer IRQ** (LAPIC) from [OS-7](OS-7-lapic-study.md) for **preemptive** choice, or stay **cooperative** at first (`yield` syscall).

**Docs:** Relationship to **OS-6/7** IRQ delivery and **EOI**.

### B2. `fork` / `exec` or `spawn`

**Create / describe:**

- **Minimal:** **`spawn(path, argv)`** syscall that **loads** ELF into a **new** address space and **jumps** to user entry (no full fork).
- **Later:** **fork** + **exec** for POSIX-like behavior.

**Docs:** Resource ownership (who frees user pages on exit).

---

## Phase C — Filesystem and VFS

### C1. VFS layer (interface)

**Create / describe:**

- **Inodes** or **handles**; operations: **`open`, `read`, `write`, `close`, `readdir`, `stat`** (subset).
- **Path** walk: `/foo/bar` → **mount** + **node**.

**Docs:** One diagram: **VFS → concrete FS** (ramfs, ext2, fat).

### C2. A concrete backend (pick one first)

**Options (increasing realism):**

| Backend | Pros | Cons |
| --- | --- | --- |
| **Ramfs** (built-in tree in memory) | **Fastest** to `ls` | No persistence; build **populates** tree at boot |
| **Initrd** (cpio or raw blob) | **Realistic** “boot filesystem” | Need **loader** + **parser** |
| **Virtio-9P** or **virtio-blk** + **ext2** | **Host files** visible | **Drivers** + **FS** code |

**Recommendation:** **Ramfs** + **static** `ls`/`uname` **inodes** first; then **initrd** with a **toolchain** that builds user binaries.

**Create / describe:**

- **Mount** root at `/`.
- **Directory entries** for `ls` to read (`readdir`).

### C3. `ls` and `uname` as user programs

**Create / describe:**

- **Build** as **freestanding** ELF + **libc** (see Phase D) **or** **no libc** with **only** syscalls.
- **`ls`:** call **`getdents()`**-style syscall or **`readdir`** abstraction; **format** output (simple columns).
- **`uname`:** syscall **`uname()`** or **read** `/proc/uname` **or** **open** `sysfs` node—pick one and **document**.

---

## Phase D — Userspace C library (minimal)

**Create / describe:**

- **Syscall wrappers** (`write`, `exit`, `open`, `read`, `close`, …).
- **Startup** (`_start`) that calls **`main`**, **`exit`**.
- **Optional:** **printf** to **fd 1** (serial) for debugging.

**Docs:** Which **errno** values you support (even if **always** 0).

---

## Phase E — Shell

### E1. REPL

**Create / describe:**

- **Read line** (blocking **`read`** on stdin).
- **Parse** `command arg…` (split on spaces; **no** quoting required in v1).
- **Builtin** `cd` optional later (needs **cwd** in kernel or shell).

### E2. `exec` path

**Create / describe:**

- **Resolve** path (`/bin/ls` → inode → ELF).
- **Load** and **run**; **wait** for child (blocking **`waitpid`** syscall or **parent blocks** on **one** child).

**Docs:** **Exit codes** on serial for failures.

---

## Planned study files (`OS-N-…-study.md`)

These names map **every phase above (A–E)** to concrete markdown files in this repo, in **dependency order**. Style matches [OS-1](OS-1-HelloInit-UEFI-boot-and-serial-study.md)–[OS-8](OS-8-page-fault-study.md): **serial markers**, diagrams, incremental proofs. You may **merge** adjacent chapters into one file if you prefer fewer documents; **OS-99** stays the **plan index**.

| Planned file | Short description |
| --- | --- |
| **`OS-9-syscall-study.md`** | **Phase A1:** `syscall`/`sysret` (or `int`+IRET) path; `IA32_EFER` / `STAR` / `LSTAR` / `SFMASK`; syscall ABI; kernel `dispatch_syscall`; serial proof (noop or `write`). |
| **`OS-10-gdt-tss-user-segments-study.md`** | **Phase A2:** Extended **GDT** (user code/data 32/64 + kernel); **TSS** with **RSP0** (and optional IST); ring 0 ↔ 3 diagram; CPL/DPL and safe return to user (`sysret` / IRET discipline). |
| **`OS-11-user-address-spaces-study.md`** | **Phase A3:** Per-task or per-process **page tables**; user **RX** text, **RW** stack/heap; **NX** if enabled; how **CR3** switch ties to scheduling (even with two tasks). |
| **`OS-12-elf-loader-study.md`** | **Phase A4:** Minimal **ELF64** (**PT_LOAD**, entry **RIP**, **BSS**); loader from embedded blob, initrd, or virtio-blk; first static user program; document no dynamic linker yet. |
| **`OS-13-processes-scheduling-study.md`** | **Phase B1:** `Task`/`Process` (CR3, kernel/user stack, saved GPRs, state); **context switch**; tie to LAPIC timer ([OS-7](OS-7-lapic-study.md)) for preempt or cooperative **`yield`**; EOI relationship. |
| **`OS-14-spawn-exec-study.md`** | **Phase B2:** **`spawn(path, argv)`** (or minimal **exec**) loading ELF into a new address space; resource ownership and freeing user pages on exit; optional later: full **fork**/**exec**. |
| **`OS-15-vfs-ramfs-study.md`** | **Phases C1–C2:** VFS ops (**open/read/write/close/readdir/stat**), path walk, mount at **`/`**; **ramfs** (or chosen first backend) with directory entries for **`readdir`**. |
| **`OS-16-minimal-libc-study.md`** | **Phase D:** Syscall wrappers; **`_start` → main → exit**; optional **printf** to fd 1; documented **errno** subset. |
| **`OS-17-ls-uname-programs-study.md`** | **Phase C3:** User **`ls`** and **`uname`** as ELFs (with or without libc); **`getdents`**/**readdir** path; **`uname`** via syscall or pseudo-file (**`/proc/...`**); static linking only. |
| **`OS-18-test-shell-study.md`** | **Phase E:** REPL (read line, split args); resolve path, load ELF, **wait** for child; builtins optional; exit codes on serial for failures. |

### Coarse checkpoints (optional grouping)

If you want **fewer** numbered milestones in conversation, these **roll-ups** match the earlier four-step sketch:

| Checkpoint | Rolls up |
| --- | --- |
| **Bring-up syscalls** | OS-9 |
| **User mode + loader + tasks + spawn** | OS-10–OS-14 |
| **VFS + ramfs + libc + user tools** | OS-15–OS-17 |
| **Interactive shell** | OS-18 |

Renumber file names only if you insert or merge steps; **OS-99** stays the **plan index**.

---

## What to skip (until later)

- **Networking**, **GUI**, **audio** — not needed for `ls`/`uname`.
- **Full** **ext4**, **ACLs**, **signals** — use **minimal** subset.
- **Multi-user** security — **single** user is enough for **first** shell.
- **Dynamic linking** (`ld.so`) — **static** binaries only at first.

---

## Verification checklist (incremental)

1. **Syscall** returns a value visible on **serial** (kernel + user).
2. **User mode** fault (**#PF**, **#GP**) is **handled** without **triple fault** (dedicated **IDT** or **IST**).
3. **One** ELF runs from **mapped** pages, **exits** cleanly.
4. **open("/…")** succeeds on a **ramfs** file.
5. **`readdir`** returns **≥2** names (including **`.`** / **`..`** if you implement them).
6. **Shell** prints **`ls`** output and **`uname`** output.

---

## References inside this book

- **FAT disk image, `BOOTX64.EFI`, QEMU disk layout:** [OS-0](OS-0-Introduction.md).
- **Boot / paging / IDT / IRQ / LAPIC / #PF:** [OS-1](OS-1-HelloInit-UEFI-boot-and-serial-study.md)–[OS-8](OS-8-page-fault-study.md).
- **Skift-style dispatch** (conceptual): [C-4](C-4-Interrupts-exceptions-and-frames.md), [C-5](C-5-Syscalls-trampoline-and-dispatch.md) (course chapters).
- **Skift sources** (optional): [`src-os-skift/skift/`](src-os-skift/skift/) — not required to mirror, but useful for **naming** and **structure**.

---

## Summary

To reach a **small user-space test shell** with **`ls`** and **`uname`**, you mainly need: **syscalls + user mode**, **ELF loading**, **a VFS** with at least **ramfs** + **directory reading**, **two user programs**, and a **shell** that **exec**s them. **Document each layer** in the planned **`OS-9-syscall-study.md` … `OS-18-test-shell-study.md`** files (section **Planned study files** above) with **serial proofs**—same teaching style as OS-1–OS-8—so failures stay **localized** and **debuggable**.
