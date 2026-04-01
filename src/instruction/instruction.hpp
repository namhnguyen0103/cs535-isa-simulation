#pragma once

#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Registers
// ---------------------------------------------------------------------------

constexpr int NUM_REGS = 32;

// ---------------------------------------------------------------------------
// Opcodes
// ---------------------------------------------------------------------------

enum class Opcode {
    LOAD,   // rd  = mem[rs1 + imm]      (rs2 unused)
    STORE,  // mem[rs1 + imm] = rs2      (rd  unused)
    ADD,    // rd  = rs1 + rs2 + imm
    BNE,    // if rs1 != rs2, PC += imm  (rd  unused)
};

// ---------------------------------------------------------------------------
// Instruction
// ---------------------------------------------------------------------------

struct Instruction {
    Opcode op;
    int rd;    // destination register       (unused for STORE, BNE)
    int rs1;   // source register 1
    int rs2;   // source register 2          (unused for LOAD)
    int imm;   // immediate / offset / branch offset

    // Human-readable label for pipeline display
    std::string label;
};

// Represents a bubble (NOP) in the pipeline — inserted on stalls and squashes
inline Instruction makeNop() {
    return { Opcode::ADD, 0, 0, 0, 0, "NOP" };
}

// ---------------------------------------------------------------------------
// Demo program: increment array[0..3] by 1
//
// Addressing model (standard RISC base+offset):
//   LOAD  rd,  rs1, imm   →  rd  = mem[rs1 + imm]
//   STORE rs2, rs1, imm   →  mem[rs1 + imm] = rs2
//
// Because the address is rs1+imm (fixed offset only), we pre-compute
// the element address into r5 before each LOAD/STORE.
//
// Registers:
//   r1 = base address of array (0)
//   r2 = loop limit            (4)
//   r3 = loop counter i
//   r4 = loaded/incremented value
//   r5 = current element address (r1 + r3), recomputed each iteration
//
// Memory layout (word-addressed):
//   address 0 = array[0], address 1 = array[1], ...
//
// Program:
//   PC 0  ADD  r1, r0, r0, 0   r1 = 0        base address
//   PC 1  ADD  r2, r0, r0, 4   r2 = 4        loop limit
//   PC 2  ADD  r3, r0, r0, 0   r3 = 0        loop counter
//   PC 3  ADD  r5, r1, r3, 0   r5 = r1+r3    element address   <-- loop start
//   PC 4  LOAD r4, r5, r0, 0   r4 = mem[r5]
//   PC 5  ADD  r4, r4, r0, 1   r4 += 1
//   PC 6  ADD  r5, r1, r3, 0   r5 = r1+r3    recompute address
//   PC 7  STORE r4, r5, r0, 0  mem[r5] = r4
//   PC 8  ADD  r3, r3, r0, 1   r3++
//   PC 9  BNE  r3, r2, -6      if r3 != 4, jump to PC 3
// ---------------------------------------------------------------------------

inline std::vector<Instruction> makeDemoProgram() {
    return {
        // PC 0: r1 = 0  (base address of array)
        { Opcode::ADD,   1, 0, 0,  0, "ADD  r1,r0,r0,0"  },
        // PC 1: r2 = 4  (loop limit)
        { Opcode::ADD,   2, 0, 0,  4, "ADD  r2,r0,r0,4"  },
        // PC 2: r3 = 0  (loop counter)
        { Opcode::ADD,   3, 0, 0,  0, "ADD  r3,r0,r0,0"  },
        // PC 3: r5 = r1 + r3  (element address)   <-- loop start
        { Opcode::ADD,   5, 1, 3,  0, "ADD  r5,r1,r3,0"  },
        // PC 4: r4 = mem[r5 + 0]
        { Opcode::LOAD,  4, 5, 0,  0, "LOAD r4,r5,0"     },
        // PC 5: r4 = r4 + 1
        { Opcode::ADD,   4, 4, 0,  1, "ADD  r4,r4,r0,1"  },
        // PC 6: r5 = r1 + r3  (recompute address for store)
        { Opcode::ADD,   5, 1, 3,  0, "ADD  r5,r1,r3,0"  },
        // PC 7: mem[r5 + 0] = r4
        { Opcode::STORE, 0, 5, 4,  0, "STORE r4,r5,0"    },
        // PC 8: r3 = r3 + 1
        { Opcode::ADD,   3, 3, 0,  1, "ADD  r3,r3,r0,1"  },
        // PC 9: if r3 != r2, jump back to PC 3  (9 + -6 = 3)
        { Opcode::BNE,   0, 3, 2, -6, "BNE  r3,r2,-6"    },
    };
}