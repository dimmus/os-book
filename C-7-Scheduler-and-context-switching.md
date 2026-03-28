# C-7 — Scheduler and Context Switching

## Learning goals
After this chapter students should understand how Skift turns “there are many tasks” into a CPU execution order:

1. Task lifecycle states (`RUNNABLE`, `BLOCKED`, `EXITED`) and the meaning of a time slice.
2. Scheduler selection: how a scheduler chooses the next runnable task based on “current time” and “slice end”.
3. Context switching: how CPU state is captured into a frame, and later restored.
4. Where the “yield” comes from:
   - user code yields by triggering an interrupt (`int $100` on x86_64)
   - the architecture layer converts that into a scheduler call

## Core scheduler code you should read

- `Hjert.Core:sched`
  - [`skift_sources/skift/src/kernel/hjert/core/sched.cpp`](skift_sources/skift/src/kernel/hjert/core/sched.cpp)
  - `Sched::schedule()` chooses the next task.
  - it updates each task’s `_sliceEnd` based on `clockNow()`.

- `Hjert.Core:task`
  - [`skift_sources/skift/src/kernel/hjert/core/task.cpp`](skift_sources/skift/src/kernel/hjert/core/task.cpp)
  - task evaluation uses `_block` and signals.
  - `enter(...)`/`leave()` controls whether task is treated as user/super.

- x86_64 architecture layer (context switching + yield)
  - [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)
  - `yield()` triggers `int $100`
  - `_intDispatch` sees `intNo == 100` and calls `switchTask(...)`
  - `switchTask(...)` saves current task state, advances the clock, schedules, and loads the chosen task state.

## Host labs (mirroring logic, not CPU)

We simulate two key behaviors:

- scheduler selection logic (host-level)
- context save/load semantics (host-level)

Labs:

- [`src/07/01-sched-selection-sim.md`](src/07/01-sched-selection-sim.md)
- [`src/07/02-context-save-load-sim.md`](src/07/02-context-save-load-sim.md)

## Why scheduling belongs in “OS microarchitecture”

At the architecture level, scheduling is a CPU state machine:

- a task is runnable only if it can make progress without blocking
- a context switch must save registers + restore another task’s registers
- timers interrupts (or explicit yields) drive preemption

Skift’s approach keeps the C++ scheduler logic in `Hjert.Core:*` and keeps the “CPU transition” in the architecture layer.

That separation is a teachable microarchitecture pattern:

- core: policy + state transitions
- arch: mechanism + register frames + interrupt/syscall entry wiring

## Next step

Next chapter makes the “policy objects” explicit:

- capability-based object creation (`CREATE`, `MAP`, `DROP`, `PLEDGE`)
- why user tasks can fail with permission errors instead of crashing the kernel

