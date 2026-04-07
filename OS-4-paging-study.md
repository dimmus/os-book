# OS-4 — Stage 1 paging study (`os/`, hello-init track)

This page walks through the next tutorial milestone: implementing minimal x86_64 paging (VMM) right after the existing `HELLO INIT` stage.

We keep the same boot boundary from phase 0:

`UEFI -> ExitBootServices -> kernel_entry -> serial log`

## Changed and new files

Stage 1 paging touches the UEFI handover, the kernel’s page-table builder, and the link layout. Paths use [`src-os/`](src-os/) in this repo.

| Role | Path |
| --- | --- |
| Pass `mmapPhys`, `descSize`, `kernelPhys`, `kernelPages`, `stackPhys` into `kernel_entry` | [`src-os/uefi/efi_main.cpp`](src-os/uefi/efi_main.cpp) |
| Paging: `allocPage`, `mapPageDual`, `CR3`, Stage 1 serial markers | [`src-os/kernel/init.cpp`](src-os/kernel/init.cpp) |
| Link script (single load segment for `objcopy`) | [`src-os/kernel/ld.ld`](src-os/kernel/ld.ld) |
| Kernel build | [`src-os/kernel/Makefile`](src-os/kernel/Makefile) |

## Memory and CPU state snapshot (Stage 1)

**What you get from earlier milestones:**

| Input | Meaning |
| --- | --- |
| **`kernelPhys` / `kernelPages`** | Where the **loaded** kernel blob lives (still identity-mapped) |
| **`stackPhys`** | Kernel stack pages |
| **`mmapPhys` + sizes** | UEFI memory-map buffer to **touch** after paging proves mappings |
| Firmware **`CR3`** | Previous paging root — **replaced** when you load yours |

**What Stage 1 builds in RAM:**

```text
  phys pages allocated by kernel for tables ──►  PML4 ──► PDPT ──► PD ──► PT
                                                      │
                                                      └── leaf PTEs: 4 KiB frames
```

- **Dual-map** the same frames at **`virt = phys`** and **`virt = phys + UPPER_HALF`** (tutorial constant) so you can test the upper alias.
- Map at least: **kernel text/data**, **stack**, **mmap buffer** (and in later trees **LAPIC MMIO** — see [OS-7](OS-7-lapic-study.md)).

**CPU registers — activation path:**

```
CR3  ← physical address of PML4 (root of walk)
     every linear address → MMU walk PML4[47:39] → PDPT[38:30] → PD[29:21] → PT[20:12] → offset[11:0]
```

**After `mov cr3` / write to CR3:** TLB is tied to new mappings; **`invlpg`** only if you need to shoot down one VA later.

**Relations / paths:**

```text
allocPage() ──► free frames from mmap-described RAM
     │
mapPageDual(va_lo, va_hi, phys, flags) ──► fills PTEs
     │
asm: mov %0, %%cr3  (load root into CR3)  ──►  STAGE 1: touch via upper-half pointer to mmap buffer
```

**Not yet:** **IDT**/`lidt` ([OS-5](OS-5-idt-study.md)), **user** segments, **#PF** handler ([OS-8](OS-8-page-fault-study.md)) — faults still “mystery halt” or firmware behavior until IDT is yours.

## What we had before (phase 0)

In `os/`, the UEFI app:

1. Allocates pages for the kernel blob.
2. Allocates pages for a stack.
3. Gets the UEFI memory map descriptor buffer and then calls `ExitBootServices`.
4. Jumps into the raw kernel entry at the copied blob base.

The kernel entry (`os/kernel/init.cpp`) prints `HELLO INIT` and halts.

At that moment, we have *a working address space*, but we do not control it. The goal of Stage 1 is to start controlling it by switching to our own page tables.

## Stage 1 goal

Milestone 16 says the next step is:

> Build a minimal page table setup so the kernel can run in a predictable virtual address space.

### What “minimal x86_64 paging (VMM)” means

In x86_64, the CPU translates **virtual addresses** (what your code uses) into **physical addresses** (real RAM/device locations) by consulting **page tables**.

“Minimal paging” in this milestone means we implement only the smallest useful version of that translation:

