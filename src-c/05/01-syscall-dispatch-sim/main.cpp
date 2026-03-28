#include <cstdint>
#include <iostream>

enum class Syscall : std::uint64_t { NOW = 0, LOG = 1 };

struct FrameArgs {
    std::uint64_t rdi{};
    std::uint64_t rsi{};
};

std::uint64_t dispatchSyscall(Syscall id, FrameArgs a) {
    switch (id) {
    case Syscall::NOW:
        std::cout << "NOW(" << a.rdi << ")\n";
        return a.rdi; // pretend "current time"
    case Syscall::LOG:
        std::cout << "LOG(" << a.rdi << "," << a.rsi << ")\n";
        return 0;
    default:
        std::cout << "INVALID_SYSCALL\n";
        return ~0ull;
    }
}

int main() {
    FrameArgs a{10, 20};
    std::cout << "ret=" << dispatchSyscall(Syscall::NOW, a) << "\n";
    (void)dispatchSyscall(Syscall::LOG, a);
}

