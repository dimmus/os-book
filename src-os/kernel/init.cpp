// Minimal kernel-mode init (os phase-0 tutorial milestone).
//
// Important for this milestone:
// - the "kernel" is loaded by our UEFI app into allocated pages
// - Stage 1: minimal paging (CR3) + upper-half verification
// - Stage 2: IDT + #BP via int3 (see OS-5-idt-study.md)
//
// We print directly to COM1 and then halt forever.

#include "../common/serial.hpp"

#include <stdint.h>

static inline void write_hello_init() {
    // Print without using a NUL-terminated string in .rodata.
    serial::write_char('H');
    serial::write_char('E');
    serial::write_char('L');
    serial::write_char('L');
    serial::write_char('O');
    serial::write_char(' ');
    serial::write_char('I');
    serial::write_char('N');
    serial::write_char('I');
    serial::write_char('T');
    serial::write_char('\n');
}

static inline void write_stage_char(char c) {
    serial::write_char(c);
}

static inline void write_stage1_done() {
    // Avoid NUL-terminated strings: linker script includes only `.text`.
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('1');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('p');
    write_stage_char('a');
    write_stage_char('g');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('g');
    write_stage_char(' ');
    write_stage_char('d');
    write_stage_char('o');
    write_stage_char('n');
    write_stage_char('e');
    write_stage_char('\n');
}

static inline void write_stage1_fail_code(char code) {
    // Re-print the same failure prefix, then add a one-character code.
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('1');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('p');
    write_stage_char('a');
    write_stage_char('g');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('g');
    write_stage_char(' ');
    write_stage_char('f');
    write_stage_char('a');
    write_stage_char('i');
    write_stage_char('l');
    write_stage_char(' ');
    write_stage_char(code);
    write_stage_char('\n');
}

static inline void write_stage1_build_paging() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('1');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('b');
    write_stage_char('u');
    write_stage_char('i');
    write_stage_char('l');
    write_stage_char('d');
    write_stage_char(' ');
    write_stage_char('p');
    write_stage_char('a');
    write_stage_char('g');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('g');
    write_stage_char('\n');
}

static inline void write_stage1_touch_memory() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('1');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('t');
    write_stage_char('o');
    write_stage_char('u');
    write_stage_char('c');
    write_stage_char('h');
    write_stage_char(' ');
    write_stage_char('m');
    write_stage_char('e');
    write_stage_char('m');
    write_stage_char('o');
    write_stage_char('r');
    write_stage_char('y');
    write_stage_char('\n');
}

// ----------------------------
// Stage 2: IDT + breakpoint (Steps 2–6 of OS-5-idt-study.md)
// ----------------------------

struct IdtEntry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} __attribute__((packed));

static IdtEntry g_idt[256] __attribute__((aligned(16)));

static void idtClearAll() {
    for (unsigned i = 0; i < 256; ++i) {
        g_idt[i].offset_lo = 0;
        g_idt[i].offset_mid = 0;
        g_idt[i].offset_hi = 0;
        g_idt[i].selector = 0;
        g_idt[i].ist = 0;
        g_idt[i].type_attr = 0;
        g_idt[i].reserved = 0;
    }
}

static void idtSetGate(uint8_t vector, uint64_t handlerVirt, uint16_t cs, uint8_t typeAttr) {
    IdtEntry* e = &g_idt[vector];
    e->offset_lo = static_cast<uint16_t>(handlerVirt & 0xffffu);
    e->offset_mid = static_cast<uint16_t>((handlerVirt >> 16) & 0xffffu);
    e->offset_hi = static_cast<uint32_t>(handlerVirt >> 32);
    e->selector = cs;
    e->ist = 0;
    e->type_attr = typeAttr;
    e->reserved = 0;
}

struct IdtPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static void idtLoad(uint64_t baseVirt, uint16_t limit) {
    IdtPointer p{};
    p.limit = limit;
    p.base = baseVirt;
    asm volatile("lidt %0" : : "m"(p) : "memory");
}

