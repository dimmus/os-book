# C-6 — Memory Model and Handover Mapping

## Learning goals
By the end of this chapter students should understand:

1. Why an OS loader must load code/data using `filez` vs `memsz` semantics.
2. Why early kernels must align memory regions to page boundaries.
3. How the microkernel builds its allocator model from the boot “handover payload”.
4. How the loader and kernel responsibilities are split across two languages layers:
   - loader: parses ELF + constructs the handover layout
   - kernel: turns handover layout into `pmm`/`kmm`/`vmm` state

## The key split: `opstart/loader.cpp` vs `Hjert.Core:initMem`

In Skift, the responsibilities are intentionally divided:

### Loader responsibility (boot-time memory layout construction)
Location:

- [`skift_sources/skift/src/kernel/opstart/loader.cpp`](skift_sources/skift/src/kernel/opstart/loader.cpp)

In `loadEntry`, the loader:

1. Maps/mmap’s its own “payload memory” region.
2. Opens the kernel ELF using `Vaerk::Elf::Image`.
3. For each `LOAD` program segment:
   - computes `paddr = prog.vaddr() - Handover::KERNEL_BASE`
   - computes `memsz = Hal::pageAlignUp(prog.memsz())`
   - copies `filez` bytes into memory
   - zeroes the remaining `memsz - filez`
   - appends a `Handover::KERNEL` record into the handover payload
4. Creates a stack mapping and adds `Handover::STACK`.
5. Maps kernel and upper-half regions in a temporary VMM so the kernel can execute.

Even though `opstart` is “just a bootloader”, this memory loading is the first real OS correctness constraint students must internalize.

### Kernel responsibility (turn handover payload into allocators + mapping)
Location:

- [`skift_sources/skift/src/kernel/hjert/core/mem.cpp`](skift_sources/skift/src/kernel/hjert/core/mem.cpp)

The kernel calls `initMem(payload)` as part of:

- [`skift_sources/skift/src/kernel/hjert/core/init.cpp`](skift_sources/skift/src/kernel/hjert/core/init.cpp)

Inside `initMem`, Skift:

1. Reads the “usable physical memory range” from `payload.usableRange<Hal::PmmRange>()`.
2. Computes a bitmap size for the physical memory allocator (`Pmm`).
3. Finds a free region in the handover payload suitable for the bitmap storage (`_findBitmapSpace`).
4. Constructs the physical page allocator (`Pmm`) and kernel heap mapper (`Kmm`).
5. Marks the handover “FREE” ranges as free in `pmm`.
6. Marks the first page and bitmap pages as used (so you don’t reallocate critical pages).
7. Maps:
   - the kernel portion into the global VMM
   - and the upper half mapping region
8. Activates the VMM (`Arch::globalVmm().activate()`).

The teaching point:

- the loader does not become a memory manager
- the kernel does not need to know ELF segment byte semantics
- the handover payload is the contract boundary between them

## Host lab: simulate `filez/memsz` segment loading

Lab:

- [`src/06/01-handover-segment-load-sim.md`](src/06/01-handover-segment-load-sim.md)

What to notice in the output:

- `file[0..2]` holds the copied “file” bytes
- `bssStart` and `bssEnd` are zero

This models what loader does before the kernel allocator/mappers even exist.

## A concrete microarchitecture mindset for students

Early OS memory work is less about “malloc” and more about these microarchitecture constraints:

- page granularity: mapping works on page-sized chunks
- implicit contract: you must zero `.bss`-like region or later code will see garbage
- address translation: `vaddr` and `paddr` have a deterministic relation early on (encoded via `KERNEL_BASE`)

If you want to enhance the OS later, start by instrumenting:

- whether every loader segment is page-aligned the way you expect
- whether `remaining` is computed from the correct ELF fields (`memsz - filez`)

## Next step

Next chapter will move from “how bytes land in memory” to “how CPU state moves between tasks”:

- scheduler selection (`Hjert.Core:sched`)
- task frames (`Hjert.Core:task`)
- context switching decision points (`Hjert.Core:x86_64/arch.cpp`)

