# C-4 — Interrupts, Exceptions, and Register Frames

## Learning goals
After this chapter, students should be able to:

1. Explain what a “register frame” is (and why you need it to transition between CPU mode contexts).
2. Describe the high-level flow of an interrupt/exception:
   - assembly entry stub -> common handler -> C++ dispatcher (`_intDispatch`) -> fault/IRQ policy.
3. Connect CPU exception classes to scheduler decisions:
   - some interrupts cause a context switch
   - others run IRQ dispatchers
   - some faults should crash/stop the current task or kernel

## Where in Skift this is implemented (x86_64)

Concrete files:

- [`skift_sources/skift/src/kernel/hjert/x86_64/ints.s`](skift_sources/skift/src/kernel/hjert/x86_64/ints.s)
  - builds the stack layout (pushes interrupt number and an error-code slot)
  - funnels every interrupt to `_intCommon` which pushes general registers and calls `_intDispatch`
- [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)
  - defines `struct Frame` and `extern "C" void _intDispatch(usize rsp)`
  - splits behavior into:
    - < 32: CPU faults (panic with backtrace)
    - == 100: “yield” interrupt (scheduler context switch)
    - otherwise: IRQs (dispatch and PIC acknowledge)

## Host lab: interrupt dispatcher simulator

We cannot run `ints.s` on the host, but we can simulate the *decision logic*:

- faults are handled by interrupt number range
- a “special intNo” triggers scheduling
- IRQs map to `irq = intNo - 32`

Lab:

- [`src/04/01-int-dispatch-sim.md`](src/04/01-int-dispatch-sim.md)