static inline void write_stage2_install_idt() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('2');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('s');
    write_stage_char('t');
    write_stage_char('a');
    write_stage_char('l');
    write_stage_char('l');
    write_stage_char(' ');
    write_stage_char('i');
    write_stage_char('d');
    write_stage_char('t');
    write_stage_char('\n');
}

static inline void write_stage2_int3_marker() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('2');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('t');
    write_stage_char('3');
    write_stage_char('\n');
}

static inline void write_stage2_dispatcher_bp() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('2');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('#');
    write_stage_char('B');
    write_stage_char('P');
    write_stage_char(' ');
    write_stage_char('d');
    write_stage_char('i');
    write_stage_char('s');
    write_stage_char('p');
    write_stage_char('a');
    write_stage_char('t');
    write_stage_char('c');
    write_stage_char('h');
    write_stage_char('e');
    write_stage_char('r');
    write_stage_char('\n');
}

static inline void write_stage2_done() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('2');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('d');
    write_stage_char('o');
    write_stage_char('n');
    write_stage_char('e');
    write_stage_char('\n');
}

extern "C" void isr_breakpoint(void);

extern "C" void breakpoint_dispatch(void) {
    write_stage2_dispatcher_bp();
}

static inline uint64_t pageAlignDown(uint64_t x, uint64_t pageSize) {
    return x & ~(pageSize - 1);
}

static inline uint64_t pageAlignUp(uint64_t x, uint64_t pageSize) {
    return (x + pageSize - 1) & ~(pageSize - 1);
}

