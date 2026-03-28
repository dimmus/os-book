# C-5 — Syscalls Trampoline and Dispatch

## Learning goals
In this chapter we focus on the syscall “plumbing” layer:

1. The CPU uses a dedicated entry stub to switch into kernel mode.
2. The stub packages arguments into a known layout and calls an architecture dispatcher (`_sysDispatch`).
3. The kernel core calls a C++ dispatcher (`doSyscall` -> `dispatchSyscall`) that implements capability/object semantics.

Even if later syscalls become more complex, the microarchitecture pattern stays the same:

- entry stub -> frame -> dispatcher -> semantic switch

## Skift x86_64 mapping

Core files to read:

- [`skift_sources/skift/src/kernel/hjert/x86_64/sys.s`](skift_sources/skift/src/kernel/hjert/x86_64/sys.s)
  - prepares syscall stack state and calls `_sysDispatch`
- [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)
  - implements `extern "C" usize _sysDispatch(usize rsp)` which calls `Core::doSyscall(...)`
- [`skift_sources/skift/src/kernel/hjert/core/syscalls.cpp`](skift_sources/skift/src/kernel/hjert/core/syscalls.cpp)
  - implements `dispatchSyscall(...)` as a switch over syscall IDs

## Host lab: syscall dispatcher switch simulator

The host lab mirrors the *shape* of the kernel switch:

- define a small syscall enum
- define argument passing as a struct
- implement `dispatchSyscall` as a switch

Lab:

- [`src/05/01-syscall-dispatch-sim.md`](src/05/01-syscall-dispatch-sim.md)

