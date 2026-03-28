# C-10 — Opstart: Bootloader Loading Kernel and Launching

## Learning goals
This chapter connects “data” and “control flow” in the boot path:

1. `opstart/entry.cpp` decides whether to show a menu or directly launch a single kernel entry.
2. `opstart/loader.cpp` turns a selected kernel entry into:
   - loaded ELF segments
   - a stack region
   - handover payload records
   - kernel VMM mappings
   - a final call into `Fw::enterKernel(...)`

Students should be able to explain:

- what `loader.json` represents (configs/entries/blobs)
- how validation errors redirect control to a menu/splash path
- why the loader must do VMM mapping before jumping to kernel entry

## Code to read (boot-time control flow)

### Entry selection + UI
- [`skift_sources/skift/src/kernel/opstart/entry.cpp`](skift_sources/skift/src/kernel/opstart/entry.cpp)

Key decision logic:

- parse `file:/loader.json` as JSON
- validate configs:
  - if `entries.len() > 1` or `entries.len() == 0` => show menu
  - else => splash screen and load single entry

### Menu UI (user interaction in pre-kernel world)
- [`skift_sources/skift/src/kernel/opstart/menu.cpp`](skift_sources/skift/src/kernel/opstart/menu.cpp)

Menu is built as a small reactive UI model:

- a `State` with `selected` index and optional `error`
- an `Action` union (move selection / select)
- a reducer that calls `Opstart::loadEntry` and stores errors
- `Ui::runAsync(...)` drives input events and renders the list

### Loader data model (`loader.h`)
- [`skift_sources/skift/src/kernel/opstart/loader.h`](skift_sources/skift/src/kernel/opstart/loader.h)

`Configs`/`Entry`/`Blob` parsing rules:

- icon may be an embedded `Image`
- `kernel` can be a string URL or an object with `url` + `props`
- `blobs` is an array of additional files/props

### ELF loading + handover records + transfer
- [`skift_sources/skift/src/kernel/opstart/loader.cpp`](skift_sources/skift/src/kernel/opstart/loader.cpp)

Key steps (ties back to chapter 06):

- map kernel ELF image (`Vaerk::Elf::Image image{kernelMem.bytes()}`)
- for each `LOAD` program:
  - compute `paddr` from `vaddr - Handover::KERNEL_BASE`
  - page-align `memsz`
  - copy `filez`, zero the rest
  - append `Handover::KERNEL` records
- leak blob memory so it remains valid when jumping into kernel
- read `.handover` requests section:
  - show how the kernel asks for resources it needs during init/userspace
- create a VMM and map:
  - kernel region (lower half)
  - upper half
  - boot-agent image (loader image)
- finalize the handover payload and call `Fw::enterKernel(...)`

## Host lab: opstart selection policy

The selection policy in `entry.cpp` is simple and is a great teaching micro-skill:

- show menu when there are multiple entries (user chooses)
- show menu when there are zero entries (configuration invalid)
- show splash when there is exactly one entry (auto-launch)

Lab:

- [`src/10/01-opstart-config-decision-sim.md`](src/10/01-opstart-config-decision-sim.md)

## Why this is a microarchitecture lesson

Boot code is frequently written as a linear script, but Skift’s opstart shows a different stance:

- even early boot has a user interaction surface (menu)
- error handling is still capability/object oriented
- the “handover payload” is the contract that avoids global mutable state

This is the same microarchitecture pattern we’ll keep reusing once we get to `Hjert.Core:init` and user-space IPC.

## Next step

Chapter `11` moves into kernel init:

- build allocators/mappers from the handover payload (`initMem`)
- locate and map the init ELF from bootfs
- create an initial user task and enter idle loop