1. Build a minimal 4-level page table hierarchy:
   - `PML4 -> PDPT -> PD -> PT`

   Important: `PML4`, `PDPT`, `PD`, and `PT` are **not CPU registers**.
   They are **page tables stored in memory** (arrays of entries).
   The CPU reads these tables while translating addresses.

   Where the names come from (abbreviations):
   - `PML4` = `Page Map Level 4` (the top/root paging structure on 4-level paging)
   - `PDPT` = `Page Directory Pointer Table` (level 2 structure that points to a Page Directory)
   - `PD` = `Page Directory` (level 3 structure that points to a Page Table)
   - `PT` = `Page Table` (level 4 leaf structure that contains the final 4KiB page mappings)

     Why it’s a chain: x86_64 splits the virtual address into multiple parts. Each level is a table with 512 entries, and each level uses a different 9-bit “slice” of the virtual address to pick which entry to follow:
     - `PML4` uses VA bits 47..39 and points to the `PDPT` table
     - `PDPT` uses bits 38..30 and points to the `PD` table
     - `PD` uses bits 29..21 and points to the `PT` table
     - `PT` uses bits 20..12 and selects the final 4KiB page mapping (physical frame)
2. Fill leaf entries (PT) so **4KiB pages** map to the intended physical frames.
3. Activate our tables by switching `CR3` (the “start using our paging” boundary).

   `CR3` is a CPU control register that holds (a physical address of) the
   root of the paging structure: the `PML4` table.

   When you write a new value into `CR3`, the CPU starts translating
   virtual addresses using the `PML4` you just installed (instead of the
   previous one that firmware/earlier code used). In practice this is the
   moment where your newly built mappings become “live”, and the
   translation caches (TLB) are invalidated/updated by the hardware.

   Note: `CR3` is an x86/x86_64-specific control register. On other CPU
   architectures, the “paging root” concept exists, but it’s controlled by
   different registers (for example, ARM uses `TTBR0/TTBR1`, and RISC-V uses
   `satp`).

We keep it intentionally small because the milestone’s focus is the *boot boundary learning goal* (UEFI → ExitBootServices → kernel entry) plus immediate verification.

Keeping paging “small” makes the failure surface smaller too:
- fewer mappings to get right (kernel/stack/mmap buffer only)
- fewer page-table allocations needed to reach a working `CR3` switch
- less chance that an unrelated mapping bug breaks the machine before we can print the next serial marker

## What a larger paging/VMM would add later

Later, “real” kernels usually expand this in several directions:
- Map more memory regions (code, data, heap, device windows) instead of just the current kernel/stack.
- Support different permission sets (user vs kernel pages, read/write/execute, NX/“no-exec”).
- Build per-process page tables (separate address spaces) instead of one shared kernel mapping.
- Add dynamic management features like demand paging, copy-on-write, and page fault handling.
- Optimize with larger page sizes (e.g. 2MiB/1GiB pages) and better TLB-friendly layouts.

### What it’s for (why we do it here)

Stage 1 paging/VMM gives the kernel:

- Predictable and controllable virtual memory: the address space after `CR3` is not dependent on whatever firmware paging state existed before.
- The ability to implement the next big tutorial idea: **upper-half addressing**.
  - We alias the same physical pages at `virt = phys` and also at `virt = phys + UPPER_HALF`, so later we can relocate/structure memory more deliberately.
- A safe transition: we use **dual mappings** so that after switching `CR3` the kernel still has working mappings for:
  - the kernel blob,
  - the stack,
  - and the UEFI memory-map buffer (used for the “touch memory” verification).

Specifically, we implement:

1. A minimal 4-level page table (PML4 -> PDPT -> PD -> PT).
2. A “dual mapping” strategy:
   - Identity map: `virt = phys` (so the kernel keeps working immediately after `CR3` switch).
   - Upper-half alias: `virt = phys + UPPER_HALF` (so we can prove upper-half mappings work).
3. A verification step after switching `CR3`:
   - Write/read (“touch memory”) through the upper-half alias of the UEFI memory map buffer.
   - Print `STAGE 1: paging done` if it succeeds.

## Step 0: pass the paging handover info into the kernel

After `ExitBootServices`, our kernel no longer calls UEFI functions, so it must be told what it needs:

- The physical address and size of the memory map descriptor buffer (`mmapPhys`, `mmapSizeUsed`, `descSize`).
- Where the kernel lives (`kernelPhys`, `kernelPages`).
- Where the stack lives (`stackPhys`).

