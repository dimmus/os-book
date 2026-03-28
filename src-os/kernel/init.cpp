// Minimal kernel-mode init (os phase-0 tutorial milestone).
//
// Important for this milestone:
// - the "kernel" is loaded by our UEFI app into allocated pages
// - Stage 1: minimal paging (CR3) + upper-half verification
// - Stage 2: IDT + #BP via int3 (see OS-5-idt-study.md)
// - Stage 3: PIC remap + PIT + IRQ0 (vector 32), EOI (see OS-6-irq-pic-study.md)
// - Stage 4: mask PIC, LAPIC MMIO timer (vector 34), LAPIC EOI (see OS-7-lapic-study.md)
// - Stage 5: #PF (vector 14), error code frame, CR2 (see OS-8-page-fault-study.md)
//
// We print directly to COM1 and then halt forever.

#include "../common/serial.hpp"

#include <stdint.h>

// Default local APIC MMIO page (Intel SDM / QEMU PC). Mapped in Stage 1.
static constexpr uint64_t kLapicMmioPhys = 0xFEE00000ull;

// Deliberately unmapped VA for Stage 5 #PF probe (not covered by Stage 1 tables).
static constexpr uintptr_t kProbeUnmappedVa = 0xABC00000ull;

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

// Storage for the IDT (linked at VMA 0; the UEFI loader places the image at
// `kernelPhys`. Never dereference `g_idt` directly — use idtTable(kernelPhys).
static IdtEntry g_idt[256] __attribute__((aligned(16)));

static IdtEntry* idtTable(uint64_t kernelPhys) {
    return reinterpret_cast<IdtEntry*>(
        kernelPhys + reinterpret_cast<uintptr_t>(static_cast<void*>(&g_idt[0])));
}

static void idtClearAll(IdtEntry* t) {
    for (unsigned i = 0; i < 256; ++i) {
        t[i].offset_lo = 0;
        t[i].offset_mid = 0;
        t[i].offset_hi = 0;
        t[i].selector = 0;
        t[i].ist = 0;
        t[i].type_attr = 0;
        t[i].reserved = 0;
    }
}

static void idtSetGate(IdtEntry* t, uint8_t vector, uint64_t handlerVirt, uint16_t cs, uint8_t typeAttr) {
    IdtEntry* e = &t[vector];
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

static inline void write_stage2_begin() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('2');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('b');
    write_stage_char('e');
    write_stage_char('g');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('\n');
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

// ----------------------------
// Stage 3: PIC + PIT + timer IRQ (see OS-6-irq-pic-study.md)
// ----------------------------

volatile uint64_t g_timer_irq_count = 0;

static inline void write_stage3_begin() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('3');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('b');
    write_stage_char('e');
    write_stage_char('g');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('\n');
}

static inline void write_stage3_pic_ok() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('3');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('p');
    write_stage_char('i');
    write_stage_char('c');
    write_stage_char(' ');
    write_stage_char('o');
    write_stage_char('k');
    write_stage_char('\n');
}

static inline void write_stage3_pit_ok() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('3');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('p');
    write_stage_char('i');
    write_stage_char('t');
    write_stage_char(' ');
    write_stage_char('o');
    write_stage_char('k');
    write_stage_char('\n');
}

static inline void write_stage3_sti() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('3');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('s');
    write_stage_char('t');
    write_stage_char('i');
    write_stage_char('\n');
}

static inline void write_stage3_irq_tick(char digit) {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('3');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('i');
    write_stage_char('r');
    write_stage_char('q');
    write_stage_char(' ');
    write_stage_char(digit);
    write_stage_char('\n');
}

static inline void write_stage3_done() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('3');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('d');
    write_stage_char('o');
    write_stage_char('n');
    write_stage_char('e');
    write_stage_char('\n');
}

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline void io_wait() {
    asm volatile("jmp 1f\n1:\tjmp 1f\n1:" ::: "memory");
}

// Dual 8259 PIC: remap to vectors 0x20–0x2F / 0x28–0x2F, mask all IRQs except timer (IRQ0).
static void pic_remap_and_mask() {
    constexpr uint16_t PIC1_CMD = 0x20;
    constexpr uint16_t PIC1_DATA = 0x21;
    constexpr uint16_t PIC2_CMD = 0xA0;
    constexpr uint16_t PIC2_DATA = 0xA1;

    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();
    outb(PIC1_DATA, 1);
    io_wait();
    outb(PIC2_DATA, 1);
    io_wait();
    outb(PIC1_DATA, 0xFE);
    outb(PIC2_DATA, 0xFF);
}

