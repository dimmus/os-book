#include <cstdint>
#include <iostream>
#include <vector>

static constexpr std::size_t PAGE = 4096;
static constexpr std::size_t KERNEL_BASE = 0x100000;

std::size_t pageAlignUp(std::size_t x) {
    return (x + PAGE - 1) / PAGE * PAGE;
}

int main() {
    std::size_t vaddr = KERNEL_BASE + 0x2000;
    std::size_t filez = 3;
    std::size_t memsz = 5000; // includes BSS
    std::size_t paddr = vaddr - KERNEL_BASE;
    std::size_t aligned = pageAlignUp(memsz);

    std::vector<std::uint8_t> mem(aligned, 0xCC); // "existing garbage"
    std::uint8_t fileBytes[] = {1, 2, 3};
    std::copy(fileBytes, fileBytes + filez, mem.begin() + 0);
    std::fill(mem.begin() + filez, mem.begin() + memsz, 0); // BSS zeroing

    std::cout << "paddr=" << std::hex << paddr << std::dec << "\n";
    std::cout << "file[0..2]=" << int(mem[0]) << "," << int(mem[1]) << "," << int(mem[2]) << "\n";
    std::cout << "bssStart=" << int(mem[filez]) << " bssEnd=" << int(mem[memsz - 1]) << "\n";
}

