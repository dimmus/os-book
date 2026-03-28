# C-2 — C++20 Building Blocks (from `karm-core`)

## Why this chapter exists
In OS microarchitecture you constantly trade:

- correctness vs performance
- safety vs control
- abstraction vs what the CPU *must* do

Skift’s userland/kernel code is written in modern C++ with a “zero-cost abstraction” mindset, and it leans heavily on:

- C++20 `concept`s + constrained templates
- C++20 coroutines (async tasks and generators)
- lifetime/liveness thinking (expressed via types, attributes, and careful ownership)

Rather than starting from OS theory, we start by reproducing two key “C++ building blocks” on the host:

1. A constrained interface using a `concept` (so template code can refuse ill-formed types early).
2. A minimal coroutine `Task<T>` (so the scheduler mental model becomes concrete).

Then we map these host labs to Skift’s actual architecture:

- The microkernel task type and scheduler loop live in `Hjert.Core:task` and `Hjert.Core:sched`.
- The user-space async waiting hooks live in `karm-sys/skift/async.cpp`.

## Host lab 1: Concepts + `requires`

We build a `Sliceable` concept that checks:

- an `Inner` type exists
- `len()` returns something size-like
- `buf()` returns a pointer to the inner type
- `operator[]` returns a reference to the inner type

This mirrors the style of the examples in [`skift_sources/skift/src/libs/karm-core/README.md`](skift_sources/skift/src/libs/karm-core/README.md).

Lab:

- [`src/02/01-concepts-requires.md`](src/02/01-concepts-requires.md)

## Host lab 2: Coroutine `Task<T>`

Then we implement a tiny coroutine-based `Task<T>` that:

- holds a `promise_type`
- resumes the coroutine when we call `run()`
- returns the computed value via `co_return`

Why it matters for an OS:

- The microkernel scheduler (`Hjert.Core:sched`) does not “know C++ coroutines”.
- Instead, it manages *task state* and CPU frames; blocking/unblocking is expressed by syscall paths + scheduling.
- User space, however, *does* use coroutines for async waiting; that is where the “await” vocabulary matches real scheduling behavior.

Lab:

- [`src/02/02-coroutine-task.md`](src/02/02-coroutine-task.md)

## Mapping: where these ideas show up in the actual Skift code

### Concepts -> “compile-time interface contracts”

Skift uses concepts to express what it expects from generic components (like view/layout systems, iterators, slices, etc.).

Even if you cannot read every module right now, the mapping principle is:

- concepts validate the interface *before runtime*
- OS code can remain generic without becoming dynamically fragile

### Coroutines -> async waiting; microkernel scheduling -> task frames

In Skift, the conceptual boundary looks like this:

- Microkernel scheduler selects a task and loads/saves a CPU frame (`Hjert.Core:task`, `Hjert.Core:sched`, plus the arch layer in `src/kernel/hjert/x86_64/arch.cpp`).
- User space coroutine code awaits readiness and yields control; readiness is powered by kernel primitives and syscalls.

Concrete “user await” wiring you can locate:

- `karm-sys/skift/async.cpp` contains `waitFor(...)` and integrates promises with kernel listener events.

And concrete “scheduler selection” you can locate:

- `Hjert.Core:sched` contains `schedule()` which chooses the next runnable task.

## Control-flow boundary: interrupts/syscalls -> C++ dispatch

This subsection ties the “language building blocks” back to microarchitecture:

1. Assembly entry stubs create a register frame and call architecture-level dispatchers.
2. Architecture dispatchers call into the C++ kernel core.
3. The kernel core implements capability/object/syscall semantics (and task scheduling effects).

For x86_64, the concrete files are:

- [`skift_sources/skift/src/kernel/hjert/x86_64/ints.s`](skift_sources/skift/src/kernel/hjert/x86_64/ints.s): interrupt entry stubs that push an interrupt number and jump to the common handler.
- [`skift_sources/skift/src/kernel/hjert/x86_64/sys.s`](skift_sources/skift/src/kernel/hjert/x86_64/sys.s): syscall entry stub that prepares user return state and calls `_sysDispatch`.
- [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp): provides `extern "C" void _intDispatch(usize rsp)` and `extern "C" usize _sysDispatch(usize rsp)` and forwards to IRQ/syscall dispatch.
- [`skift_sources/skift/src/kernel/hjert/core/syscalls.cpp`](skift_sources/skift/src/kernel/hjert/core/syscalls.cpp): `doSyscall(...)` and the `dispatchSyscall(...)` switch that implements object creation, `MAP/UNMAP`, `SEND/RECV`, etc.

When you teach the course, this is the moment to say:

- “C++ types and coroutines help us structure the logic”
- “but CPU transitions still require frames, explicit dispatcher plumbing, and careful return paths”

## What we will do next

Next, we go “one layer lower” into early runtime responsibilities:

- how a kernel decides what to do on panic vs fatal
- how interrupts/syscalls create CPU frames and dispatch into C++ code

Those low-level steps are where the microarchitecture teaching becomes very tangible.