### Where this is implemented

In:

- `[os/uefi/efi_main.cpp](/run/media/dimmus/dev1/os/0_brutal_skift/os_book/os/uefi/efi_main.cpp)`

we changed the kernel entry call to set SysV AMD64 argument registers:

- `rdi = mmapPhys`
- `rsi = mmapSizeUsed`
- `rdx = descSize`
- `rcx = kernelPhys`
- `r8  = kernelPages`
- `r9  = stackPhys`

### Code (from `os/uefi/efi_main.cpp`)

```cpp
asm volatile(
    "mov %[st], %%rsp\n\t"
    "mov %[mmapPhys], %%rdi\n\t"
    "mov %[mmapSize], %%rsi\n\t"
    "mov %[descSize], %%rdx\n\t"
    "mov %[kernelPhys], %%rcx\n\t"
    "mov %[kernelPages], %%r8\n\t"
    "mov %[stackPhys], %%r9\n\t"
    "call *%[e]\n\t"
    :
    : [st] "r"(stackTop),
      [mmapPhys] "r"(mmapPhys),
      [mmapSize] "r"(mmapSize),
      [descSize] "r"(descSize),
      [kernelPhys] "r"(kernelPhys),
      [kernelPages] "r"(kernelPages),
      [stackPhys] "r"(stackPhys),
      [e] "r"(entry)
    : "rsp", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "memory"
);
```

Line-by-line explanation (these are the *shown* lines above):

