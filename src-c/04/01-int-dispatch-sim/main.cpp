#include <iostream>
#include <cstdint>

struct Frame {
    std::uint64_t intNo{};
    std::uint64_t errNo{};
    std::uint64_t rip{};
    std::uint64_t rsp{};
};

void _intDispatch(Frame& f) {
    static const char* faultMsg[] = {"division-by-zero", "debug", "nmi"};

    if (f.intNo < 32) {
        const char* msg = (f.intNo < 3) ? faultMsg[f.intNo] : "cpu-fault";
        std::cout << "FAULT " << f.intNo << ": " << msg << "\n";
        return;
    }

    if (f.intNo == 100) {
        std::cout << "SCHEDULE tick (via intNo=100)\n";
        return;
    }

    auto irq = f.intNo - 32;
    std::cout << "IRQ dispatch irq=" << irq << " (intNo=" << f.intNo << ")\n";
}

int main() {
    Frame f1{0, 0, 0xDEAD, 0xBEEF};
    Frame f2{100, 0, 0, 0};
    Frame f3{33, 0, 0, 0};
    _intDispatch(f1);
    _intDispatch(f2);
    _intDispatch(f3);
}

