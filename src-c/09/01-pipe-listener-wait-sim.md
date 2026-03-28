# 01 - Pipe readable/writable wakeup simulation (host lab)

## Learning goal
User-space async IPC is built on a wakeup primitive:

- `Listener` watches a capability and produces events when signal bits become set/unset.
- `karm-sys/skift/async.cpp::waitFor(...)` converts those events into coroutine-resolved promises.
- Then async `readAsync/writeAsync` waits for `READABLE`/`WRITABLE` before calling the actual operation.

This host lab simulates only the signal idea:

- a `Pipe` has a bounded byte buffer
- `poll()` returns READABLE when bytes exist, WRITABLE when free space exists
- we “wake” when READABLE becomes true after we write

## Buildable files

- `src/09/01-pipe-listener-wait-sim/main.cpp`
- `src/09/01-pipe-listener-wait-sim/Makefile`

## Expected output

`make run` produced:

```text
wake READABLE at tick=3
wake READABLE at tick=4
read=9,8
```

## Mapping to Skift

- `Pipe` semantics: [`skift_sources/skift/src/kernel/hjert/core/pipe.cpp`](skift_sources/skift/src/kernel/hjert/core/pipe.cpp)
  - `read(...)` wouldBlock when no bytes
  - `write(...)` wouldBlock when buffer is full
  - `_updateSignalsUnlock()` publishes READABLE/WRITABLE
- `Listener` and wakeups: [`skift_sources/skift/src/kernel/hjert/core/listener.cpp`](skift_sources/skift/src/kernel/hjert/core/listener.cpp)
- User async integration: [`skift_sources/skift/src/libs/karm-sys/skift/async.cpp`](skift_sources/skift/src/libs/karm-sys/skift/async.cpp)
  - `waitFor(cap, set, unset)` uses `Listener::listen(...)` and resolves on events

