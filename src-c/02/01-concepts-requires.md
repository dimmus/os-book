# 01 - Concepts and `requires` constraints (host lab)

## Learning goal
We want to practice how Skift’s C++ code keeps generic code safe:

- template code should reject incompatible types at compile time
- error messages should point to the missing *interface contract*

Skift’s `karm-core` uses `concept`s heavily (e.g. in the “Sliceable” example style).

This lab builds a `Sliceable` concept and uses it to constrain a `sum(...)` function.

## Code

Buildable files:

- `src/02/01-concepts-requires/main.cpp`
- `src/02/01-concepts-requires/Makefile`

## Expected output

`make run` produced:

```text
10
```

## Mapping to Skift / `karm-core`

### Compile-time interface contracts
In Skift’s kernel/library code, constrained templates prevent “wrong shape” values from reaching unsafe operations (like low-level mapping code).

You can find the C++20 feature motivation and examples in:

- [`skift_sources/skift/src/libs/karm-core/README.md`](skift_sources/skift/src/libs/karm-core/README.md)

### Why that matters at OS level
When teaching microarchitecture, we keep returning to this idea:

- low-level CPU transitions are brittle
- generic code is safe only if its preconditions are statically enforced whenever possible

Concepts are one of those enforcement mechanisms.

## Next step
Coroutines move this from “compile-time safety” to “control-flow safety”: how computations can pause/resume without losing invariants.

