# 90 - Enhancements Roadmap (student extensions)

This chapter is intentionally open-ended. The point is not to finish everything, but to give students a structured way to extend the system while reusing the microarchitecture concepts from earlier chapters.

## Extension themes (in priority order)

### 1) Extend the syscall surface while preserving the microarchitecture boundary
Add:

- new syscalls in `Hjert.Core:syscalls` and handlers in the same module
- user-side wrappers in `karm-sys/skift/sys.cpp`

Keep the course “guarantee structure”:

- decode capability
- check pledge/rights (`Task::ensure`)
- dispatch object operation

### 2) Improve IPC semantics (timeouts, better would-block behavior)
Build on:

- `Hjert.Core:channel` / `pipe` / `listener`
- `karm-sys/skift/async.cpp::waitFor(...)`

Possible extensions:

- multiple waiter support per `cap` (currently noted as FIXME in `waitFor`)
- richer polling/wakeup behavior for `POLL` and event batching

### 3) Make memory debugging educational
Instrument:

- loader segment loading rules (page-align, copy/zero)
- `initMem` allocator setup (bitmap range selection, marking used/free)

Goal: students should be able to “see” why a mapping is correct.

### 4) Add a small “user-space test shell” mode
Instead of jumping straight into full desktop shell:

- add an init program option that runs a deterministic set of syscall/IPC tests
- print results over the early output path (once user runtime is up)

### 5) Add fallible error context
The codebase already uses `Res<>` and `try$`.

Enhancement direction:

- enrich errors with `Cap`, requested lengths/ranges, and pledge requirements
- show students how to propagate context without losing performance

## Teaching methodology suggestion
Every student extension should end with:

- a new chapter section or a new mini-lab
- a small “expected output” block
- a mapping paragraph back to the original Skift files

