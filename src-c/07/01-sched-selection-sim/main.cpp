#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

enum class State { RUNNABLE, BLOCKED, EXITED };

struct Task {
    std::string label;
    State state;
    std::uint64_t sliceEnd;
};

std::size_t chooseNext(std::uint64_t now, std::vector<Task>& tasks) {
    // Represent the `_idle` task by forcing its slice end to `now+1`.
    std::uint64_t bestEnd = now + 1;
    std::size_t best = 0;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        auto& t = tasks[i];
        if (t.state == State::EXITED) continue;
        if (t.state == State::RUNNABLE && t.sliceEnd <= bestEnd) {
            bestEnd = t.sliceEnd;
            best = i;
        }
    }
    return best;
}

int main() {
    std::uint64_t now = 100;
    std::vector<Task> tasks = {
        {"A", State::RUNNABLE, 99}, {"B", State::RUNNABLE, 120}, {"C", State::BLOCKED, 80}};
    auto pick = chooseNext(now, tasks);
    std::cout << "now=" << now << " pick=" << tasks[pick].label << " sliceEnd=" << tasks[pick].sliceEnd << "\n";
}

