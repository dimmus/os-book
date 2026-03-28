# C-1 — Introduction and QEMU Setup

## What you will build
By the end of this chapter, you should be able to:

1. Run the existing Skift OS project in QEMU (x86_64) and see it reach the first user-space program.
2. Explain (from code) the high-level control flow: firmware -> bootloader (opstart) -> microkernel (Hjert) -> user runtime (karm-sys) -> services/UI.
3. Understand why *error propagation* (`Res`/`Opt`-style) matters even before the OS has a “real” file system, heap discipline, or debugger.

The key point for this course: we are not describing an OS abstractly. We are mapping *real source files* into a mental model, then building small C++ programs that mimic those mechanisms.

## Quick map of the Skift boot path (what the code actually does)

Skift’s early boot entrypoint is `opstart`, which is responsible for:

- Parsing configuration (`loader.json`).
- Optionally drawing a splash/menu UI.
- Loading a kernel ELF image and creating a handover payload.
- Setting up mappings (VMM) for the kernel and the initial handover region.

The most important functions for this mapping are:

- `opstart/entry.cpp`: parses `file:/loader.json` and chooses between splash/menu vs direct launch.
- `opstart/loader.cpp`: loads the kernel ELF via `Vaerk::Elf::Image`, maps segments, builds the “handover” payload, maps stack, and then transfers control with `Fw::enterKernel`.
- `Hjert` platform + core init: x86_64 platform layer sets up IDT/PIC/PIT and syscall/interrupt trampolines, then `Hjert::Core::init` locates `file:/skift/init.bootfs` and enters userspace.

Even if you do not read all of these files right now, you should know the *names* because later chapters will expand them.

## QEMU prerequisites and the exact runner used by this repo

Skift’s build/run automation is driven by `skift.sh` and “machine plugins” under `skift/meta/plugins/start/`.

For QEMU specifically, the command line is constructed by:

- [`skift_sources/skift/meta/plugins/start/runner.py`](skift_sources/skift/meta/plugins/start/runner.py)

For x86_64 QEMU it uses:

- `qemu-system-x86_64`
- `-machine q35`
- `-bios OVMF.fd` (tries `/usr/share/edk2/x64/OVMF.fd`, otherwise downloads an edk2 nightly OVMF)
- 512M memory and 4 vCPUs
- A raw disk image created by the build pipeline: `-drive file=fat:rw:{image.finalize()},media=disk,format=raw`

If SDL is available it adds `-display sdl`. If KVM/hvf is available it picks `-enable-kvm` or `-accel hvf`, otherwise falls back to `-accel tcg`.

## Build/run in this repo (recommended starting command)

There is a high-level “developer guide” for building and running under:

- [`skift_sources/skift/doc/building.md`](skift_sources/skift/doc/building.md)

From inside `skift_sources/skift/`, the important commands are:

```sh
./skift.sh tools setup
./skift.sh tools doctor
./skift.sh install
./skift.sh run --release hideo-shell
```

Notes for students:

- `tools setup` installs the external toolchain components and Python dependencies via the repo’s `cutekit`.
- The repo’s own build scripts expect a *working host environment* (clang/nasm/qemu/etc.). We keep that outside the OS itself; the OS tutorial focuses on kernel architecture.
- The tutorial is designed so that each chapter can be implemented/validated incrementally (host labs first, then kernel integration).

## Micro-lab: why `Res`-style error propagation shows up everywhere

Even though this is a “QEMU setup” chapter, you should already recognize a recurring OS pattern:

- OS code cannot assume everything succeeds.
- When failure occurs, the failure must propagate *without losing context*.
- In a microkernel, failed operations often happen due to capability permission checks, mapping errors, or missing boot resources.

Skift’s kernel code uses `Res<>` and `Opt<>` heavily (and wraps them in `try$`-style early-return macros).

In later chapters we will connect this to:

- capability checks in syscall dispatch (kernel side)
- user-space IPC waiting (user side)

For this chapter’s lab, we will implement a minimal “result type” on the host to reinforce the *shape* of the pattern before we go back to boot code.

See the host lab:

- [`src/01/01-res-opt-try.md`](src/01/01-res-opt-try.md)

## How the boot flow “feels” (mental model you should keep)

Try to hold onto this sequence:

1. `opstart` turns a configuration (`loader.json`) into “load these ELF binaries and map them”.
2. `Hjert::Core::init` turns the handover payload + bootfs into “create an address space, map init ELF, create a task, enqueue it”.
3. `Hjert`’s scheduler picks tasks based on time slices and whether they are blocked/runnable.
4. Syscalls and interrupts use architecture-specific trampolines and then dispatch into a capability/object model.

That mental model is what the rest of the book will keep refining.

