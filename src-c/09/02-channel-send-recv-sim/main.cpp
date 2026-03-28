#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

struct Channel {
    std::size_t bufCap;
    std::size_t capsCap;
    std::size_t msgQueueCap;
    std::size_t bytesUsed = 0;
    std::size_t capsUsed = 0;
    std::size_t msgQueueUsed = 0;

    bool send(std::size_t bytesLen, std::size_t capsLen) {
        if (msgQueueUsed + 1 > msgQueueCap) return false;
        if (bytesUsed + bytesLen > bufCap) return false;
        if (capsUsed + capsLen > capsCap) return false;
        bytesUsed += bytesLen;
        capsUsed += capsLen;
        msgQueueUsed += 1;
        return true;
    }

    bool recv(std::size_t bytesCap, std::size_t capsCapReq) {
        if (msgQueueUsed == 0) return false;
        // For simulation: assume message payload uses the entire caps/bytes we ask for.
        if (bytesCap < 2) return false;
        if (capsCapReq < 1) return false;
        bytesUsed -= 2;
        capsUsed -= 1;
        msgQueueUsed -= 1;
        return true;
    }
};

int main() {
    Channel ch{8, 2, 4};
    bool s = ch.send(2, 1);
    bool r = ch.recv(2, 1);
    std::cout << std::boolalpha << "send=" << s << " recv=" << r << "\n";
}