static void pic_mask_all() {
    constexpr uint16_t PIC1_DATA = 0x21;
    constexpr uint16_t PIC2_DATA = 0xA1;
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

// Channel 0, square wave (~hz), for QEMU's emulated PIT.
static void pit_set_frequency(unsigned hz) {
    constexpr uint16_t PIT_CMD = 0x43;
    constexpr uint16_t PIT_CH0 = 0x40;
    constexpr uint32_t PIT_BASE_HZ = 1193182;

    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }
    if (divisor < 2) {
        divisor = 2;
    }
    outb(PIT_CMD, 0x36);
    io_wait();
    outb(PIT_CH0, static_cast<uint8_t>(divisor & 0xFF));
    io_wait();
    outb(PIT_CH0, static_cast<uint8_t>(divisor >> 8));
    io_wait();
}

extern "C" void isr_irq32(void);

extern "C" void timer_irq_dispatch(void) {
    constexpr uint16_t PIC1_CMD = 0x20;
    outb(PIC1_CMD, 0x20);
    uint64_t n = ++g_timer_irq_count;
    if (n <= 3) {
        write_stage3_irq_tick(static_cast<char>('0' + static_cast<int>(n)));
    }
}

// ----------------------------
// Stage 4: Local APIC timer (see OS-7-lapic-study.md)
// ----------------------------

volatile uint64_t g_lapic_irq_count = 0;

static inline void lapic_mmio_write(uintptr_t reg, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(kLapicMmioPhys + reg) = value;
}

static inline uint32_t lapic_mmio_read(uintptr_t reg) {
    return *reinterpret_cast<volatile uint32_t*>(kLapicMmioPhys + reg);
}

static inline void write_stage4_begin() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('4');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('b');
    write_stage_char('e');
    write_stage_char('g');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('\n');
}

static inline void write_stage4_pic_masked() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('4');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('p');
    write_stage_char('i');
    write_stage_char('c');
    write_stage_char(' ');
    write_stage_char('m');
    write_stage_char('a');
    write_stage_char('s');
    write_stage_char('k');
    write_stage_char('\n');
}

static inline void write_stage4_lapic_mmio() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('4');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('l');
    write_stage_char('a');
    write_stage_char('p');
    write_stage_char('i');
    write_stage_char('c');
    write_stage_char(' ');
    write_stage_char('m');
    write_stage_char('m');
    write_stage_char('i');
    write_stage_char('o');
    write_stage_char('\n');
}

static inline void write_stage4_svr() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('4');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('s');
    write_stage_char('v');
    write_stage_char('r');
    write_stage_char('\n');
}

static inline void write_stage4_timer_arm() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('4');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('t');
    write_stage_char('i');
    write_stage_char('m');
    write_stage_char('e');
    write_stage_char('r');
    write_stage_char('\n');
}

static inline void write_stage4_sti() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('4');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('s');
    write_stage_char('t');
    write_stage_char('i');
    write_stage_char('\n');
}

static inline void write_stage4_irq_tick(char digit) {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('4');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('i');
    write_stage_char('r');
    write_stage_char('q');
    write_stage_char(' ');
    write_stage_char(digit);
    write_stage_char('\n');
}

static inline void write_stage4_done() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('4');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('d');
    write_stage_char('o');
    write_stage_char('n');
    write_stage_char('e');
    write_stage_char('\n');
}

// LAPIC register offsets (MMIO, 32-bit aligned accesses).
static constexpr uintptr_t kLapicRegSvr = 0xF0;
static constexpr uintptr_t kLapicRegEoi = 0xB0;
static constexpr uintptr_t kLapicRegVer = 0x30;
static constexpr uintptr_t kLapicRegLvtTimer = 0x320;
static constexpr uintptr_t kLapicRegTimerInit = 0x380;
static constexpr uintptr_t kLapicRegTimerDiv = 0x3E0;

extern "C" void isr_lapic_timer(void);

extern "C" void lapic_timer_dispatch(void) {
    lapic_mmio_write(kLapicRegEoi, 0);
    uint64_t n = ++g_lapic_irq_count;
    if (n <= 3) {
        write_stage4_irq_tick(static_cast<char>('0' + static_cast<int>(n)));
    }
}

// ----------------------------
// Stage 5: page fault #PF (see OS-8-page-fault-study.md)
// ----------------------------

static inline void write_stage5_begin() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('5');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('b');
    write_stage_char('e');
    write_stage_char('g');
    write_stage_char('i');
    write_stage_char('n');
    write_stage_char('\n');
}