// Must be the first bytes of the kernel image: UEFI jumps to `kernelPhys`
// (offset 0 of the raw blob). Other functions must not precede this in
// `.text` unless forced by the linker script.
__attribute__((section(".text.kernel_entry")))
extern "C" void kernel_entry(
    uint64_t mmapPhys,
    uint64_t mmapSizeUsed,
    uint64_t descSize,
    uint64_t kernelPhys,
    uint64_t kernelPages,
    uint64_t stackPhys
) {
    serial::init_115200();
    write_hello_init();

    // ----------------------------
    // Stage 1: minimal paging.
    // ----------------------------
    constexpr uint64_t PAGE_SIZE = 4096;
    // Mirrors Skift's x86_64 upper-half constant. For x86_64 canonical
    // addresses, this is a sign-extended "negative" high range.
    constexpr uint64_t UPPER_HALF = 0xffff800000000000ull;
    constexpr uint64_t PTE_PRESENT = 1ull << 0;
    constexpr uint64_t PTE_WRITABLE = 1ull << 1;

    struct EFI_MEMORY_DESCRIPTOR {
        uint32_t Type;
        uint32_t Pad;
        uint64_t PhysicalStart;
        uint64_t VirtualStart;
        uint64_t NumberOfPages;
        uint64_t Attribute;
    };

    write_stage1_build_paging();

    // Basic derived ranges.
    uint64_t kernelSizeBytes = kernelPages * PAGE_SIZE;
    // Must match the UEFI loader constant: stackPages = 4 (16KiB).
    constexpr uint64_t STACK_PAGES = 4;
    uint64_t stackSizeBytes = STACK_PAGES * PAGE_SIZE;
    uint64_t stackEnd = stackPhys + stackSizeBytes;

    // Map the whole memory-map descriptor buffer in both regions so we
    // can verify upper-half mappings by touching it after the CR3 switch.
    uint64_t mmapStart = pageAlignDown(mmapPhys, PAGE_SIZE);
    uint64_t mmapEnd = pageAlignUp(mmapPhys + mmapSizeUsed, PAGE_SIZE);
    // For the upper-half touch test we only need the first page.
    uint64_t mmapTouchPhys = pageAlignDown(mmapPhys, PAGE_SIZE);
    uint64_t mmapTouchSize = PAGE_SIZE;
    uint64_t mmapAllocEnd = mmapEnd;

    // The kernel blob range.
    uint64_t kernelEnd = kernelPhys + kernelSizeBytes;

    // --------
    // Helpers
    // --------
    auto idxPml4 = [&](uint64_t vaddr) -> uint64_t { return (vaddr >> 39) & 0x1ff; };
    auto idxPdpt = [&](uint64_t vaddr) -> uint64_t { return (vaddr >> 30) & 0x1ff; };
    auto idxPd = [&](uint64_t vaddr) -> uint64_t { return (vaddr >> 21) & 0x1ff; };
    auto idxPt = [&](uint64_t vaddr) -> uint64_t { return (vaddr >> 12) & 0x1ff; };

    auto makeEntryAddr = [&](uint64_t physPage) -> uint64_t {
        return (physPage & ~0xfffull); // keep bits [51:12], clear low 12
    };

    // Page table allocator:
    // - primary: allocate from the memory-map buffer itself (already mapped
    //   by firmware before CR3 switch)
    // - requires no additional assumptions about firmware mappings.
    //
    // We also "cache" a few conventional ranges from the descriptor list so
    // that if we run out of space in the buffer we can still try.
    constexpr uint64_t CONVENTIONAL_MEMORY_TYPE = 7;
    struct Range {
        uint64_t start;
        uint64_t end; // exclusive
    };
    constexpr uint64_t MAX_RANGES = 64;
    Range ranges[MAX_RANGES];
    uint64_t rangeCount = 0;

    // Cache conventional ranges (for potential fallback allocations).
    //
    // Note: the memory-map descriptor buffer is not guaranteed to be
    // fully identity-mapped in the kernel address space after
    // `ExitBootServices`. To avoid page faults while parsing, scan only
    // the first few pages worth of descriptors and stop early.
    if (descSize == 0 || mmapSizeUsed < descSize) {
        write_stage1_fail_code('A');
        for (;;) {
            asm volatile("cli; hlt");
        }
    }

    uint64_t descCount = mmapSizeUsed / descSize;
    auto* descBytes = reinterpret_cast<uint8_t*>(mmapPhys);
    constexpr uint32_t LOADER_DATA_TYPE = 2;

    // Scan more descriptors so fallback conventional ranges are more likely
    // to be discovered on firmware with larger memory maps.
    constexpr uint64_t kMaxScanBytes = 64 * PAGE_SIZE; // 256KiB
    uint64_t scanBytes = mmapSizeUsed < kMaxScanBytes ? mmapSizeUsed : kMaxScanBytes;
    uint64_t scanCount = scanBytes / descSize;
    for (uint64_t i = 0; i < descCount && i < scanCount && rangeCount < MAX_RANGES; ++i) {
        const auto* d = reinterpret_cast<const EFI_MEMORY_DESCRIPTOR*>(descBytes + i * descSize);
        if (d->Type == LOADER_DATA_TYPE) {
            uint64_t start = d->PhysicalStart;
            uint64_t end = start + d->NumberOfPages * PAGE_SIZE;
            if (mmapPhys >= start && mmapPhys < end) {
                mmapAllocEnd = pageAlignUp(end, PAGE_SIZE);
            }
        }
        if (d->Type == CONVENTIONAL_MEMORY_TYPE && d->NumberOfPages > 0) {
            uint64_t start = pageAlignDown(d->PhysicalStart, PAGE_SIZE);
            uint64_t end = start + d->NumberOfPages * PAGE_SIZE;
            ranges[rangeCount++] = Range{start, end};
        }
    }

    // Where we store page tables (scratch) before CR3 switch.
    // Allocate from the memory-map buffer itself.
    uint64_t scratch = pageAlignUp(mmapStart, PAGE_SIZE);

    // If we couldn't detect an expanded LOADER_DATA region for the mmap
    // buffer, still leave some headroom so the page-table builder has
    // enough space for the missing PD/PT levels.
    if (mmapAllocEnd == mmapEnd) {
        // Reserve a larger local pool for page-table pages to avoid early
        // exhaustion before the fallback allocator kicks in.
        uint64_t candidate = mmapEnd + 256 * PAGE_SIZE;
        uint64_t nextReservedStart = 0;
        if (kernelPhys > mmapEnd)
            nextReservedStart = kernelPhys;
        if (stackPhys > mmapEnd && (nextReservedStart == 0 || stackPhys < nextReservedStart))
            nextReservedStart = stackPhys;
        if (nextReservedStart != 0 && candidate > nextReservedStart)
            candidate = nextReservedStart;
        mmapAllocEnd = pageAlignUp(candidate, PAGE_SIZE);
    }
    uint64_t scratchEnd = mmapAllocEnd;
    uint64_t scratchNext = scratch;

    // Fallback allocation cursor.
    uint64_t fbIdx = 0;
    uint64_t fbNext = 0;

    auto pageOverlapsReserved = [&](uint64_t p) -> bool {
        // [start,end) overlap tests.
        if (p >= kernelPhys && p < kernelEnd)
            return true;
        if (p >= stackPhys && p < stackEnd)
            return true;
        // Avoid overwriting pages already consumed by our scratch allocator.
        // (We write page-table entries there; overwriting would break CR3.)
        if (p >= scratch && p < scratchNext)
            return true;
        return false;
    };

    auto allocPage = [&]() -> uint64_t {
        // Scratch path first.
        while (scratchNext + PAGE_SIZE <= scratchEnd) {
            uint64_t p = scratchNext;
            scratchNext += PAGE_SIZE;
            // If headroom estimation accidentally overlaps kernel/stack,
            // skip it so we don't corrupt live execution.
            if ((p >= kernelPhys && p < kernelEnd) || (p >= stackPhys && p < stackEnd))
                continue;

            // Zero the page so new page tables start empty.
            auto* page = reinterpret_cast<uint64_t*>(p);
            for (uint64_t i = 0; i < 512; ++i)
                page[i] = 0;
            return p;
        }

        // Fallback path from cached conventional pages.
        while (fbIdx < rangeCount) {
            if (fbNext == 0)
                fbNext = ranges[fbIdx].start;

            while (fbNext < ranges[fbIdx].end) {
                uint64_t p = fbNext;
                fbNext += PAGE_SIZE;
                if (pageOverlapsReserved(p))
                    continue;

                auto* page = reinterpret_cast<uint64_t*>(p);
                for (uint64_t i = 0; i < 512; ++i)
                    page[i] = 0;
                return p;
            }

            fbIdx++;
            fbNext = 0;
        }

        return 0;
    };

    uint64_t pml4Phys = allocPage();
    if (pml4Phys == 0) {
        write_stage1_fail_code('B');
        for (;;) {
            asm volatile("cli; hlt");
        }
    }

    auto mapPageDual = [&](uint64_t vaddrBase, uint64_t paddrBase, uint64_t pageFlags) {
        // vaddrBase and paddrBase must be page-aligned.
        const uint64_t v = vaddrBase;
        const uint64_t p = paddrBase;

        uint64_t& pml4e = reinterpret_cast<uint64_t*>(pml4Phys)[idxPml4(v)];
        uint64_t pdptPhys = pml4e & ~0xfffull;
        if ((pml4e & PTE_PRESENT) == 0) {
            pdptPhys = allocPage();
            if (pdptPhys == 0) {
                write_stage1_fail_code('C');
                for (;;) {
                    asm volatile("cli; hlt");
                }
            }
            pml4e = makeEntryAddr(pdptPhys) | PTE_PRESENT | PTE_WRITABLE;
        }

        uint64_t& pdpte = reinterpret_cast<uint64_t*>(pdptPhys)[idxPdpt(v)];
        uint64_t pdPhys = pdpte & ~0xfffull;
        if ((pdpte & PTE_PRESENT) == 0) {
            pdPhys = allocPage();
            if (pdPhys == 0) {
                write_stage1_fail_code('D');
                for (;;) {
                    asm volatile("cli; hlt");
                }
            }
            pdpte = makeEntryAddr(pdPhys) | PTE_PRESENT | PTE_WRITABLE;
        }

        uint64_t& pde = reinterpret_cast<uint64_t*>(pdPhys)[idxPd(v)];
        uint64_t ptPhys = pde & ~0xfffull;
        if ((pde & PTE_PRESENT) == 0) {
            ptPhys = allocPage();
            if (ptPhys == 0) {
                write_stage1_fail_code('E');
                for (;;) {
                    asm volatile("cli; hlt");
                }
            }
            pde = makeEntryAddr(ptPhys) | PTE_PRESENT | PTE_WRITABLE;
        }

        uint64_t& pte = reinterpret_cast<uint64_t*>(ptPhys)[idxPt(v)];
        pte = makeEntryAddr(p) | PTE_PRESENT | (pageFlags & PTE_WRITABLE);
    };

    auto mapRangeDual = [&](uint64_t vStart, uint64_t pStart, uint64_t byteSize) {
        uint64_t pages = (byteSize + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t i = 0; i < pages; ++i) {
            uint64_t v = vStart + i * PAGE_SIZE;
            uint64_t p = pStart + i * PAGE_SIZE;
            mapPageDual(v, p, PTE_WRITABLE);
        }
    };

    // Map kernel + stack + mmap buffer with dual identity + upper-half alias.
    mapRangeDual(kernelPhys, kernelPhys, kernelSizeBytes);
    mapRangeDual(stackPhys, stackPhys, stackSizeBytes);
    // Map only the first page of the mmap buffer for the verification touch.
    mapRangeDual(mmapTouchPhys, mmapTouchPhys, mmapTouchSize);

    // Upper-half alias mappings: v = p + UPPER_HALF.
    auto mapUpperHalfAliases = [&]() {
        uint64_t kPagesBytes = kernelSizeBytes;
        uint64_t sPagesBytes = stackSizeBytes;
        uint64_t mPagesBytes = mmapTouchSize;
        for (uint64_t off = 0; off < kPagesBytes; off += PAGE_SIZE)
            mapPageDual(kernelPhys + UPPER_HALF + off, kernelPhys + off, PTE_WRITABLE);
        for (uint64_t off = 0; off < sPagesBytes; off += PAGE_SIZE)
            mapPageDual(stackPhys + UPPER_HALF + off, stackPhys + off, PTE_WRITABLE);
        for (uint64_t off = 0; off < mPagesBytes; off += PAGE_SIZE)
            mapPageDual(mmapTouchPhys + UPPER_HALF + off, mmapTouchPhys + off, PTE_WRITABLE);
    };

    mapUpperHalfAliases();

    // Switch CR3 to activate our new page tables.
    // We allocated it earlier, before we started filling mappings.
    asm volatile("mov %0, %%cr3" : : "r"(pml4Phys) : "memory");
    write_stage1_touch_memory();

    // Touch memory through the upper-half alias of the mmap buffer.
    volatile uint64_t* testPtr = reinterpret_cast<volatile uint64_t*>(mmapPhys + UPPER_HALF);
    uint64_t old = *testPtr;
    *testPtr = old ^ 0xa5a5a5a5a5a5a5a5ull;
    (void)*testPtr;
    *testPtr = old;

    write_stage1_done();

    // Stage 2: install IDT, trigger int3 (#BP), return via iretq from stub.
    //
    // The ELF is linked at VMA 0 but UEFI loads the raw blob at `kernelPhys`.
    // Identity mapping uses linear addr == physical == kernelPhys + link_offset.
    // IDTR and gate offsets must use that linear address, not bare link symbols.
    auto kLinear = [&](const void* p) -> uint64_t {
        return kernelPhys + static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
    };

    uint16_t csSel = 0;
    asm volatile("mov %%cs, %0" : "=r"(csSel));

    asm volatile("cli" ::: "memory");

    idtClearAll();
    constexpr uint8_t kVectorBreakpoint = 3;
    constexpr uint8_t kIdtTypeTrap64 = 0x8F;
    uint64_t bpHandler = kLinear(reinterpret_cast<const void*>(&isr_breakpoint));
    idtSetGate(kVectorBreakpoint, bpHandler, csSel, kIdtTypeTrap64);
    idtLoad(kLinear(static_cast<const void*>(&g_idt[0])), static_cast<uint16_t>(sizeof(g_idt) - 1));

    write_stage2_install_idt();
    write_stage2_int3_marker();
    asm volatile("int3");
    write_stage2_done();

    for (;;) {
        asm volatile("cli; hlt");
    }
}
