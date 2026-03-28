#include <concepts>
#include <cstddef>
#include <iostream>

// A host-side interface check inspired by `karm-core`'s slice/iterator style.
template <class T>
concept Sliceable = requires(T const& t) {
    typename T::Inner;
    { t.len() } -> std::convertible_to<std::size_t>;
    { t.buf() } -> std::same_as<typename T::Inner const*>;
    { t[std::size_t{0}]} -> std::same_as<typename T::Inner const&>;
};

struct SliceInt {
    using Inner = int;
    int const* _p{};
    std::size_t _n{};

    constexpr std::size_t len() const { return _n; }
    constexpr int const* buf() const { return _p; }
    constexpr int const& operator[](std::size_t i) const { return _p[i]; }
};

static_assert(Sliceable<SliceInt>);

template <Sliceable S>
int sum(S s) {
    int acc = 0;
    for (std::size_t i = 0; i < s.len(); ++i)
        acc += s[i];
    return acc;
}

int main() {
    int data[] = {1, 2, 3, 4};
    SliceInt s{data, 4};
    std::cout << sum(s) << "\n";
}

