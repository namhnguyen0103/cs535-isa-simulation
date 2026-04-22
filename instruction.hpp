#pragma once

#include <vector>
#include <string>
#include <stdexcept>

class DRAM;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
constexpr int NUM_REGS  = 32;
constexpr int REG_ZERO  =  0;
constexpr int REG_RA    =  1;
constexpr int REG_SP    =  2;
constexpr int REG_FLAGS = 31;

constexpr int FLAG_ZERO     = (1 << 0);
constexpr int FLAG_NEGATIVE = (1 << 1);
constexpr int FLAG_OVERFLOW = (1 << 2);
constexpr int FLAG_DIVZERO  = (1 << 3);

// PROGRAM_BASE: instructions start at word address 0.
// DATA_BASE: data starts at word address 16.
//   The demo program has 12 instructions (3 DRAM lines of 4 words).
//   Addresses 12-15 are unused padding.  Address 16 is the first data word.
//   With the default 32-line DRAM (128 words total), address 16 is well
//   within range and will never wrap onto the instruction area.
constexpr int PROGRAM_BASE = 0;
constexpr int DATA_BASE    = 16;

// ---------------------------------------------------------------------------
// Opcodes
// ---------------------------------------------------------------------------
enum class Opcode {
    ADD   =  0, SUB   =  1, MUL  =  2, DIV  =  3,
    AND   =  4, OR    =  5, XOR  =  6, NOT  =  7,
    SLL   =  8, SLA   =  9, SRL  = 10, SRA  = 11,
    CMPLT = 12, CMPGT = 13, CMPEQ= 14,
    ADDI  = 15, SUBI  = 16, MULI = 17, DIVI = 18,
    LOAD  = 19, STORE = 20,
    BEQ   = 21, BEQI  = 22,
    J     = 23, JR    = 24,
    JAL   = 25, JALR  = 26,
    NOP   = 27, HALT  = 28,
};

// ---------------------------------------------------------------------------
// Instruction struct
// ---------------------------------------------------------------------------
struct Instruction {
    Opcode op  = Opcode::NOP;
    int    rd  = 0;
    int    rs1 = 0;
    int    rs2 = 0;
    int    imm = 0;
    std::string label;
};

inline Instruction makeNop() {
    return { Opcode::NOP, 0, 0, 0, 0, "NOP" };
}

