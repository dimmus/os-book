#include <cstdint>
#include <cstring>
#include <iostream>

struct Frame {
    std::uint64_t rip{};
    std::uint64_t rsp{};
};

struct Context {
    Frame saved{};
    void save(Frame const& f) { saved = f; }
    void load(Frame& f) const { f = saved; }
};

int main() {
    Context c;
    Frame f1{0x1111, 0x2222};
    c.save(f1);

    Frame f2{0, 0};
    c.load(f2);

    std::cout << std::hex << "rip=" << f2.rip << " rsp=" << f2.rsp << "\n";
    return 0;
}

