#pragma once

#include "instruction.hpp"
#include <array>
#include <stdexcept>
#include <iostream>
#include <iomanip>

class RegisterFile {
public:
    RegisterFile() { regs_.fill(0); }

    // r0 is hardwired to 0 — reads always return 0, writes are silently ignored
    int read(int reg) const {
        checkBounds(reg);
        return (reg == 0) ? 0 : regs_[reg];
    }
    void write(int reg, int value) {
        checkBounds(reg);
        if (reg != 0) regs_[reg] = value;
    }

    void dump() const {
        for (int i = 0; i < NUM_REGS; ++i)
            if (i == 0 || regs_[i] != 0)
                std::cout << "  r" << std::setw(2) << i << " = " << regs_[i] << "\n";
    }

private:
    std::array<int, NUM_REGS> regs_;
    void checkBounds(int r) const {
        if (r < 0 || r >= NUM_REGS)
            throw std::out_of_range("register index out of range: " + std::to_string(r));
    }
};
