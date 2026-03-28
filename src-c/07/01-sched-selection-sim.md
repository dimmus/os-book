# 01 - Scheduler selection simulation (host lab)

## Learning goal
Skift’s `Sched::schedule()` implements a simple but instructive policy:

- time slices: each task has a `_sliceEnd`
- the scheduler chooses among tasks that are `RUNNABLE`
- `EXITED` tasks are removed
- an `idle` task is always scheduled last by forcing its slice end to `now+1`

In this host lab we reproduce only the selection logic:

- pick the `RUNNABLE` task with the earliest (<=) `sliceEnd`
- tasks in `BLOCKED/EXITED` do not participate

## Buildable files

- `src/07/01-sched-selection-sim/main.cpp`
- `src/07/01-sched-selection-sim/Makefile`

## Expected output

`make run` produced:

```text
now=100 pick=A sliceEnd=99
```

## Mapping to Skift

The selection corresponds to:

- [`skift_sources/skift/src/kernel/hjert/core/sched.cpp`](skift_sources/skift/src/kernel/hjert/core/sched.cpp)
  - `auto now = clockNow();`
  - `_curr->_sliceEnd = now;`
  - idle `_idle->_sliceEnd = now + 1;`
  - for each task: `t->eval(now)` and selection of `State::RUNNABLE` with `_sliceEnd <= next->_sliceEnd`

