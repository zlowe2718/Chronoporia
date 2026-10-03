#include <thread>
#include <cstdint>

int main() {
    using namespace std::chrono_literals;

    uint64_t i = 0;
    while (true) {
        printf("%lld\n", i);
        std::this_thread::sleep_for(1s);
        i += 1;
    }

    return 0;
}