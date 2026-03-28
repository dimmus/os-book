#include <exception>
#include <iostream>

// Teaching lab: simulate the shape of Hjert's `__panicHandler`.
// In the real kernel, fatal panic calls `Hjert::Arch::stop()` (infinite halt loop).
// Here we throw an exception so the host program can terminate in a testable way.

enum class PanicKind { DEBUG, PANIC };

struct KernelStop : std::exception {
    const char* msg;
    explicit KernelStop(const char* m) : msg(m) {}
    const char* what() const noexcept override { return msg; }
};

[[noreturn]] void stopWorld() { throw KernelStop{"kernel stop (simulated)"}; }

void __panicHandler(PanicKind kind, char const* buf) {
    if (kind == PanicKind::PANIC) {
        std::cout << "PANIC: " << buf << "\n";
        stopWorld();
    } else {
        std::cout << "DEBUG: " << buf << "\n";
    }
}

int main() {
    try {
        __panicHandler(PanicKind::DEBUG, "early log ok");
        __panicHandler(PanicKind::PANIC, "unreachable");
    } catch (KernelStop const& e) {
        std::cout << "stopped: " << e.what() << "\n";
    }
}

