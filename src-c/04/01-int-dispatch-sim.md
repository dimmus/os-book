# 01 - Interrupt/exception dispatch decision logic (host lab)

## Learning goal
An OS interrupt path has a “policy” step:

- faults (exceptions) are classified and typically crash/stop or show diagnostics
- a dedicated scheduling interrupt requests a context switch
- hardware interrupts are mapped to an IRQ number and dispatched

On x86_64 Skift, that policy is expressed in `_intDispatch` inside:

- [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)

This host lab mirrors the policy logic with a simplified `Frame` type and prints what would happen.

## Buildable files

- `src/04/01-int-dispatch-sim/main.cpp`
- `src/04/01-int-dispatch-sim/Makefile`

## Expected output

`make run` produced:

```text
FAULT 0: division-by-zero
SCHEDULE tick (via intNo=100)
IRQ dispatch irq=1 (intNo=33)
```

## Mapping to Skift assembly and C++

The real flow is:

1. `ints.s` constructs the stack/register frame and calls `_intDispatch`.
   - [`skift_sources/skift/src/kernel/hjert/x86_64/ints.s`](skift_sources/skift/src/kernel/hjert/x86_64/ints.s)
2. `arch.cpp::_intDispatch` interprets `frame.intNo` and chooses the policy.
   - [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)

In teaching terms:

- assembly is “stack shape creation”
- C++ dispatch is “semantic policy”

