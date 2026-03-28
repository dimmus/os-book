# 01 - Panic handler and early logging (host lab)

## Learning goal
In a freestanding kernel, panic handling has two jobs:

- emit the most useful message possible using the *minimal* output path
- switch the system into a “do not continue” control flow (halt/stop/reboot)

Skift’s x86_64 platform code models this explicitly in its C++ panic handler.

## Buildable files

- `src/03/01-panic-handler/main.cpp`
- `src/03/01-panic-handler/Makefile`

## Expected output

Running `make run` produced:

```text
DEBUG: early log ok
PANIC: unreachable
stopped: kernel stop (simulated)
```

## Mapping to Skift

In Skift x86_64, compare:

- panic handler and registration:
  - [`skift_sources/skift/src/kernel/hjert/x86_64/entry.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/entry.cpp)
- the “stop the world” behavior:
  - [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)

In the kernel, fatal panic calls `Hjert::Arch::stop()` which halts forever via CPU instructions.

The host lab uses a controlled exception to keep it runnable under CI-like conditions.

