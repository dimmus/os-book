#include <cstdint>
#include <iostream>
#include <vector>

enum class Pledge : std::uint32_t { LOG = 1u << 0, MEM = 1u << 1 };

struct Cap {
    std::uint32_t idx{};
};

struct Domain {
    std::vector<int> slots; // 0 => empty, non-zero => object id

    explicit Domain(std::size_t n) : slots(n, 0) {}

    Cap add(int objId) {
        for (std::size_t i = 1; i < slots.size(); ++i)
            if (slots[i] == 0) {
                slots[i] = objId;
                return Cap{static_cast<std::uint32_t>(i)};
            }
        return Cap{0};
    }

    int get(Cap c) const { return (c.idx < slots.size()) ? slots[c.idx] : 0; }
};

bool ensure(std::uint32_t taskPledges, Pledge need) {
    auto mask = static_cast<std::uint32_t>(need);
    return (taskPledges & mask) == mask;
}

int main() {
    Domain dom(8);
    auto cap = dom.add(42);
    std::cout << "cap.idx=" << cap.idx << " obj=" << dom.get(cap) << "\n";

    std::uint32_t pledges = static_cast<std::uint32_t>(Pledge::LOG);
    std::cout << "ensure(LOG)=" << ensure(pledges, Pledge::LOG) << " ensure(MEM)="
              << ensure(pledges, Pledge::MEM) << "\n";
}

