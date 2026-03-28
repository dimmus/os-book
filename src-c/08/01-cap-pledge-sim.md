# 01 - Capability + pledge permission simulation (host lab)

## Learning goal
Skift’s syscall layer is “safe by construction” because it uses:

- capabilities to name kernel objects (`Cap`)
- pledges to gate which operations a task is allowed to perform (`Pledge` flags)

This host lab simulates:

- storing objects inside a domain with fixed slots
- retrieving an object via a `Cap`-like index
- checking whether a task’s pledge bitset contains the required pledge bit(s)

## Buildable files

- `src/08/01-cap-pledge-sim/main.cpp`
- `src/08/01-cap-pledge-sim/Makefile`

## Expected output

`make run` produced:

```text
cap.idx=1 obj=42
ensure(LOG)=1 ensure(MEM)=0
```

## Mapping to Skift

Closest real code equivalents:

- capability storage and lookup:
  - [`skift_sources/skift/src/kernel/hjert/core/domain.cpp`](skift_sources/skift/src/kernel/hjert/core/domain.cpp)
    - `Domain::_slots`
    - `Domain::add(...)` and `Domain::get(...)`
- pledge enforcement:
  - [`skift_sources/skift/src/kernel/hjert/core/task.cpp`](skift_sources/skift/src/kernel/hjert/core/task.cpp)
    - `Task::ensure(Flags<Hj::Pledge>)`
- syscall handlers that call `ensure(...)`:
  - [`skift_sources/skift/src/kernel/hjert/core/syscalls.cpp`](skift_sources/skift/src/kernel/hjert/core/syscalls.cpp)
    - e.g. `doCreate` checks different pledges for different object types

