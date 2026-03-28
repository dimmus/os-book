# 01 - Res/Opt style error propagation (host lab)

## Learning goal
Before we touch interrupt/syscall machinery, we need a discipline for failure:

- OS operations fail often (missing boot resources, permission checks, mapping errors).
- In kernel code you want the failure to propagate *immediately* to the correct layer.
- In Skift, that shape is expressed with `Res<>` / `Opt<>` plus `try$`-style early-return.

This host lab implements a minimal `Res<T>` and shows a “pipeline” function that:

1. Calls a fallible step.
2. Early-returns the error.
3. Continues on the success path.

## Code
Buildable files:

- `src/01/01-res-opt-try/main.cpp`
- `src/01/01-res-opt-try/Makefile`

## Expected output

Running `make run` inside `src/01/01-res-opt-try/` produced:

```text
3
division undefined for x==0
```

## Where this maps in Skift

### `Res<>` / `Opt<>` in `karm-core`
The `karm-core` documentation explicitly demonstrates the “result with early unwrap/return” shape, and it uses `try$()` in the examples:

- [`skift_sources/skift/src/libs/karm-core/README.md`](skift_sources/skift/src/libs/karm-core/README.md)

### Kernel-side syscalls return `Res<>`
The kernel syscall dispatcher returns `Res<>` and each syscall handler uses `try$` to propagate errors.

Concrete places to look:

- [`skift_sources/skift/src/kernel/hjert/core/syscalls.cpp`](skift_sources/skift/src/kernel/hjert/core/syscalls.cpp) (`doCreate`, `doMap`, `doSend`/`doRecv`, and the `dispatchSyscall` switch)
- [`skift_sources/skift/src/kernel/opstart/entry.cpp`](skift_sources/skift/src/kernel/opstart/entry.cpp) (uses `co_try$` for async parsing/loading)

### Why the pattern is more than “convenient syntax”
In a capability microkernel, “failure” is frequently:

- a permission model decision (invalid pledge/rights)
- a mapping decision (page permissions/size mismatch)
- a readiness decision (a blocked operation waiting for a listener)

So the *control-flow* of errors is part of the OS microarchitecture teaching, not just a coding style.

## Next step
Now that you know the failure-control shape, the next chapter deepens the C++20 language mechanisms that Skift relies on (concept constraints and coroutines).

