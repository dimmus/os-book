#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

static constexpr std::size_t PAGE = 4096;

std::size_t alignUp(std::size_t x) { return (x + PAGE - 1) / PAGE * PAGE; }

struct Range {
    std::size_t start{};
    std::size_t size{};
    bool overlaps(Range const& o) const {
        auto a2 = start + size;
        auto b2 = o.start + o.size;
        return start < b2 && o.start < a2;
    }
};

int main() {
    std::vector<Range> mapped = {{0x20000, 0x3000}}; // simulate existing map
    Range req{0x21001, 0x1000};
    req.start = alignUp(req.start);
    req.size = alignUp(req.size);

    bool ok = true;
    for (auto const& m : mapped)
        if (m.overlaps(req)) ok = false;

    std::cout << std::hex << "req.start=" << req.start << " ok=" << ok << "\n";
}

