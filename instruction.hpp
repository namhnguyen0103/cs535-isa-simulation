#pragma once

#include <vector>
#include <string>

class DRAM;  // forward declaration

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
constexpr int NUM_REGS  = 32;
constexpr int REG_ZERO  =  0;   // hardwired 0
constexpr int REG_RA    =  1;   // return address (JAL/JALR write here)
constexpr int REG_SP    =  2;   // stack pointer (by convention)
constexpr int REG_FLAGS = 31;   // status bits [3:0]

constexpr int FLAG_ZERO     = (1 << 0);
constexpr int FLAG_NEGATIVE = (1 << 1);
constexpr int FLAG_OVERFLOW = (1 << 2);
constexpr int FLAG_DIVZERO  = (1 << 3);

// Instructions are written to DRAM starting at address 0.
// Data should start at DATA_BASE, safely after the instruction area.
// With the default 64-line x 4-word DRAM (256 words), DATA_BASE=64 is
// safe for programs up to 64 instructions.
constexpr int PROGRAM_BASE = 0;
constexpr int DATA_BASE    = 64;

// ---------------------------------------------------------------------------
// Opcodes
// ---------------------------------------------------------------------------
enum class Opcode {
    // R-type (type code 00), opcode field 0–14
    ADD   =  0, SUB   =  1, MUL  =  2, DIV  =  3,
    AND   =  4, OR    =  5, XOR  =  6, NOT  =  7,
    SLL   =  8, SLA   =  9, SRL  = 10, SRA  = 11,
    CMPLT = 12, CMPGT = 13, CMPEQ= 14,

    // I-type (type code 01), opcode field 0–3
    ADDI  = 15, SUBI  = 16, MULI = 17, DIVI = 18,

    // M-type (type code 10), opcode field 0–1
    LOAD  = 19, STORE = 20,

    // Control (type code 11), opcode field 0–5
    BEQ   = 21, BEQI  = 22,
    J     = 23, JR    = 24,
    JAL   = 25, JALR  = 26,

    // Special
    NOP   = 27,
    HALT  = 28,
};

// ---------------------------------------------------------------------------
// Instruction struct
//
// Field mapping by type:
//   R-type   rd=dest, rs1=src1, rs2=src2, imm=0
//   I-type   rd=dest, rs1=src1, rs2=0,    imm=imm16
//   M-LOAD   rd=rt,   rs1=base, rs2=0,    imm=off18
//   M-STORE  rd=0,    rs1=base, rs2=rt,   imm=off18  (rt is the value)
//   BEQ      rd=0,    rs1,      rs2,      imm=off16
//   BEQI     rd=rTgt, rs1,      rs2,      imm=0
//   J/JAL    rd=0/r1, rs1=0,    rs2=0,    imm=off21
//   JR/JALR  rd=0/r1, rs1,      rs2=0,    imm=imm16
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

// Sign-extend n-bit value stored in low n bits of v
inline int signExtend(int v, int bits) {
    int shift = 32 - bits;
    return (v << shift) >> shift;
}

