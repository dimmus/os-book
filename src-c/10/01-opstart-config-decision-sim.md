# 01 - Opstart menu vs splash decision (host lab)

## Learning goal
`opstart/entry.cpp` chooses between showing a menu or directly launching a single kernel.

Its core policy is:

- show the menu if `entries.len() > 1`
- show the menu if `entries.len() == 0` (invalid config)
- otherwise show a splash and launch the single entry

This host lab reproduces that policy with a pure function over `entriesLen`.

## Buildable files

- `src/10/01-opstart-config-decision-sim/main.cpp`
- `src/10/01-opstart-config-decision-sim/Makefile`

## Expected output

`make run` produced:

```text
len=0 -> menu
len=1 -> splash+load
len=2 -> menu
```

## Mapping to Skift

- [`skift_sources/skift/src/kernel/opstart/entry.cpp`](skift_sources/skift/src/kernel/opstart/entry.cpp)
  - `if (configs.entries.len() > 1 or configs.entries.len() == 0) showMenuAsync(...)`
  - else `splashScreen(...)` and then `Opstart::loadEntry(...)`