static inline void write_stage5_pf_gate() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('5');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('p');
    write_stage_char('f');
    write_stage_char(' ');
    write_stage_char('g');
    write_stage_char('a');
    write_stage_char('t');
    write_stage_char('e');
    write_stage_char('\n');
}

static inline void write_stage5_probe() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('5');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('p');
    write_stage_char('r');
    write_stage_char('o');
    write_stage_char('b');
    write_stage_char('e');
    write_stage_char('\n');
}

static inline void write_stage5_dispatch() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('5');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('#');
    write_stage_char('P');
    write_stage_char('F');
    write_stage_char(' ');
    write_stage_char('d');
    write_stage_char('i');
    write_stage_char('s');
    write_stage_char('p');
    write_stage_char('a');
    write_stage_char('t');
    write_stage_char('c');
    write_stage_char('h');
    write_stage_char('\n');
}

static inline void write_stage5_cr2_ok() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('5');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('c');
    write_stage_char('r');
    write_stage_char('2');
    write_stage_char(' ');
    write_stage_char('o');
    write_stage_char('k');
    write_stage_char('\n');
}

static inline void write_stage5_cr2_bad() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('5');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('c');
    write_stage_char('r');
    write_stage_char('2');
    write_stage_char(' ');
    write_stage_char('b');
    write_stage_char('a');
    write_stage_char('d');
    write_stage_char('\n');
}

static inline void write_stage5_done() {
    write_stage_char('S');
    write_stage_char('T');
    write_stage_char('A');
    write_stage_char('G');
    write_stage_char('E');
    write_stage_char(' ');
    write_stage_char('5');
    write_stage_char(':');
    write_stage_char(' ');
    write_stage_char('d');
    write_stage_char('o');
    write_stage_char('n');
    write_stage_char('e');
    write_stage_char('\n');
}

// `movb (%rax), %al` after #PF — skip this many bytes before `iretq` (see OS-8).
static constexpr uint64_t kPfProbeInsnLen = 2;

extern "C" void isr_page_fault(void);

