# C-8 — Capability Objects, Space/Domain, and Pledges

## Learning goals
In this chapter we make the “policy objects” explicit:

1. How the kernel represents *objects* and references them using capabilities (`Cap`).
2. How those capabilities live in a `Domain` (a capability namespace with slot storage).
3. How user code requests actions through syscalls, and the kernel checks a task’s allowed rights via *pledges* (`Pledge` flags).
4. How memory mappings are mediated by `Space` (virtual address space) and its `map/unmap` logic.

## Where this exists in Skift (code pointers)

### Capability namespaces: `Domain`
File:
- [`skift_sources/skift/src/kernel/hjert/core/domain.cpp`](skift_sources/skift/src/kernel/hjert/core/domain.cpp)

`Domain` maintains an array of optional object slots (`Opt<Arc<Object>> _slots`) and implements:

- `add(destCap, obj)` / `_addUnlock` to place an object into a free slot and return the corresponding `Cap`
- `get(cap)` to retrieve a stored object
- `drop(cap)` to clear a slot

### Virtual memory: `Space`
File:
- [`skift_sources/skift/src/kernel/hjert/core/space.cpp`](skift_sources/skift/src/kernel/hjert/core/space.cpp)

`Space` owns:

- a VMM handle (`Arc<Hal::Vmm> _vmm`)
- a set of currently available address ranges (`_ranges`)
- a list of `Map` entries (`_maps`) which hold `vrange/off/vmo`

`Space::map(...)` enforces:

- page alignment (`vrange.ensureAligned(Hal::PAGE_SIZE)`)
- mapping size rules (if `vrange.size == 0`, use `vmo` range size)
- bounds checks (`end > vmo->range().size` => error)
- overlap prevention (`_ensureNotMapped`)
- VMM wiring via `_vmm->mapRange(...)` and `_vmm->flush(...)`

### Rights gate: Task pledges + syscall ensures
File:
- [`skift_sources/skift/src/kernel/hjert/core/task.cpp`](skift_sources/skift/src/kernel/hjert/core/task.cpp)

`Task` stores:

- `_pledges` (flags)
- `ensure(pledge)` checks permission before continuing
- `pledge(...)` updates the allowed rights (via syscall)

Kernel syscall dispatch checks pledges inside handlers:

- [`skift_sources/skift/src/kernel/hjert/core/syscalls.cpp`](skift_sources/skift/src/kernel/hjert/core/syscalls.cpp)

For example, `doCreate` enforces pledges depending on which object type is being created (task/mem/hw, etc.).

## Host labs

We simulate two micro-systems:

- capability slot encoding + retrieving objects
- pledge permission checks (allow/deny)
- (separately) page-aligned range mapping + overlap rejection

Labs:

- [`src/08/01-cap-pledge-sim.md`](src/08/01-cap-pledge-sim.md)
- [`src/08/02-space-map-overlap-sim.md`](src/08/02-space-map-overlap-sim.md)

## Why pledges + capabilities are “microarchitecture”

From a student’s point of view, this chapter is where the OS stops being “functions and structs” and becomes “guarantees”:

- Without capabilities, the syscall boundary would need ad-hoc global pointers (unsafe).
- Without pledges, the syscall boundary would need a dynamic policy engine (slow and sprawling).
- With both, the kernel’s hot path remains:
  - decode `Cap`
  - check required `Pledge`
  - dispatch to object-specific logic

That is a microarchitectural design: you choose where to spend CPU cycles and where to spend compile-time structure.

## Next step

Next chapter adds IPC:

- Channels for `SEND/RECV`
- Pipes for `WRITE/READ`
- Listeners and polling for wakeup signals

Those build on capabilities + pledges directly.

