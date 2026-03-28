# 01 - Init ELF mapping flow (host lab)

## Learning goal
`Hjert.Core::enterUserspace` does the kernel-side work that prepares the first user program:

- locate init ELF inside `init.bootfs`
- map its program segments into a new `Space`
- map the handover payload so init can access boot data
- allocate/map a user stack
- create a `Task` in USER mode and call `ready(entry, stackEnd, {handoverBase})`

This host lab simulates:

- locating an ELF range from a bootfs record
- selecting per-segment mapping mode (RW vs RX) based on a “writable” flag
- computing `stackSize` and printing `task.ready` arguments

## Buildable files

- `src/11/01-init-userspace-mapping-sim/main.cpp`
- `src/11/01-init-userspace-mapping-sim/Makefile`

## Expected output

The lab prints one line for the located ELF range, one line per segment, and one line for task creation parameters.

In our run, it produced:

```text
bootfs->elf start=0x1000800 len=8192
map RX vaddr=0x400000 size=12288
map RW vaddr=0x401000 size=8192
task.ready entry=0x400123 stack=65536 handoverArg=0xffff0000
```

## Mapping to Skift

- `_locateInit(...)` computations:
  - [`skift_sources/skift/src/kernel/hjert/core/init.cpp`](skift_sources/skift/src/kernel/hjert/core/init.cpp)
- `enterUserspace(...)` segment mapping:
  - mapping RX vs RW depends on whether the ELF program flags include `WRITE`

