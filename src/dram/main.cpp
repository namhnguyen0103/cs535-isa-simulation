#include "dram.hpp"

#include <iostream>

int main() {
    DRAM dram(8, 4, 3);
    // 8 lines, 4 ints per line, 3-cycle delay

    DRAM::Line lineToStore = {10, 20, 30, 40};

    std::cout << "=== Store example ===\n";
    while (true) {
        auto result = dram.store(6, 1, lineToStore);
        std::cout << "store wait = " << result.wait << "\n";
        if (!result.wait) {
            break;
        }
    }

    dram.dump();

    std::cout << "\n=== Load example ===\n";
    while (true) {
        auto result = dram.load(6, 1);
        std::cout << "load wait = " << result.wait << "\n";
        if (!result.wait) {
            std::cout << "Returned line: ";
            for (int x : result.line) {
                std::cout << x << " ";
            }
            std::cout << "\n";
            break;
        }
    }

    std::cout << "\n=== Different requester blocked example ===\n";
    // Start a request from requester 2
    auto a = dram.load(9, 2);
    std::cout << "Requester 2 first load, wait = " << a.wait << "\n";

    // Requester 3 tries while requester 2 owns DRAM
    auto b = dram.load(9, 3);
    std::cout << "Requester 3 attempt, wait = " << b.wait << "\n";

    // Continue with requester 2 until done
    while (true) {
        auto r = dram.load(9, 2);
        std::cout << "Requester 2 continuing, wait = " << r.wait << "\n";
        if (!r.wait) {
            std::cout << "Requester 2 completed load.\n";
            break;
        }
    }

    return 0;
}