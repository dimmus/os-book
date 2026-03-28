# C-11 — Hjert init: Load init from bootfs and Enter Userspace

## Learning goals
Students now have enough to understand the “kernel takes over fully” moment:

1. `Hjert.Core:init` validates the handover payload and initializes memory + scheduler.
2. It then loads an init program from bootfs (`init.bootfs`) into a freshly created user address space.
3. It creates the initial user `Task`, sets up the initial stack and passes the handover base to it.
4. Finally it enters idle loop if the init task immediately blocks/exits.

## Core code to read

- [`skift_sources/skift/src/kernel/hjert/core/init.cpp`](skift_sources/skift/src/kernel/hjert/core/init.cpp)
  - `_locateInit(...)`: find init ELF inside bootfs
  - `enterUserspace(...)`: map user programs, map handover + stack, create domain+task, enqueue
  - `init(...)`: call `Arch::init`, validate handover, call `initMem`, call `initSched`, enable interrupts, enterUserspace, then idle loop

- [`skift_sources/skift/src/kernel/hjert/core/mem.cpp`](skift_sources/skift/src/kernel/hjert/core/mem.cpp)
  - `initMem(payload)` (allocator + global VMM mapping)

## Host lab: init mapping flow (simulated)

We simulate the calculations and “shape” of `enterUserspace(...)`:

- locate init ELF range inside a bootfs record
- align its size to pages
- pretend to map 2 program segments (one exec-like, one writable/data-like)
- compute a stack size (64KiB) and show task creation parameters

Lab:

- [`src/11/01-init-userspace-mapping-sim.md`](src/11/01-init-userspace-mapping-sim.md)

## Where the contract boundary is

The init program needs access to boot resources and shared data.

In Skift, the contract boundary is explicit:

- the loader puts kernel resources into `Handover::Payload`
- kernel init maps:
  - the init ELF into the new `Space`
  - the handover payload region into the user `Space` as a readable mapping
  - the user stack mapping into the user `Space`
- kernel then calls `task->ready(entry, stackEnd, {handoverRange.start})`

This is why teaching “handover mapping” (chapter 06) and teaching “enterUserspace” (this chapter) should be tightly coupled.

## Next step

Chapter `12` switches to user-space runtime:

- `karm-sys/skift/entry.cpp` is the user entry stub (SysV init + hooks + IPC connection)
- `karm-sys/skift/async.cpp` is where `waitFor(...)` turns listener events into coroutine completion

