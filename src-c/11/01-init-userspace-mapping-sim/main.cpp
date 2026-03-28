#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

static constexpr std::size_t PAGE = 4096;

std::size_t alignUp(std::size_t x) { return (x + PAGE - 1) / PAGE * PAGE; }

struct Prog {
    std::size_t vaddr;
    std::size_t memsz;
    std::size_t filez;
    bool writeable;
};

int main() {
    // Simulate `_locateInit`: init ELF is inside bootfs record.
    std::size_t bootfsRecordStart = 0x1000000;
    std::size_t direntOffset = 0x800;
    std::size_t initLen = 6000;

    std::size_t elfStart = bootfsRecordStart + direntOffset;
    std::size_t elfLenAligned = alignUp(initLen);
    std::cout << "bootfs->elf start=0x" << std::hex << elfStart << " len=" << std::dec << elfLenAligned << "\n";

    std::vector<Prog> progs = {
        {0x400000, 9000, 3000, false},
        {0x401000, 6000, 6000, true},
    };

    for (auto const& p : progs) {
        std::size_t size = alignUp(std::max(p.memsz, p.filez));
        std::cout << (p.writeable ? "map RW " : "map RX ")
                  << "vaddr=0x" << std::hex << p.vaddr << " size=" << std::dec << size << "\n";
    }

    std::size_t stackSize = 64 * 1024;
    std::size_t entry = 0x400123;
    std::size_t handoverBase = 0xFFFF0000;
    std::cout << "task.ready entry=0x" << std::hex << entry << " stack=" << std::dec << stackSize
              << " handoverArg=0x" << std::hex << handoverBase << "\n";
}