1. `asm volatile(`: emits inline assembly and prevents the compiler from assuming it has no side effects.
2. `"mov %[st], %%rsp\n\t"`: sets the CPU stack pointer (`RSP`) for the kernel before calling it.
3. `"mov %[mmapPhys], %%rdi\n\t"`: puts `mmapPhys` into argument register `RDI` (arg #1 in SysV AMD64).
4. `"mov %[mmapSize], %%rsi\n\t"`: puts `mmapSize` into `RSI` (arg #2).
5. `"mov %[descSize], %%rdx\n\t"`: puts `descSize` into `RDX` (arg #3).
6. `"mov %[kernelPhys], %%rcx\n\t"`: puts `kernelPhys` into `RCX` (arg #4).
7. `"mov %[kernelPages], %%r8\n\t"`: puts `kernelPages` into `R8` (arg #5).
8. `"mov %[stackPhys], %%r9\n\t"`: puts `stackPhys` into `R9` (arg #6).
9. `"call *%[e]\n\t"`: calls the kernel entry function pointer `entry`.
10. `:` (the empty output operand list): there are no C/C++ outputs produced by the asm block.
11. `: [st] "r"(stackTop), ...`: the input operands list; these values get substituted into the `%[...]` placeholders above.
12. `: "rsp", "rdi", ... "memory"`: tells the compiler which registers/memory this asm may change.

Deeper explanation:

This `asm` block is the glue between the UEFI loader and the freestanding C++ kernel. After `ExitBootServices`, the kernel can’t rely on firmware conventions anymore; the only reliable way to pass data is through CPU state (registers and stack). Setting `RSP` ensures that the very next function calls/locals in the kernel use a valid stack, and loading `rdi/rsi/rdx/rcx/r8/r9` ensures the kernel entry receives the physical addresses/sizes it needs for paging.

### Why those registers (SysV AMD64 ABI)

On x86_64, the compiler doesn’t “look at the stack” to find function arguments by default.
Instead, it follows a calling convention / ABI (Application Binary Interface).
For the System V AMD64 ABI, the first 6 integer/pointer arguments must be in:

- `rdi, rsi, rdx, rcx, r8, r9`

Because we enter `kernel_entry` using a hand-written `asm` call (after `ExitBootServices`),
there is no compiler-generated glue code to marshal arguments for us.
So we set the ABI-required registers ourselves to match the function signature in
`os/kernel/init.cpp`.

### Why `kernel_entry` should be `extern "C"`

The kernel entry function is marked `extern "C"` because we call it using
raw control-flow (`call *entry`) instead of going through normal C++
function-call ABI glue.

In C++, function names are *mangled* based on parameter/return types.
If we didn’t use `extern "C"`, the linker symbol name and/or the assumed
calling convention could differ from what our UEFI side expects.

Using `extern "C"` keeps the entry symbol predictable and compatible with
the ABI assumptions we make when we set the argument registers and jump to it.

## Step 1: decide which pages must be mapped

After we write our page tables, we switch CR3. The CPU will then translate addresses using *our* tables.

### Code (from `os/kernel/init.cpp`)

```cpp
uint64_t kernelSizeBytes = kernelPages * PAGE_SIZE;
constexpr uint64_t STACK_PAGES = 4;
uint64_t stackSizeBytes = STACK_PAGES * PAGE_SIZE;
uint64_t stackEnd = stackPhys + stackSizeBytes;

uint64_t mmapStart = pageAlignDown(mmapPhys, PAGE_SIZE);
uint64_t mmapEnd = pageAlignUp(mmapPhys + mmapSizeUsed, PAGE_SIZE);
uint64_t mmapTouchPhys = pageAlignDown(mmapPhys, PAGE_SIZE);
uint64_t mmapTouchSize = PAGE_SIZE;

uint64_t kernelEnd = kernelPhys + kernelSizeBytes;
```

Line-by-line explanation (shown lines above):

1. `kernelSizeBytes = kernelPages * PAGE_SIZE;`: total byte size of the kernel blob we must map.
2. `STACK_PAGES = 4;`: stack is exactly 4 pages in this tutorial.
3. `stackSizeBytes`: total stack byte size.
4. `stackEnd`: end boundary of the stack region (`[stackPhys, stackEnd)`).
5. `mmapStart`: page-aligned start of the memory-map descriptor buffer.
6. `mmapEnd`: page-aligned end of that buffer.
7. `mmapTouchPhys`: the page-aligned physical address used for the upper-half “touch memory” test.
8. `mmapTouchSize`: touch size (1 page).
9. `kernelEnd`: end boundary of the kernel blob region (`[kernelPhys, kernelEnd)`).

Deeper explanation:

These computed boundaries are exactly the physical frames the kernel will dereference after we switch `CR3`. If even one of those regions is missing from the new page tables, the first access inside that range can trigger a page fault. The page-aligning (`pageAlignDown/Up`) is essential because the paging hardware maps in whole 4KiB pages: mapping a region using unaligned start/end would either miss bytes or create partial-page coverage that doesn’t match what the CPU will actually translate.

So we must map anything our code will touch:

1. Kernel text + data we execute (we cover it by mapping the whole kernel blob range).
2. The current stack pages (to keep `RSP` valid).

   `RSP` is the CPU register that points to the current top of the stack.
   The kernel uses the stack for function calls, local variables, and
   temporary storage.

   After switching `CR3`, addresses are translated using *our* page
   tables. So if the physical frames that back the current stack (at the
   current `RSP` value) are not mapped, the very next stack access can
   trigger a page fault (and the kernel dies before we can print the next
   stage marker).
3. The UEFI memory-map descriptor buffer (to run the upper-half verification after CR3 switch).

   Even though we “leave UEFI” after `ExitBootServices` (we no longer call
   any UEFI functions), we still use the *data* we copied into RAM:

   - the UEFI memory map descriptors live in the `mmapPhys` buffer we
     allocated earlier
   - after `ExitBootServices`, firmware services are gone, but normal
     memory at `mmapPhys` remains
   - in Stage 1 we need to access that buffer after switching `CR3` to
     prove the upper-half mapping works via a simple read/write
     (“touch memory”) test

We also map everything “twice”:

- Identity: `virt = phys`
- Upper-half: `virt = phys + UPPER_HALF`

Why do both?

- Identity mapping (`virt = phys`) makes the `CR3` switch safer:
  when the CPU starts using our page tables, the addresses we’re
  currently executing on (kernel code + current stack) still refer to
  the same physical frames.
- Upper-half mapping (`virt = phys + UPPER_HALF`) lets us access the
  same physical memory through a *different* virtual region:
  this is the whole point of the next milestone direction, and it gives
  us a simple post-`CR3` verification target (the “touch memory” check
  in the upper-half alias).


## Step 2: implement the minimal page table builder

In Stage 1, we only need enough of the paging model to translate **4KiB pages**.

Why pick 4KiB?

- 4KiB is the **base page size** for the classic x86_64 4-level paging mode. It’s the default “leaf mapping” size and matches the simplest PT format.
- Using 4KiB pages keeps the problem small and mechanical:
  - you only need to fill PT entries (leaf level) with a physical frame address
  - you can ignore the extra “large page” machinery (like 2MiB/1GiB pages, different entry formats, and stricter alignment requirements)
- The tutorial milestone wants confidence in the CR3/page-table-walk boundary. 4KiB pages are enough to prove that boundary works without adding complexity from large-page support.

- Each level has 512 entries.
- Each virtual address selects indices from:
  - `PML4` bits 47..39
  - `PDPT` bits 38..30
  - `PD` bits 29..21
  - `PT` bits 20..12

### Where this is implemented

In:

- `[os/kernel/init.cpp](/run/media/dimmus/dev1/os/0_brutal_skift/os_book/os/kernel/init.cpp)`

the page-table setup functions compute indices and fill entries:

- allocate a page table page when an entry is missing
- write the “next level physical address + PRESENT + WRITABLE”
- for leaf entries (PT), write `paddr + PRESENT + WRITABLE`

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
auto idxPml4 = [&](uint64_t vaddr) -> uint64_t { return (vaddr >> 39) & 0x1ff; };
auto idxPdpt = [&](uint64_t vaddr) -> uint64_t { return (vaddr >> 30) & 0x1ff; };
auto idxPd = [&](uint64_t vaddr) -> uint64_t { return (vaddr >> 21) & 0x1ff; };
auto idxPt = [&](uint64_t vaddr) -> uint64_t { return (vaddr >> 12) & 0x1ff; };

auto makeEntryAddr = [&](uint64_t physPage) -> uint64_t {
    return (physPage & ~0xfffull); // keep bits [51:12], clear low 12
};
```

<details>
<summary>C++ language explanation (skip if you already know)</summary>

What does this C++ lambda construction mean (`[&](uint64_t vaddr) -> uint64_t`)?

`[&]` is the capture list:
- `&` means “capture used outer variables by reference”.
- In this code it lets the lambda access values like `vaddr`/`physPage` parameters and any surrounding constants without copying.

`(uint64_t vaddr)` is the parameter list:
- the lambda takes one argument, named `vaddr`, typed as `uint64_t`.

`-> uint64_t` is the trailing return type:
- it explicitly declares that the lambda returns a `uint64_t`.

`{ return ...; }` (the body):
- computes and returns the value that the later page-table-walk logic needs (the 9-bit index or the masked physical frame address).

</details>

Line-by-line explanation (shown lines above):

Before the per-level explanations:

- `0x1ff` is `9` one-bits in binary (`0b111111111`). That’s why `& 0x1ff` extracts exactly a 9-bit index.
- Each paging level has `512` entries, so the index must be in the range `[0, 511]` (9 bits).

1. `idxPml4(...)`: extracts the `PML4` index from the virtual address.
   - `(vaddr >> 39)` shifts the virtual address right so that VA bits `47..39` move into the low 9 bits.
   - `& 0x1ff` then masks out exactly those low 9 bits, producing the entry number for the `PML4`.
2. `idxPdpt(...)`: extracts the `PDPT` index.
   - `(vaddr >> 30)` moves VA bits `38..30` into the low 9 bits.
   - `& 0x1ff` keeps only those 9 bits as the `PDPT` entry index.
3. `idxPd(...)`: extracts the `PD` index.
   - `(vaddr >> 21)` moves VA bits `29..21` into the low 9 bits.
   - `& 0x1ff` masks to a 9-bit `PD` entry index.
4. `idxPt(...)`: extracts the `PT` index (the final step of the page-table walk).
   - `(vaddr >> 12)` moves VA bits `20..12` into the low 9 bits.
   - `& 0x1ff` masks to the 9-bit `PT` entry index, selecting the final 4KiB page mapping.
5. `makeEntryAddr(...)`: prepares a physical address value to store in a page-table entry by clearing the low 12 bits (those low bits are the page offset, not part of the entry’s frame number).

Deeper explanation:

Conceptually, the page-table walk is a “tree walk” driven by the virtual address. Each level consumes one 9-bit chunk of the address to choose which entry to follow next, and the repeated pattern (shift right, mask with `0x1ff`) converts “which bits belong to this level” into a usable array index in the range 0–511. Once the CPU reaches the `PT` level, the selected entry finally determines the physical 4KiB frame.

The second half of “minimal page table builder” is writing the leaf mapping (the `PT` entry):

```cpp
uint64_t& pte = reinterpret_cast<uint64_t*>(ptPhys)[idxPt(v)];
pte = makeEntryAddr(p) | PTE_PRESENT | (pageFlags & PTE_WRITABLE);
```

Line-by-line explanation:

1. `pte = ...[idxPt(v)]`: finds the `PT` entry that corresponds to the 4KiB page containing `v`.
2. `pte = ... | PRESENT | WRITABLE`: writes the physical frame base plus permission bits so the CPU can translate the page and allow writes.

Deeper explanation:

After this `pte` assignment, the CPU has everything it needs to translate addresses that hit this particular `PT` entry. The value stored in the entry is effectively “the 4KiB frame this VA should land in” (plus flags). `PRESENT` tells the hardware that the mapping exists, and `WRITABLE` allows writes; without these flags, the translation might fail or the access would fault.

Why the cast `reinterpret_cast<uint64_t*>(ptPhys)`?

- `ptPhys` is a physical address of the page-table page that holds the `PT` entries.
- During this stage we ensure we have an identity mapping (`virt = phys`) for the pages we are editing, so the physical address `ptPhys` is also reachable as a usable address in the current virtual address space.
- `reinterpret_cast<uint64_t*>(ptPhys)` just tells the compiler: “treat the bytes at address `ptPhys` as an array of `uint64_t` entries”.
- Then `[idxPt(v)]` selects the 64-bit entry corresponding to the specific 4KiB virtual page we are mapping.

Where does `makeEntryAddr(...)` come from?

- It’s defined earlier in the same “minimal page table builder” snippet as a helper lambda:
  `makeEntryAddr = [&](uint64_t physPage) -> uint64_t { return (physPage & ~0xfffull); }`.
- That helper clears the low 12 bits of the physical page address so the value we store matches the page-table entry format (frame number vs page offset bits).

Why use the `|` (bitwise OR) construction here?

- A page-table entry is essentially: `frame_address_bits + flag_bits`.
- `makeEntryAddr(p)` produces the `frame_address_bits` portion (with the low 12 bits cleared).
- `PTE_PRESENT` and `PTE_WRITABLE` are the flag bits that live in those low bits.
- Using `|` merges the two without overwriting the frame base:
  it sets the relevant flag bits while keeping the address bits intact.

## Step 3: allocate memory for page tables (practical tutorial choice)

We need physical pages to store our tables.

The simplest safe choice for this tutorial is to use the memory-map buffer itself as scratch space:

- `mmapPhys` is a real physical RAM region that the UEFI app allocates and then fills with the memory-map descriptors via `getMemoryMap(fill)`.
- We parse the memory-map descriptors and **cache** the important conventional ranges first.
  - “Cache” here means: copy the physical `[start, end)` regions of memory
    we consider usable into a small local array (`ranges[]`) in the kernel.
  - Later, if the scratch space inside the mmap region runs out, the page-table
    allocator can fall back to using pages from these cached conventional ranges.
- After caching, we can treat that region as normal RAM and overwrite some of its pages with our own page tables.
  - For Stage 1 verification we do not require the original descriptor bytes to remain intact; we only require that the page containing `mmapTouchPhys` is mapped (so we can read/write it through the upper-half alias).

This avoids needing a full-blown physical allocator and keeps Stage 1 focused on the paging boundary itself.

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
uint64_t scratch = pageAlignUp(mmapStart, PAGE_SIZE);
uint64_t scratchEnd = mmapAllocEnd;
uint64_t scratchNext = scratch;

auto allocPage = [&]() -> uint64_t {
    if (scratchNext + PAGE_SIZE <= scratchEnd) {
        uint64_t p = scratchNext;
        scratchNext += PAGE_SIZE;
        auto* page = reinterpret_cast<uint64_t*>(p);
        for (uint64_t i = 0; i < 512; ++i)
            page[i] = 0;
        return p;
    }
    return 0;
};
```

Line-by-line explanation (shown lines above):

1. `scratch = pageAlignUp(mmapStart, PAGE_SIZE)`: chooses the first page-aligned address inside the mmap buffer to overwrite with page tables.
2. `scratchEnd = mmapAllocEnd`: end boundary where we stop allocating scratch.
3. `scratchNext = scratch`: bump-pointer cursor (next free scratch page).
4. `allocPage = ...`: returns a physical 4KiB page that we can treat as a page-table.
5. `if (scratchNext + PAGE_SIZE <= scratchEnd)`: fast path: do we still have room in the scratch region?
6. `p = scratchNext`: take the current scratch page address.
7. `scratchNext += PAGE_SIZE`: advance to the next free scratch page.
8. `page = reinterpret_cast<uint64_t*>(p)`: treat the scratch page as an array of 512 64-bit entries.
9. `for (...) page[i] = 0`: clear the table so “missing” entries start out not-present.
10. `return p`: hand back the new page-table page.
11. `return 0`: allocation failure indicator (the caller prints a fail marker and halts).

Deeper explanation:

Paging structures are just normal memory filled with entries—so we must place them in writable physical RAM. The bump-pointer scratch allocator (`scratchNext`) avoids needing a full physical allocator for the tutorial: it hands out page-sized chunks inside a known buffer and clears them to all-zero (meaning “not present”). That “start empty” property is important: only entries we explicitly fill with `PRESENT` become usable mappings.

## Step 4: fill dual mappings

We then map:

1. Kernel pages:
   - `map(kernelPhys -> kernelPhys)` identity
   - `map(kernelPhys -> kernelPhys + UPPER_HALF)` upper-half alias
2. Stack pages:
   - `map(stackPhys -> stackPhys)` identity
   - `map(stackPhys -> stackPhys + UPPER_HALF)` upper-half alias
3. Memory map buffer pages:
   - `map(mmapPhys -> mmapPhys)` identity
   - `map(mmapPhys -> mmapPhys + UPPER_HALF)` upper-half alias

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
mapRangeDual(kernelPhys, kernelPhys, kernelSizeBytes);
mapRangeDual(stackPhys, stackPhys, stackSizeBytes);
mapRangeDual(mmapTouchPhys, mmapTouchPhys, mmapTouchSize);

auto mapUpperHalfAliases = [&]() {
    for (uint64_t off = 0; off < kPagesBytes; off += PAGE_SIZE)
        mapPageDual(kernelPhys + UPPER_HALF + off, kernelPhys + off, PTE_WRITABLE);
    for (uint64_t off = 0; off < sPagesBytes; off += PAGE_SIZE)
        mapPageDual(stackPhys + UPPER_HALF + off, stackPhys + off, PTE_WRITABLE);
    for (uint64_t off = 0; off < mPagesBytes; off += PAGE_SIZE)
        mapPageDual(mmapTouchPhys + UPPER_HALF + off, mmapTouchPhys + off, PTE_WRITABLE);
};

mapUpperHalfAliases();
```

Line-by-line explanation (shown lines above):

1. `mapRangeDual(kernelPhys, kernelPhys, kernelSizeBytes)`: installs the identity mapping for kernel pages.
2. `mapRangeDual(stackPhys, stackPhys, stackSizeBytes)`: installs the identity mapping for stack pages.
3. `mapRangeDual(mmapTouchPhys, mmapTouchPhys, mmapTouchSize)`: identity-maps the single mmap page we’ll touch later.
4. `auto mapUpperHalfAliases = [&]() {`: defines a helper that creates the upper-half alias mappings.
5. First `for` loop: maps each kernel page to `kernelPhys + UPPER_HALF + off`.
6. Second `for` loop: maps each stack page to `stackPhys + UPPER_HALF + off`.
7. Third `for` loop: maps the mmap touch page to `mmapTouchPhys + UPPER_HALF + off`.
8. `mapUpperHalfAliases();`: runs the alias mapping code.

Deeper explanation:

Stage 1’s dual mapping strategy is what makes the `CR3` switch survivable and the upper-half goal testable. The identity mappings (`virt = phys`) keep the currently executing code/stack addresses working after paging is activated. The upper-half aliases (`virt = phys + UPPER_HALF`) create a second virtual view of the same physical memory so we can read/write a chosen test address in that higher range.

At this point, *our* page tables contain the translations needed for the kernel to keep running and for verification to work.

## Step 5: switch `CR3`

The actual “activate paging boundary” step is:

- load our PML4 physical address into `CR3`

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
asm volatile("mov %0, %%cr3" : : "r"(pml4Phys) : "memory");
write_stage1_touch_memory();
```

Line-by-line explanation (shown lines above):

1. `asm volatile("mov %0, %%cr3" ...)`: switches the paging root by loading `pml4Phys` into the CPU register `CR3`.
2. `write_stage1_touch_memory();`: prints the “touch memory” marker before performing the verification access.

Deeper explanation:

Loading `CR3` is the moment the CPU starts using our page tables as the source of truth for virtual-to-physical translation. Hardware also invalidates/updates translation caches (TLB) so stale translations don’t apply. That’s why we set up identity mappings first: otherwise, the very next instruction fetch or stack access could fault before we can print anything else.

After this instruction:

- instruction fetch and stack access use our mappings
- the upper-half alias access can now succeed (or fault, if you missed a mapping)

What that means in practice:

- “instruction fetch” means the CPU needs to keep fetching machine code from the kernel’s current virtual addresses. Because we built an identity mapping (`virt = phys`) for the kernel/stack pages, those instruction fetches keep translating to the same physical pages.
- “stack access” means every function call and local variable uses the current stack at `RSP`. With the new page tables, the stack’s physical frames must also be mapped; otherwise the very next push/pop or call can page-fault.
- the “upper-half alias access” is our verification: after `CR3`, we try to read/write `mmapPhys + UPPER_HALF`. If the upper-half mapping is missing or wrong, the access faults; if it’s correct, it succeeds.

## Step 6: verify with an upper-half memory touch

We verify by:

1. Taking an address in the upper-half alias of the memory-map buffer:
   - `test = mmapPhys + UPPER_HALF`
2. Reading a 64-bit word from `test`
3. Writing the same 64-bit word back after flipping bits (using bitwise XOR with a constant pattern)
4. Restoring the original value

### Code (excerpt from `os/kernel/init.cpp`)

```cpp
volatile uint64_t* testPtr =
    reinterpret_cast<volatile uint64_t*>(mmapPhys + UPPER_HALF);
uint64_t old = *testPtr;
*testPtr = old ^ 0xa5a5a5a5a5a5a5a5ull;
(void)*testPtr;
*testPtr = old;
write_stage1_done();
```

Line-by-line explanation (shown lines above):

1. `testPtr = ...`: forms an address in the upper-half alias (`mmapPhys + UPPER_HALF`) and casts it to a volatile pointer so the compiler must actually access memory.
2. `old = *testPtr;`: reads the current 64-bit value through the page tables.
3. `*testPtr = old ^ ...`: uses bitwise XOR (`^`) to flip bits in the 64-bit word, then writes the result back to confirm the mapping supports writes.
4. `(void)*testPtr;`: forces the compiler to keep the read/write behavior (prevents aggressive optimization).
5. `*testPtr = old;`: restores the original value to leave memory unchanged.
6. `write_stage1_done();`: prints the final “Stage 1 done” marker after the test succeeded.

Deeper explanation:

The “touch memory” test is deliberately simple but it checks two important things: (1) that the upper-half virtual address translates correctly to the intended physical page, and (2) that the mapping’s permissions allow writes. Using a `volatile` pointer prevents the compiler from optimizing away the memory access, and restoring the original value avoids leaving the descriptor buffer corrupted.

At line *testPtr = old ^ 0xa5a5a5a5a5a5a5a5ull;:
- testPtr is a pointer to a memory location (the upper-half virtual address of the mmap buffer).
- *testPtr means “the 64-bit value stored at that memory location”.
- old is the previously read 64-bit value from *testPtr.
- ^ is the bitwise XOR operator.
- 0xa5a5a5a5a5a5a5a5ull is a constant pattern.
- So this line means: flip bits in the 64-bit word (using XOR with that pattern), then write the modified 64-bit word back to the same memory address.

If the mapping is correct, these operations succeed and the stage marker prints.

### Expected serial output

You should see:

- `HELLO INIT`
- `STAGE 1: build paging`
- `STAGE 1: touch memory`
- `STAGE 1: paging done`

If QEMU halts/crashes, the last printed line usually tells you which boundary failed:

- failure before `touch memory`: page-table build / CR3 switch / missing identity mappings
- failure at `touch memory`: missing or wrong upper-half mapping

## Next step: milestone 2 (IDT + interrupts)

The `os/` tree in this book is currently **paging-only** (Stage 1). A follow-up milestone can add an IDT, trigger `int3` (#BP), and log from a dispatcher over serial; see `OS-5-idt-study.md` for notes when you implement it.