// ---------------------------------------------------------------------------
// Sign extension helper
// ---------------------------------------------------------------------------
inline int signExtend(int v, int bits) {
    int shift = 32 - bits;
    return (v << shift) >> shift;
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------
inline int encode(const Instruction& i) {
    int op = static_cast<int>(i.op);
    int w  = 0;

    if (i.op == Opcode::NOP)  return 0;
    if (i.op == Opcode::HALT) return (0b11 << 30) | (0xF << 26);

    if (op <= 14) {
        // R-type
        w |= (0b00 << 30) | ((op & 0xF) << 26)
           | ((i.rd  & 0x1F) << 21) | ((i.rs1 & 0x1F) << 16) | ((i.rs2 & 0x1F) << 11);
        return w;
    }
    if (op <= 18) {
        // I-type
        w |= (0b01 << 30) | (((op-15) & 0xF) << 26)
           | ((i.rd  & 0x1F) << 21) | ((i.rs1 & 0x1F) << 16) | (i.imm & 0xFFFF);
        return w;
    }
    if (op <= 20) {
        // M-type
        int field = op - 19;
        w |= (0b10 << 30) | ((field & 0x3) << 28);
        if (i.op == Opcode::LOAD) {
            w |= ((i.rd  & 0x1F) << 23) | ((i.rs1 & 0x1F) << 18);
        } else {
            w |= ((i.rs2 & 0x1F) << 23) | ((i.rs1 & 0x1F) << 18);
        }
        w |= (i.imm & 0x3FFFF);
        return w;
    }

    // Control
    int field = op - 21;
    w |= (0b11 << 30) | ((field & 0xF) << 26);
    switch (i.op) {
        case Opcode::BEQ:
            w |= ((i.rs1 & 0x1F) << 21) | ((i.rs2 & 0x1F) << 16) | (i.imm & 0xFFFF); break;
        case Opcode::BEQI:
            w |= ((i.rs1 & 0x1F) << 21) | ((i.rs2 & 0x1F) << 16) | ((i.rd & 0x1F) << 11); break;
        case Opcode::J: case Opcode::JAL:
            w |= ((i.imm & 0x1FFFFF) << 5); break;
        case Opcode::JR: case Opcode::JALR:
            w |= ((i.rs1 & 0x1F) << 21) | ((i.imm & 0xFFFF) << 5); break;
        default: break;
    }
    return w;
}

// ---------------------------------------------------------------------------
// Label reconstruction
// ---------------------------------------------------------------------------
inline std::string makeLabel(const Instruction& i) {
    auto r = [](int reg){ return "r" + std::to_string(reg); };
    switch (i.op) {
        case Opcode::ADD:   return "ADD "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::SUB:   return "SUB "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::MUL:   return "MUL "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::DIV:   return "DIV "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::AND:   return "AND "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::OR:    return "OR "   +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::XOR:   return "XOR "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::NOT:   return "NOT "  +r(i.rd)+","+r(i.rs1);
        case Opcode::SLL:   return "SLL "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::SLA:   return "SLA "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::SRL:   return "SRL "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::SRA:   return "SRA "  +r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::CMPLT: return "CMPLT "+r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::CMPGT: return "CMPGT "+r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::CMPEQ: return "CMPEQ "+r(i.rd)+","+r(i.rs1)+","+r(i.rs2);
        case Opcode::ADDI:  return "ADDI " +r(i.rd)+","+r(i.rs1)+","+std::to_string(i.imm);
        case Opcode::SUBI:  return "SUBI " +r(i.rd)+","+r(i.rs1)+","+std::to_string(i.imm);
        case Opcode::MULI:  return "MULI " +r(i.rd)+","+r(i.rs1)+","+std::to_string(i.imm);
        case Opcode::DIVI:  return "DIVI " +r(i.rd)+","+r(i.rs1)+","+std::to_string(i.imm);
        case Opcode::LOAD:  return "LOAD " +r(i.rd)+","+r(i.rs1)+","+std::to_string(i.imm);
        case Opcode::STORE: return "STORE "+r(i.rs2)+","+r(i.rs1)+","+std::to_string(i.imm);
        case Opcode::BEQ:   return "BEQ "  +r(i.rs1)+","+r(i.rs2)+","+std::to_string(i.imm);
        case Opcode::BEQI:  return "BEQI " +r(i.rs1)+","+r(i.rs2)+","+r(i.rd);
        case Opcode::J:     return "J "    +std::to_string(i.imm);
        case Opcode::JR:    return "JR "   +r(i.rs1)+","+std::to_string(i.imm);
        case Opcode::JAL:   return "JAL "  +std::to_string(i.imm);
        case Opcode::JALR:  return "JALR " +r(i.rs1)+","+std::to_string(i.imm);
        case Opcode::NOP:   return "NOP";
        case Opcode::HALT:  return "HALT";
        default:            return "???";
    }
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------
inline Instruction decode(int word) {
    if (word == 0) return { Opcode::NOP, 0, 0, 0, 0, "NOP" };

    Instruction i = {};
    int typeCode  = (word >> 30) & 0x3;

    if (typeCode == 0b00) {
        int opField = (word >> 26) & 0xF;
        i.op  = static_cast<Opcode>(opField);
        i.rd  = (word >> 21) & 0x1F;
        i.rs1 = (word >> 16) & 0x1F;
        i.rs2 = (word >> 11) & 0x1F;

    } else if (typeCode == 0b01) {
        int opField = (word >> 26) & 0xF;
        i.op  = static_cast<Opcode>(opField + 15);
        i.rd  = (word >> 21) & 0x1F;
        i.rs1 = (word >> 16) & 0x1F;
        i.imm = signExtend(word & 0xFFFF, 16);

    } else if (typeCode == 0b10) {
        int opField = (word >> 28) & 0x3;
        int rt      = (word >> 23) & 0x1F;
        int base    = (word >> 18) & 0x1F;
        int off18   = signExtend(word & 0x3FFFF, 18);
        if (opField == 0) { i.op = Opcode::LOAD;  i.rd  = rt; i.rs1 = base; i.rs2 = 0; }
        else               { i.op = Opcode::STORE; i.rd  = 0;  i.rs1 = base; i.rs2 = rt; }
        i.imm = off18;

    } else {
        int opField = (word >> 26) & 0xF;
        if (opField == 0xF) {
            i.op = Opcode::HALT;
        } else {
            switch (opField) {
                case 0: i.op=Opcode::BEQ;  i.rs1=(word>>21)&0x1F; i.rs2=(word>>16)&0x1F; i.imm=signExtend(word&0xFFFF,16); break;
                case 1: i.op=Opcode::BEQI; i.rs1=(word>>21)&0x1F; i.rs2=(word>>16)&0x1F; i.rd=(word>>11)&0x1F; break;
                case 2: i.op=Opcode::J;    i.imm=signExtend((word>>5)&0x1FFFFF,21); break;
                case 3: i.op=Opcode::JR;   i.rs1=(word>>21)&0x1F; i.imm=signExtend((word>>5)&0xFFFF,16); break;
                case 4: i.op=Opcode::JAL;  i.rd=REG_RA; i.imm=signExtend((word>>5)&0x1FFFFF,21); break;
                case 5: i.op=Opcode::JALR; i.rd=REG_RA; i.rs1=(word>>21)&0x1F; i.imm=signExtend((word>>5)&0xFFFF,16); break;
                default: i.op=Opcode::NOP; break;
            }
        }
    }

    i.label = makeLabel(i);
    return i;
}

// ---------------------------------------------------------------------------
// Load program into DRAM
// ---------------------------------------------------------------------------
#include "dram.hpp"

inline void loadProgramToDRAM(const std::vector<Instruction>& program,
                               DRAM& dram, int baseAddress) {
    int lineSize = dram.getLineSize();
    int n        = static_cast<int>(program.size());
    int numLines = (n + lineSize - 1) / lineSize;
    for (int i = 0; i < numLines; ++i) {
        DRAM::Line line(lineSize, 0);
        for (int j = 0; j < lineSize; ++j) {
            int idx = i * lineSize + j;
            if (idx < n) line[j] = encode(program[idx]);
        }
        dram.setLineDirect(baseAddress + i * lineSize, line);
    }
}

// ---------------------------------------------------------------------------
// Demo program
//
// Memory layout with default config (32 lines × 4 words = 128 words):
//   Addresses  0-11  : 12 instructions (lines 0-2)
//   Addresses 12-15  : padding (line 3)
//   Addresses 16-19  : data array[0..3]  (line 4)  ← DATA_BASE = 16
//
// The program and data are in completely separate DRAM lines and will never
// collide, regardless of cache line mapping.
// ---------------------------------------------------------------------------
inline std::vector<Instruction> makeDemoProgram() {
    return {
        // PC 0: r3 = DATA_BASE (16) — base address of the data array
        { Opcode::ADDI,  3, 0, 0, DATA_BASE, "ADDI r3,r0," + std::to_string(DATA_BASE) },
        // PC 1: r4 = 4 — loop limit
        { Opcode::ADDI,  4, 0, 0, 4,          "ADDI r4,r0,4" },
        // PC 2: r5 = 0 — loop counter i
        { Opcode::ADDI,  5, 0, 0, 0,          "ADDI r5,r0,0" },
        // PC 3: r6 = r3 + r5 — element address  (loop start)
        { Opcode::ADD,   6, 3, 5, 0,           "ADD r6,r3,r5" },
        // PC 4: r7 = mem[r6]
        { Opcode::LOAD,  7, 6, 0, 0,           "LOAD r7,r6,0" },
        // PC 5: r7 = r7 + 1
        { Opcode::ADDI,  7, 7, 0, 1,           "ADDI r7,r7,1" },
        // PC 6: r6 = r3 + r5 — recompute address for store
        { Opcode::ADD,   6, 3, 5, 0,           "ADD r6,r3,r5" },
        // PC 7: mem[r6] = r7
        { Opcode::STORE, 0, 6, 7, 0,           "STORE r7,r6,0" },
        // PC 8: r5 = r5 + 1
        { Opcode::ADDI,  5, 5, 0, 1,           "ADDI r5,r5,1" },
        // PC 9: if r5 == r4 skip next (branch to HALT)
        //       BEQ offset = +1 → target = 9+1+1 = 11 (HALT) ✓
        { Opcode::BEQ,   0, 5, 4, 1,           "BEQ r5,r4,1" },
        // PC 10: jump back to loop start (PC 3)
        //        J offset = -8 → target = 10+1+(-8) = 3 ✓
        { Opcode::J,     0, 0, 0, -8,          "J -8" },
        // PC 11: halt
        { Opcode::HALT,  0, 0, 0, 0,           "HALT" },
    };
}