// ---------------------------------------------------------------------------
// Encoding — packs an Instruction into a 32-bit word
//
//   R  [31:30=00][29:26=op4][25:21=rd][20:16=rs1][15:11=rs2][10:0=0]
//   I  [31:30=01][29:26=op4][25:21=rd][20:16=rs1][15:0=imm16]
//   M  [31:30=10][29:28=op2][27:23=rt][22:18=base][17:0=off18]
//   B  [31:30=11][29:26=op4][25:21=rs1][20:16=rs2][15:0=off16]
//   J  [31:30=11][29:26=op4][25:5=off21][4:0=0]
//   JR [31:30=11][29:26=op4][25:21=rs1][20:5=imm16][4:0=0]
// ---------------------------------------------------------------------------
inline int encode(const Instruction& i) {
    int op = static_cast<int>(i.op);
    int w  = 0;

    if (i.op == Opcode::NOP)  return 0;
    if (i.op == Opcode::HALT) return (0b11 << 30) | (0xF << 26);

    if (op <= 14) {  // R-type
        w = (0b00 << 30) | ((op & 0xF) << 26)
          | ((i.rd  & 0x1F) << 21) | ((i.rs1 & 0x1F) << 16) | ((i.rs2 & 0x1F) << 11);
        return w;
    }
    if (op <= 18) {  // I-type
        w = (0b01 << 30) | (((op-15) & 0xF) << 26)
          | ((i.rd  & 0x1F) << 21) | ((i.rs1 & 0x1F) << 16) | (i.imm & 0xFFFF);
        return w;
    }
    if (op <= 20) {  // M-type
        w = (0b10 << 30) | (((op-19) & 0x3) << 28);
        if (i.op == Opcode::LOAD)
            w |= ((i.rd  & 0x1F) << 23) | ((i.rs1 & 0x1F) << 18);
        else
            w |= ((i.rs2 & 0x1F) << 23) | ((i.rs1 & 0x1F) << 18);
        w |= (i.imm & 0x3FFFF);
        return w;
    }
    // Control
    w = (0b11 << 30) | (((op-21) & 0xF) << 26);
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
// Label reconstruction for display after decode
// ---------------------------------------------------------------------------
inline std::string makeLabel(const Instruction& i) {
    auto r = [](int x){ return "r" + std::to_string(x); };
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
// Decoding — unpacks a 32-bit word back into an Instruction
// ---------------------------------------------------------------------------
inline Instruction decode(int word) {
    if (word == 0) return { Opcode::NOP, 0, 0, 0, 0, "NOP" };

    Instruction i = {};
    int tc = (word >> 30) & 0x3;

    if (tc == 0b00) {
        i.op  = static_cast<Opcode>((word >> 26) & 0xF);
        i.rd  = (word >> 21) & 0x1F;
        i.rs1 = (word >> 16) & 0x1F;
        i.rs2 = (word >> 11) & 0x1F;
    } else if (tc == 0b01) {
        i.op  = static_cast<Opcode>(((word >> 26) & 0xF) + 15);
        i.rd  = (word >> 21) & 0x1F;
        i.rs1 = (word >> 16) & 0x1F;
        i.imm = signExtend(word & 0xFFFF, 16);
    } else if (tc == 0b10) {
        int rt   = (word >> 23) & 0x1F;
        int base = (word >> 18) & 0x1F;
        int off  = signExtend(word & 0x3FFFF, 18);
        if (((word >> 28) & 0x3) == 0) {
            i.op = Opcode::LOAD;  i.rd  = rt; i.rs1 = base;
        } else {
            i.op = Opcode::STORE; i.rd  = 0;  i.rs1 = base; i.rs2 = rt;
        }
        i.imm = off;
    } else {
        int op = (word >> 26) & 0xF;
        if (op == 0xF) { i.op = Opcode::HALT; }
        else switch (op) {
            case 0: i.op=Opcode::BEQ;  i.rs1=(word>>21)&0x1F; i.rs2=(word>>16)&0x1F; i.imm=signExtend(word&0xFFFF,16); break;
            case 1: i.op=Opcode::BEQI; i.rs1=(word>>21)&0x1F; i.rs2=(word>>16)&0x1F; i.rd=(word>>11)&0x1F; break;
            case 2: i.op=Opcode::J;    i.imm=signExtend((word>>5)&0x1FFFFF,21); break;
            case 3: i.op=Opcode::JR;   i.rs1=(word>>21)&0x1F; i.imm=signExtend((word>>5)&0xFFFF,16); break;
            case 4: i.op=Opcode::JAL;  i.rd=REG_RA; i.imm=signExtend((word>>5)&0x1FFFFF,21); break;
            case 5: i.op=Opcode::JALR; i.rd=REG_RA; i.rs1=(word>>21)&0x1F; i.imm=signExtend((word>>5)&0xFFFF,16); break;
            default: i.op=Opcode::NOP; break;
        }
    }
    i.label = makeLabel(i);
    return i;
}

// ---------------------------------------------------------------------------
// loadProgramToDRAM — encode instructions and write to DRAM starting at base
// ---------------------------------------------------------------------------
#include "dram.hpp"

inline void loadProgramToDRAM(const std::vector<Instruction>& program,
                               DRAM& dram, int baseAddress) {
    int ls = dram.getLineSize();
    int n  = (int)program.size();
    int nl = (n + ls - 1) / ls;
    for (int i = 0; i < nl; ++i) {
        DRAM::Line line(ls, 0);
        for (int j = 0; j < ls; ++j) {
            int idx = i * ls + j;
            if (idx < n) line[j] = encode(program[idx]);
        }
        dram.setLineDirect(baseAddress + i * ls, line);
    }
}
