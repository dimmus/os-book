#include <iostream>
#include <string>

std::string decide(std::size_t entriesLen) {
    if (entriesLen > 1 || entriesLen == 0)
        return "menu";
    return "splash+load";
}

int main() {
    std::cout << "len=0 -> " << decide(0) << "\n";
    std::cout << "len=1 -> " << decide(1) << "\n";
    std::cout << "len=2 -> " << decide(2) << "\n";
}

