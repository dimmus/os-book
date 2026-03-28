#include <coroutine>
#include <iostream>

// A tiny coroutine-based Task. This is NOT Skift's Task type;
// it is a host analog so students can feel the control flow of coroutines.
template <class T>
struct Task {
    struct promise_type {
        T value{};
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T v) noexcept { value = v; }
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h{};
    explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
    Task(Task&& o) noexcept : h(o.h) { o.h = {}; }
    ~Task() {
        if (h)
            h.destroy();
    }

    T run() {
        h.resume(); // executes until final_suspend
        return h.promise().value;
    }
};

Task<int> compute() { co_return 21 * 2; }

int main() { std::cout << compute().run() << "\n"; }

