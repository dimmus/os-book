# 01 - User entry wiring simulation (host lab)

## Learning goal
`karm-sys/skift/entry.cpp` is the user-space entry stub that wires:

- ABI runtime initialization (`Abi::SysV::init()`)
- panic handler registration
- `Sys::Context` hooks:
  - `Sys::ArgsHook` (argv)
  - `HandoverHook` (boot payload mapping)
- IPC connection to services
- then executes the async user entrypoint and returns/terminates

This host lab simulates the ordering and the dependency structure: the “async entry” succeeds only if hooks were added.

## Buildable files

- `src/12/01-user-entry-hookup-sim/main.cpp`
- `src/12/01-user-entry-hookup-sim/Makefile`

## Expected output

`make run` produced:

```text
Abi::SysV::init()
ipc.in=11 ipc.out=22
run=ok
```

## Mapping to Skift

- user entry stub:
  - [`skift_sources/skift/src/libs/karm-sys/skift/entry.cpp`](skift_sources/skift/src/libs/karm-sys/skift/entry.cpp)
- async waiting integration:
  - [`skift_sources/skift/src/libs/karm-sys/skift/async.cpp`](skift_sources/skift/src/libs/karm-sys/skift/async.cpp)
- user syscall embedding:
  - [`skift_sources/skift/src/libs/karm-sys/skift/sys.cpp`](skift_sources/skift/src/libs/karm-sys/skift/sys.cpp)

