#include <iostream>
#include <string>

struct CancellationToken {
    bool cancelled = false;
};

struct SysContext {
    std::string argsHook0;
    bool handoverHook = false;
    void addArgsHook(std::string firstArg) { argsHook0 = firstArg; }
    void addHandoverHook() { handoverHook = true; }
};

int entryPointAsync(SysContext& ctx, CancellationToken const&) {
    // In real Skift, this would run the user app logic.
    if (ctx.argsHook0.empty() || !ctx.handoverHook)
        return 1; // error
    return 0; // ok
}

int userEntry(std::size_t rawHandover, std::size_t rawIn, std::size_t rawOut) {
    (void)rawHandover;
    std::cout << "Abi::SysV::init()\n";

    SysContext ctx;
    char const* argv0 = "service";
    ctx.addArgsHook(argv0);
    ctx.addHandoverHook();

    std::cout << "ipc.in=" << rawIn << " ipc.out=" << rawOut << "\n";

    CancellationToken ct;
    int res = entryPointAsync(ctx, ct);
    std::cout << "run=" << (res == 0 ? "ok" : "err") << "\n";
    return res;
}

int main() {
    return userEntry(33, 11, 22);
}

