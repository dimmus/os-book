# 02 - `Space::map` page alignment + overlap checks (host lab)

## Learning goal
`Hjert.Core:Space` mediates virtual memory mappings. Before touching the VMM it must:

- align requested ranges to page boundaries
- reject overlap with already mapped ranges
- (later) reject out-of-bounds mappings against the underlying VMO

In Skift, the overlap checks happen around:

- `vrange.ensureAligned(Hal::PAGE_SIZE)`
- `_ensureNotMapped(vrange)` and `_ranges` management

This host lab simulates only the alignment and overlap decision.

## Buildable files

- `src/08/02-space-map-overlap-sim/main.cpp`
- `src/08/02-space-map-overlap-sim/Makefile`

## Expected output

`make run` produced:

```text
req.start=21000 ok=0
```

## Mapping to Skift

Corresponding real logic:

- [`skift_sources/skift/src/kernel/hjert/core/space.cpp`](skift_sources/skift/src/kernel/hjert/core/space.cpp)
  - `vrange.ensureAligned(Hal::PAGE_SIZE)`
  - `_ensureNotMapped(vrange)` (already mapped => error)

