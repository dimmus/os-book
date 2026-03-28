#include <cstdint>
#include <iostream>
#include <vector>

enum Sig : std::uint32_t { READABLE = 1u << 0, WRITABLE = 1u << 1 };

struct Pipe {
    std::size_t cap;
    std::vector<std::uint8_t> bytes;
    explicit Pipe(std::size_t c) : cap(c) {}
    std::uint32_t poll() const {
        std::uint32_t s = 0;
        if (!bytes.empty()) s |= READABLE;
        if (bytes.size() < cap) s |= WRITABLE;
        return s;
    }
    void write(std::uint8_t v) { bytes.push_back(v); }
    std::uint8_t read() {
        auto v = bytes.front();
        bytes.erase(bytes.begin());
        return v;
    }
};

int main() {
    Pipe p{4};
    for (int tick = 0; tick < 5; ++tick) {
        if (p.poll() & READABLE)
            std::cout << "wake READABLE at tick=" << tick << "\n";
        if (tick == 2)
            p.write(9), p.write(8);
    }
    std::cout << "read=" << int(p.read()) << "," << int(p.read()) << "\n";
}

