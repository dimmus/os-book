// Minimal kernel after UEFI jump: hello on serial, then halt (OS-2 milestone).
// Paging and IDT come in OS-4 / OS-5.

#include "../common/serial.hpp"

static inline void write_hello_init() {
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

__attribute__((section(".text.kernel_entry")))
extern "C" void kernel_entry(
    uint64_t /*mmapPhys*/,
    uint64_t /*mmapSizeUsed*/,
    uint64_t /*descSize*/,
    uint64_t /*kernelPhys*/,
    uint64_t /*kernelPages*/,
    uint64_t /*stackPhys*/
) {
    serial::init_115200();
    write_hello_init();
    for (;;) {
        asm volatile("cli; hlt");
    }
}
