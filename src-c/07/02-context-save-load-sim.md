# 02 - Context save/load simulation (host lab)

## Learning goal
In Skift, a task switch does not just change an instruction pointer.

Conceptually it must:

1. Save the CPU state of the currently running task into a per-task context object.
2. Restore the CPU state of the next task from that stored context.

On x86_64 Skift, this is represented by:

- `Frame` (the register frame abstraction)
- `Context::save(frame)` and `Context::load(frame)` (arch layer)

This host lab mirrors that idea with a tiny `Frame`/`Context` pair.

## Buildable files

- `src/07/02-context-save-load-sim/main.cpp`
- `src/07/02-context-save-load-sim/Makefile`

## Expected output

`make run` produced:

```text
rip=1111 rsp=2222
```

## Mapping to Skift

- `Frame` and context save/load exist in:
  - [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp) (`Context::save` / `Context::load`)
- the scheduler switch path is orchestrated by:
  - [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp) (`switchTask`), called when `_intDispatch` sees `intNo == 100`

