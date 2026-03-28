# 01 - Handover segment load simulation (host lab)

## Learning goal
Skift’s `opstart/loader.cpp` must load an ELF kernel image into memory using a simple but crucial rule:

- bytes present in the ELF file (`filez`) are copied into memory
- bytes beyond `filez` up to `memsz` (the `.bss`-like remainder) are zeroed
- `memsz` is page-aligned before mapping/using it (because page tables work in page granularity)

On real hardware, the loader also turns the kernel’s `vaddr` into the physical address range (`paddr`) used during early mapping.

This host lab simulates those three rules in ~20 lines so students can see the “BSS zeroing + alignment” mechanics independent of the ELF parser.

## Buildable files

- `src/06/01-handover-segment-load-sim/main.cpp`
- `src/06/01-handover-segment-load-sim/Makefile`

## Expected output

Running `make run` produced:

```text
paddr=2000
file[0..2]=1,2,3
bssStart=0 bssEnd=0
```

## Mapping to Skift

The host logic corresponds directly to these lines in:

- [`skift_sources/skift/src/kernel/opstart/loader.cpp`](skift_sources/skift/src/kernel/opstart/loader.cpp)
  - `paddr = prog.vaddr() - Handover::KERNEL_BASE`
  - `memsz = Hal::pageAlignUp(prog.memsz())`
  - `memcpy((void*)paddr, prog.buf(), prog.filez())`
  - `memset((void*)(paddr + prog.filez()), 0, remaining)` where `remaining = prog.memsz() - prog.filez()`

