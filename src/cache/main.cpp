#include "../dram/dram.hpp"
#include "cache.hpp"

#include <iostream>

int main() {
    DRAM dram(8, 4, 3);

    Cache cache(
        4,   // cache lines
        4,   // words per line
        1,   // cache delay
        &dram,
        Cache::WritePolicy::WRITE_THROUGH,
        Cache::AllocatePolicy::NO_WRITE_ALLOCATE
    );

    // Put a line directly into DRAM for testing
    DRAM::Line initial = {100, 101, 102, 103};
    while (true) {
        auto s = dram.store(4, 99, initial);
        if (!s.wait) break;
    }

    std::cout << "=== Load address 6 ===\n";
    while (true) {
        auto r = cache.load(6, 1);
        std::cout << "wait = " << r.wait << "\n";
        if (!r.wait) {
            std::cout << "value = " << r.value << "\n";
            break;
        }
    }

    cache.dump();

    std::cout << "\n=== Store address 6 = 999 ===\n";
    while (true) {
        auto s = cache.store(6, 1, 999);
        std::cout << "wait = " << s.wait << "\n";
        if (!s.wait) {
            break;
        }
    }

    cache.dump();
    std::cout << "\n";
    dram.dump();

    std::cout << "\n=== Load address 6 again ===\n";
    while (true) {
        auto r = cache.load(6, 1);
        std::cout << "wait = " << r.wait << "\n";
        if (!r.wait) {
            std::cout << "value = " << r.value << "\n";
            break;
        }
    }

    return 0;
}