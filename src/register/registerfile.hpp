#pragma once

#include "../instruction/instruction.hpp"

#include <array>
#include <stdexcept>
#include <iostream>
#include <iomanip>

class RegisterFile {
public:
    RegisterFile() {
        regs_.fill(0);
    }

    // Read a register value. r0 always returns 0.
    int read(int reg) const {
        checkBounds(reg);
        if (reg == 0) return 0;
        return regs_[reg];
    }

    // Write a register value. Writes to r0 are silently ignored.
    void write(int reg, int value) {
        checkBounds(reg);
        if (reg == 0) return;
        regs_[reg] = value;
    }

    // Print all non-zero registers (plus r0 always)
    void dump() const {
        std::cout << "Register file:\n";
        for (int i = 0; i < NUM_REGS; ++i) {
            if (i == 0 || regs_[i] != 0) {
                std::cout << "  r" << std::setw(2) << i
                          << " = " << regs_[i] << "\n";
            }
        }
    }

    // Print a specific range of registers
    void dump(int start, int end) const {
        if (start < 0 || end >= NUM_REGS || end < start) {
            std::cout << "Invalid register range\n";
            return;
        }
        std::cout << "Register file:\n";
        for (int i = start; i <= end; ++i) {
            std::cout << "  r" << std::setw(2) << i
                      << " = " << regs_[i] << "\n";
        }
    }

private:
    std::array<int, NUM_REGS> regs_;

    void checkBounds(int reg) const {
        if (reg < 0 || reg >= NUM_REGS) {
            throw std::out_of_range("Register index out of range: " +
                                    std::to_string(reg));
        }
    }
};