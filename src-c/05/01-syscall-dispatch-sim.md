# 01 - Syscall dispatch switch (host lab)

## Learning goal
The kernel does two things around syscalls:

1. It uses architecture-specific entry code to route into a kernel dispatcher.
2. It uses a *semantic switch* (`dispatchSyscall`) to implement syscall meaning.

This host lab simulates only step (2): the C++ switch-based dispatcher.

## Buildable files

- `src/05/01-syscall-dispatch-sim/main.cpp`
- `src/05/01-syscall-dispatch-sim/Makefile`

## Expected output

Running `make run` produced:

```text
ret=NOW(10)
10
LOG(10,20)
```

## Mapping to Skift

The real control flow is:

- syscall stub: [`skift_sources/skift/src/kernel/hjert/x86_64/sys.s`](skift_sources/skift/src/kernel/hjert/x86_64/sys.s)
- arch dispatcher: [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)
  - `extern "C" usize _sysDispatch(usize rsp)` -> `Core::doSyscall(...)`
- core syscall dispatcher:
  - [`skift_sources/skift/src/kernel/hjert/core/syscalls.cpp`](skift_sources/skift/src/kernel/hjert/core/syscalls.cpp)
  - `dispatchSyscall(...)` switch implements the semantics for each syscall ID

