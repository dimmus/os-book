#include <iostream>

// A minimal "result" type to mimic the shape of skift's `Res<T>`:
// - success: `err == nullptr`
// - failure: `err != nullptr`
// This is deliberately small so students can focus on control flow.
template <class T>
struct Res {
    T val{};               // meaningful only when success
    const char* err = 0;  // nullptr => success
    // Allow `if (res) { ... }` checks.
    // `explicit` prevents unintended implicit conversions to integer types.
    constexpr explicit operator bool() const { return err == 0; }
};

Res<int> div2(int x) {
    if (x == 0) return {0, "division undefined for x==0"};
    // We must return a `Res<int>`, not an `int`.
    // `{x / 2, 0}` means: value is `x/2`, error is null (success).
    return {x / 2, 0};
}

Res<int> pipeline(int x) {
    auto r = div2(x);      // equivalent of `try$(div2(x))` pattern
    if (!r) return r;      // early-return on failure, keep error context
    return {r.val + 1, 0}; // success path continues
}

int main() {
    auto ok = pipeline(5);
    if (ok)
        std::cout << ok.val << "\n";
    else
        std::cout << ok.err << "\n";
    auto bad = pipeline(0);
    if (bad)
        std::cout << bad.val << "\n";
    else
        std::cout << bad.err << "\n";

    auto ok2 = pipeline_try_style(8);
    if (ok2)
        std::cout << ok2.val << "\n";
    else
        std::cout << ok2.err << "\n";
}