extern "C" void page_fault_dispatch(uint64_t* frame) {
    (void)frame[0];
    write_stage5_dispatch();
    uint64_t cr2 = 0;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    if (cr2 == static_cast<uint64_t>(kProbeUnmappedVa)) {
        write_stage5_cr2_ok();
    } else {
        write_stage5_cr2_bad();
    }
    frame[1] += kPfProbeInsnLen;
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
    // Local APIC MMIO (not described as conventional RAM in the UEFI map).
    mapPageDual(kLapicMmioPhys, kLapicMmioPhys, PTE_WRITABLE);

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
        mapPageDual(kLapicMmioPhys + UPPER_HALF, kLapicMmioPhys, PTE_WRITABLE);
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

    // On #BP the CPU reloads CS from our IDT gate and reads the segment
    // descriptor from whatever GDTR UEFI installed. That table often lives in
    // physical pages we never mapped → #PF (CR2 in the firmware GDT) and #DF.
    // Install a tiny GDT on the stack (already identity-mapped) and reload
    // CS=0x08 via lretq instead of mapping firmware RAM.
    alignas(16) uint64_t bootGdt[3] = {
        0,
        0x00AF9A000000FFFFull, // 64-bit ring-0 code (long mode)
        0x00AF92000000FFFFull, // ring-0 data
    };
    struct BootGdtr {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) bootGdtr = {
        static_cast<uint16_t>(sizeof(bootGdt) - 1),
        reinterpret_cast<uint64_t>(bootGdt),
    };
    uint64_t code64Sel = 8;
    asm volatile(
        "lgdt %[gdtr]\n\t"
        "pushq %[sel]\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movl $0x10, %%eax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        :
        : [gdtr] "m"(bootGdtr), [sel] "r"(code64Sel)
        : "rax", "memory");

    // First Stage 2 line: if this is missing but Stage 1 completed, the running
    // image is almost certainly an old kernel.raw / stale BOOTX64.EFI embed.
    write_stage2_begin();

    // Stage 2: install IDT, trigger int3 (#BP), return via iretq from stub.
    //
    // The ELF is linked at VMA 0 but UEFI loads the raw blob at `kernelPhys`.
    // Identity mapping uses linear addr == physical == kernelPhys + link_offset.
    // IDTR, gate offsets, and any dereference of linked-at-0 globals must use
    // kernelPhys + symbol offset — bare link addresses (e.g. 0x20c0 for g_idt)
    // are not mapped.
    auto kLinear = [kernelPhys](const void* p) -> uint64_t {
        return kernelPhys + static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
    };

    IdtEntry* idt = idtTable(kernelPhys);

    uint16_t csSel = 0;
    asm volatile("mov %%cs, %0" : "=r"(csSel));

    asm volatile("cli" ::: "memory");

    idtClearAll(idt);
    constexpr uint8_t kVectorBreakpoint = 3;
    constexpr uint8_t kIdtTypeTrap64 = 0x8F;
    uint64_t bpHandler = kLinear(reinterpret_cast<const void*>(&isr_breakpoint));
    idtSetGate(idt, kVectorBreakpoint, bpHandler, csSel, kIdtTypeTrap64);
    idtLoad(reinterpret_cast<uint64_t>(idt), static_cast<uint16_t>(sizeof(g_idt) - 1));

    write_stage2_install_idt();
    write_stage2_int3_marker();
    asm volatile("int3" ::: "memory");
    write_stage2_done();

    // Stage 3: external interrupt path (PIC + PIT → IRQ0 → vector 32).
    write_stage3_begin();
    pic_remap_and_mask();
    write_stage3_pic_ok();
    pit_set_frequency(100);
    write_stage3_pit_ok();

    constexpr uint8_t kVectorIRQ0 = 32;
    constexpr uint8_t kIdtTypeInt64 = 0x8E;
    uint64_t irqHandler = kLinear(reinterpret_cast<const void*>(&isr_irq32));
    idtSetGate(idt, kVectorIRQ0, irqHandler, csSel, kIdtTypeInt64);
    idtLoad(reinterpret_cast<uint64_t>(idt), static_cast<uint16_t>(sizeof(g_idt) - 1));

    write_stage3_sti();
    g_timer_irq_count = 0;
    asm volatile("sti" ::: "memory");
    while (g_timer_irq_count < 3) {
        asm volatile("hlt" ::: "memory");
    }
    asm volatile("cli" ::: "memory");
    write_stage3_done();

    // Stage 4: Local APIC periodic timer; PIC fully masked (no PIT delivery).
    write_stage4_begin();
    pic_mask_all();
    write_stage4_pic_masked();

    (void)lapic_mmio_read(kLapicRegVer);
    write_stage4_lapic_mmio();

    lapic_mmio_write(kLapicRegSvr, 0x1FF);
    write_stage4_svr();

    constexpr uint32_t kLapicTimerDivide16 = 3;
    lapic_mmio_write(kLapicRegTimerDiv, kLapicTimerDivide16);

    constexpr uint8_t kVectorLapicTimer = 34;
    constexpr uint32_t kLvtTimerPeriodic = 1u << 17;
    lapic_mmio_write(kLapicRegLvtTimer, static_cast<uint32_t>(kVectorLapicTimer) | kLvtTimerPeriodic);

    constexpr uint32_t kLapicInitCount = 0x80000;
    lapic_mmio_write(kLapicRegTimerInit, kLapicInitCount);
    write_stage4_timer_arm();

    uint64_t lapicHandler = kLinear(reinterpret_cast<const void*>(&isr_lapic_timer));
    idtSetGate(idt, kVectorLapicTimer, lapicHandler, csSel, kIdtTypeInt64);
    idtLoad(reinterpret_cast<uint64_t>(idt), static_cast<uint16_t>(sizeof(g_idt) - 1));

    write_stage4_sti();
    g_lapic_irq_count = 0;
    asm volatile("sti" ::: "memory");
    while (g_lapic_irq_count < 3) {
        asm volatile("hlt" ::: "memory");
    }
    asm volatile("cli" ::: "memory");

    constexpr uint32_t kLvtMasked = 1u << 16;
    lapic_mmio_write(kLapicRegLvtTimer, kLvtMasked);

    write_stage4_done();

    // Stage 5: #PF with error code on stack; CR2 holds faulting linear address.
    write_stage5_begin();
    constexpr uint8_t kVectorPageFault = 14;
    uint64_t pfHandler = kLinear(reinterpret_cast<const void*>(&isr_page_fault));
    idtSetGate(idt, kVectorPageFault, pfHandler, csSel, kIdtTypeTrap64);
    idtLoad(reinterpret_cast<uint64_t>(idt), static_cast<uint16_t>(sizeof(g_idt) - 1));
    write_stage5_pf_gate();
    write_stage5_probe();
    {
        uintptr_t probe = kProbeUnmappedVa;
        asm volatile("movb (%%rax), %%al" : : "a"(probe) : "memory");
    }
    write_stage5_done();

    for (;;) {
        asm volatile("cli; hlt");
    }
}
