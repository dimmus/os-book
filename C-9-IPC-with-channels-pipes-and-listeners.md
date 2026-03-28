# C-9 — IPC with Channels, Pipes, and Listeners

## Learning goals
After this chapter students should be able to explain:

1. The difference between message-based IPC (`Channel` with SEND/RECV) and byte-stream IPC (`Pipe` with WRITE/READ).
2. Why a microkernel needs a `Listener` abstraction for blocking/wakeup without busy waiting.
3. How user-space async waiting integrates with kernel listener events.

## Kernel pieces to read

### Message IPC: `Channel`
- [`skift_sources/skift/src/kernel/hjert/core/channel.cpp`](skift_sources/skift/src/kernel/hjert/core/channel.cpp)

Important behaviors:

- `send(...)` checks ring capacities for:
  - message queue space (`_sr.rem()`)
  - byte space (`_bytes.rem()`)
  - caps space (`_caps.rem()`)
- `recv(...)` checks:
  - message queue non-empty (`_sr.len() > 0`)
  - destination buffer sizes large enough (`bytes.len() >= expectedBytes`, `caps.len() >= expectedCaps`)
- send/recv update signal bits via `_updateSignalsUnlock()`, which sets:
  - `READABLE` when message bytes exist
  - `WRITABLE` when buffer space exists

### Byte IPC: `Pipe`
- [`skift_sources/skift/src/kernel/hjert/core/pipe.cpp`](skift_sources/skift/src/kernel/hjert/core/pipe.cpp)

Important behaviors:

- `write(...)` returns `wouldBlock` when `_bytes.rem() < 1`
- `read(...)` returns `wouldBlock` when `_bytes.len() == 0`
- `_updateSignalsUnlock()` sets READABLE/WRITABLE based on buffer fill level

### Wakeups and blocking: `Listener`
- [`skift_sources/skift/src/kernel/hjert/core/listener.cpp`](skift_sources/skift/src/kernel/hjert/core/listener.cpp)

`Listener` tracks watched caps plus which signal bits to treat as:

- set-condition events (`set`)
- unset-condition events (`unset`)

`pollEvents()` computes events by:

- asking the watched object `obj->poll()`
- emitting events when `(sigs & set)` is true or when unset transitions occur

## User-space async bridge: `karm-sys`
- [`skift_sources/skift/src/libs/karm-sys/skift/async.cpp`](skift_sources/skift/src/libs/karm-sys/skift/async.cpp)

The critical function is:

- `waitFor(cap, set, unset)`

It:

- calls `Listener::listen(...)` to register interest
- stores a promise per-caphandle
- resolves that promise when `Listener::next()` produces an event with the watched cap

Then higher-level `readAsync` / `writeAsync` does:

- `co_trya$(waitFor(... WRITABLE ...))`
- then calls the kernel-side operation (`chan.write(...)` / `chan.read(...)`)

## Host labs (simulations)

We simulate:

1. Pipe + listener wakeup behavior (readable/writable signals).
2. Channel “message record” capacity checks (message queue + payload buffers).

Labs:

- [`src/09/01-pipe-listener-wait-sim.md`](src/09/01-pipe-listener-wait-sim.md)
- [`src/09/02-channel-send-recv-sim.md`](src/09/02-channel-send-recv-sim.md)

## Next step

Next chapter (`10`) moves back to boot plumbing:

- `opstart/entry.cpp` selects which kernel entry to launch
- `opstart/loader.cpp` maps kernel ELF segments and prepares the handover records

