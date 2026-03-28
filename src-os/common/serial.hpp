#pragma once

#include <stdint.h>

// Minimal COM1 (UART16550) programming helpers.
// This header is intentionally freestanding: it does not depend on libc,
// and it only uses inline asm for port I/O.

namespace serial {

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// COM1 base port.
static constexpr uint16_t COM1_BASE = 0x3F8;

static constexpr uint16_t REG_THR = COM1_BASE + 0; // Transmitter Holding Register
static constexpr uint16_t REG_LSR = COM1_BASE + 5; // Line Status Register
static constexpr uint16_t REG_LCR = COM1_BASE + 3; // Line Control Register
static constexpr uint16_t REG_DLL = COM1_BASE + 0; // Divisor Latch (LSB)
static constexpr uint16_t REG_DLM = COM1_BASE + 1; // Divisor Latch (MSB)
static constexpr uint16_t REG_FCR = COM1_BASE + 2; // FIFO Control Register
static constexpr uint16_t REG_IER = COM1_BASE + 1; // Interrupt Enable Register (shares DLM)

// 115200 baud on the common 1.8432MHz UART clock => divisor = 16 (0x0010).
static inline void init_115200() {
    // Disable UART interrupts.
    outb(REG_IER, 0x00);

    // Enable Divisor Latch Access Bit (DLAB).
    outb(REG_LCR, 0x80);

    outb(REG_DLL, 0x10); // divisor low byte
    outb(REG_DLM, 0x00); // divisor high byte

    // 8N1 framing, clear DLAB.
    outb(REG_LCR, 0x03);

    // Enable FIFO, clear RX/TX queues.
    outb(REG_FCR, 0x07);
}

static inline void write_char(char c) {
    // Wait until THR is empty (LSR bit 5).
    while ((inb(REG_LSR) & 0x20u) == 0) {
        asm volatile("pause");
    }
    outb(REG_THR, static_cast<uint8_t>(c));
}

static inline void write_str(const char* s) {
    // Expect a NUL-terminated ASCII string.
    for (; s && *s; ++s) {
        write_char(*s);
    }
}

} // namespace serial

