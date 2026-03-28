# C-12 — User Runtime: `karm-sys` Entry and Async Integration

## Learning goals
This chapter is where the booted init program becomes an actual user process:

1. Understand the user-space entry stub responsibilities (SysV ABI init, hook wiring, IPC client connection).
2. Understand how async waiting maps user coroutines onto kernel listener events.
3. Recognize how user syscalls become kernel syscalls (via the syscall boundary you built earlier in chapter 05).

## User entry stub in Skift
File:

- [`skift_sources/skift/src/libs/karm-sys/skift/entry.cpp`](skift_sources/skift/src/libs/karm-sys/skift/entry.cpp)

What it does (high level):

- calls `Abi::SysV::init()`
- registers a panic handler (`__panicHandler`)
- creates a `Sys::Context ctx`
- wires:
  - `Sys::ArgsHook` (argv array)
  - `HandoverHook` (gives the app access to the handover payload mapping)
- creates an IPC connection:
  - wraps raw handles into `DuplexFd`
  - notifies the Skift service/protocol with the connection details
- runs `Sys::run(entryPointAsync(ctx, cancellation.token()))`
- if the async entry returns an error, logs and crashes

## User syscalls + exit
File:
- [`skift_sources/skift/src/libs/karm-sys/skift/sys.cpp`](skift_sources/skift/src/libs/karm-sys/skift/sys.cpp)

This is where user-visible syscalls like `sleep`, `exit`, memory mapping, etc. are embedded.

For instance, `exit(i32)` calls `Hj::Task::self().ret()`.

## Async waiting bridge: `waitFor(...)`
File:
- [`skift_sources/skift/src/libs/karm-sys/skift/async.cpp`](skift_sources/skift/src/libs/karm-sys/skift/async.cpp)

`waitFor(cap, set, unset)` is the heart of the async integration:

- it registers interest with a kernel `Listener` (`_listener.listen(...)`)
- stores a promise per-capability
- the async scheduler loop polls listener events and resolves promises when events arrive

Then async IO wrappers use this to avoid busy waiting:

- `readAsync` waits for `READABLE`
- `writeAsync` waits for `WRITABLE`

## Host lab: entry wiring simulation

Lab:

- [`src/12/01-user-entry-hookup-sim.md`](src/12/01-user-entry-hookup-sim.md)

This lab is intentionally simple, but it preserves the *order* of operations that matters in microarchitecture teaching:

- init ABI
- register hooks
- create IPC connection
- run async entry

## Next step
Chapter `13` focuses on the only “real-world test” for the whole stack:

- building/running `hideo-shell` in QEMU and interpreting where failures occur

