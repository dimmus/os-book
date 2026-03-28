# C-3 — Freestanding Entry and Early Logging

## Learning goals
This chapter connects three things:

1. The earliest point where “real C++ code” can run (kernel entry).
2. How an OS handles unrecoverable states before a scheduler/userspace exists.
3. Why early logging must use a *minimal* output path that works during init and panic.

We will anchor the discussion in the Skift x86_64 code:

- `src/kernel/hjert/x86_64/entry.cpp`: the C++ entrypoint and panic handler registration.
- `src/kernel/hjert/x86_64/arch.cpp`: the concrete `stop()` implementation and the `globalOut()` writer.

## Where kernel C++ entry begins

On x86_64, Skift’s kernel platform layer provides a C++ entry function:

- `Hjert.Core::entryPoint(u64 magic, Handover::Payload& payload)` in [`skift_sources/skift/src/kernel/hjert/x86_64/entry.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/entry.cpp)

That entrypoint is responsible for:

- registering a panic handler (`Karm::registerPanicHandler(__panicHandler)`)
- delegating to microkernel core init (`Hjert::Core::init(magic, payload)`)

The subtle OS teaching point is: a panic handler is not just “error handling”. In a freestanding kernel it is part of the *control-flow contract*.

## What “early logging” means in this codebase

Early logging in Hjert is not “a logging library writing to a file”.

Instead, `globalOut()` returns a very small writer connected to the architecture:

- `Io::TextWriter& globalOut()` in [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)

In your later chapters you will see it used inside:

- the panic handler (fatal vs debug kinds)
- syscall paths
- interrupts (when faults occur)

So “logging” is really “debug I/O plumbing” that must work at the earliest boot stage.

## Host lab: panic handler shape (testable)

Skift’s panic handler calls `Hjert::Arch::stop()` which loops forever on x86_64.

For a host buildable lab we cannot loop forever, so we use exceptions to simulate:

- “print fatal/debug”
- “stop the world” (via a controlled termination that the host can catch)

Lab:

- [`src/03/01-panic-handler.md`](src/03/01-panic-handler.md)

## Mapping checklist (what to find in Skift when you read next)

When you open the referenced files, try to locate these exact concepts:

- `__panicHandler` is registered before `Hjert::Core::init`.
- fatal panic prints `PANIC: ...` and then halts via `stop()`.
- `stop()` is an infinite `cli; hlt` loop.

Those correspond exactly to the lab’s structure.

