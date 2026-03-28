# 02 - Coroutine `Task<T>` (host lab)

## Learning goal
This lab is intentionally small: it implements a minimal coroutine-based `Task<T>` so students can feel:

- what `promise_type` stores
- how `co_return` delivers a value
- how “resuming” a coroutine maps to “running” a task in a scheduler world

The host `Task<T>` is not Skift’s task type. It is a teaching proxy for the *control flow* of coroutines.

## Code

Buildable files:

- `src/02/02-coroutine-task/main.cpp`
- `src/02/02-coroutine-task/Makefile`

## Expected output

`make run` produced:

```text
42
```

## Mapping to Skift / microkernel scheduling

### Where scheduling actually happens
Skift’s microkernel scheduler lives here:

- [`skift_sources/skift/src/kernel/hjert/core/sched.cpp`](skift_sources/skift/src/kernel/hjert/core/sched.cpp)

It chooses the next runnable task based on time slices and whether tasks are blocked/exited.

### Where CPU state is saved/restored
Skift saves/restores CPU frames per task:

- [`skift_sources/skift/src/kernel/hjert/core/task.cpp`](skift_sources/skift/src/kernel/hjert/core/task.cpp)

And on x86_64, the architecture layer provides the interrupt/syscall wiring and context loading:

- [`skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp`](skift_sources/skift/src/kernel/hjert/x86_64/arch.cpp)

### Where coroutines show up in user-land
User-space async waiting uses coroutine-style operations. You can see the integration points in:

- [`skift_sources/skift/src/libs/karm-sys/skift/async.cpp`](skift_sources/skift/src/libs/karm-sys/skift/async.cpp)

That code builds “awaitable” behavior using kernel listener caps and resolves promises when events arrive.

## Next step
Now we can connect C++ coroutine control-flow to microkernel primitives, and then go lower to interrupts/syscalls where CPU frames are born.

