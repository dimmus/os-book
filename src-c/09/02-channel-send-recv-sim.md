# 02 - Channel send/recv capacity checks (host lab)

## Learning goal
`Hjert.Core:Channel` models message IPC where each send includes:

- a byte payload
- a slice of capabilities attached to the message
- a “message record” (queue entry) so recv knows how many bytes/caps to pop

The kernel implementation enforces capacity checks before mutating:

- `_sr` (message queue) rem/len
- `_bytes` (byte buffer) rem
- `_caps` (cap buffer) rem

This host lab simulates those capacity checks (without ring buffer details).

## Buildable files

- `src/09/02-channel-send-recv-sim/main.cpp`
- `src/09/02-channel-send-recv-sim/Makefile`

## Expected output

`make run` produced:

```text
send=true recv=true
```

## Mapping to Skift

Core file:

- [`skift_sources/skift/src/kernel/hjert/core/channel.cpp`](skift_sources/skift/src/kernel/hjert/core/channel.cpp)

Look at:

- `send(...)` capacity checks for `_sr.rem()`, `_bytes.rem()`, `_caps.rem()`
- `recv(...)` checks `_sr.len()`, verifies buffers are large enough, then pops message and payload

