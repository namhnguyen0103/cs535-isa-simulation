#pragma once

#include <vector>
#include <string>
#include <stdexcept>

class DRAM;  // forward declaration for loadProgramToDRAM

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
constexpr int NUM_REGS   = 32;
constexpr int REG_ZERO   =  0;   // r0  — hardwired 0
constexpr int REG_RA     =  1;   // r1  — return address (JAL/JALR write here)
constexpr int REG_SP     =  2;   // r2  — stack pointer (by convention)
constexpr int REG_FLAGS  = 31;   // r31 — status bits [3:0]

// Flags register bit positions
constexpr int FLAG_ZERO     = (1 << 0);
constexpr int FLAG_NEGATIVE = (1 << 1);
constexpr int FLAG_OVERFLOW = (1 << 2);
constexpr int FLAG_DIVZERO  = (1 << 3);

// Memory layout
constexpr int PROGRAM_BASE = 0;    // instructions loaded here
constexpr int DATA_BASE    = 64;   // data starts after 64 instruction slots

// ---------------------------------------------------------------------------
// Opcode enum
//
// Values are chosen so helper math works cleanly:
//   R-type  field = static_cast<int>(op)          (0–14)
//   I-type  field = static_cast<int>(op) - 15     (0–3)
//   M-type  field = static_cast<int>(op) - 19     (0–1)
//   Control field = static_cast<int>(op) - 21     (0–5)
// ---------------------------------------------------------------------------
enum class Opcode {
    // R-type (type code 00) — opcode field 0–14
    ADD   =  0,  SUB  =  1,  MUL  =  2,  DIV  =  3,
    AND   =  4,  OR   =  5,  XOR  =  6,  NOT  =  7,
    SLL   =  8,  SLA  =  9,  SRL  = 10,  SRA  = 11,
    CMPLT = 12,  CMPGT = 13, CMPEQ = 14,

    // I-type (type code 01) — opcode field 0–3
    ADDI  = 15,  SUBI = 16,  MULI = 17,  DIVI = 18,

    // M-type (type code 10) — opcode field 0–1
    LOAD  = 19,  STORE = 20,

    // Control (type code 11) — opcode field 0–5
    BEQ   = 21,  BEQI = 22,
    J     = 23,  JR   = 24,
    JAL   = 25,  JALR = 26,

    // Special
    NOP   = 27,
    HALT  = 28,
};

// ---------------------------------------------------------------------------
// Instruction struct
//
// Field mapping by instruction type:
//
//  R-type  ADD/SUB/…/CMPEQ  rd=dest,  rs1=src1, rs2=src2, imm=unused
//  I-type  ADDI/…/DIVI      rd=dest,  rs1=src1, rs2=0,    imm=imm16
//  M-type  LOAD              rd=rt,    rs1=base, rs2=0,    imm=off18
//          STORE             rd=0,     rs1=base, rs2=rt,   imm=off18
//  Control BEQ               rd=0,     rs1,      rs2,      imm=off16
//          BEQI              rd=rTgt,  rs1,      rs2,      imm=unused
//          J / JAL           rd=0/1,   rs1=0,    rs2=0,    imm=off21
//          JR / JALR         rd=0/1,   rs1,      rs2=0,    imm=imm16
//  Special NOP               all 0
//          HALT              all 0
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
// Bit-field helpers
// ---------------------------------------------------------------------------

// Sign-extend an n-bit value stored in the low n bits of v.
inline int signExtend(int v, int bits) {
    int shift = 32 - bits;
    return (v << shift) >> shift;    // arithmetic right shift
}

// ---------------------------------------------------------------------------
// Encoding
//
// Bit layout:
//   R-type  [31:30=00][29:26=op4][25:21=rd][20:16=rs1][15:11=rs2][10:0=unused]
//   I-type  [31:30=01][29:26=op4][25:21=rd][20:16=rs1][15:0=imm16]
//   M-type  [31:30=10][29:28=op2][27:23=rt][22:18=base][17:0=off18]
//   B-type  [31:30=11][29:26=op4][25:21=rs1][20:16=rs2][15:0=off16]
//   BEQI    [31:30=11][29:26=op4][25:21=rs1][20:16=rs2][15:11=rTgt][10:0=0]
//   J/JAL   [31:30=11][29:26=op4][25:5=off21][4:0=0]
//   JR/JALR [31:30=11][29:26=op4][25:21=rs1][20:5=imm16][4:0=0]
//   HALT    [31:30=11][29:26=1111][25:0=0]
//   NOP     0x00000000
// ---------------------------------------------------------------------------

inline int encode(const Instruction& i) {
    int op = static_cast<int>(i.op);
    int w  = 0;

    if (i.op == Opcode::NOP) return 0;

    if (i.op == Opcode::HALT) {
        // type=11, opcode_field=1111
        return (0b11 << 30) | (0xF << 26);
    }

    // R-type (op 0–14)
    if (op <= 14) {
        w |= (0b00 << 30);
        w |= (op       & 0xF)  << 26;
        w |= (i.rd     & 0x1F) << 21;
        w |= (i.rs1    & 0x1F) << 16;
        w |= (i.rs2    & 0x1F) << 11;
        return w;
    }

    // I-type (op 15–18)
    if (op <= 18) {
        int field = op - 15;
        w |= (0b01 << 30);
        w |= (field    & 0xF)  << 26;
        w |= (i.rd     & 0x1F) << 21;
        w |= (i.rs1    & 0x1F) << 16;
        w |= (i.imm    & 0xFFFF);
        return w;
    }

    // M-type (op 19–20)
    if (op <= 20) {
        int field = op - 19;           // 0=LOAD, 1=STORE
        w |= (0b10 << 30);
        w |= (field & 0x3) << 28;
        if (i.op == Opcode::LOAD) {
            w |= (i.rd  & 0x1F) << 23;  // rt = rd for LOAD
            w |= (i.rs1 & 0x1F) << 18;  // base = rs1
        } else {                          // STORE
            w |= (i.rs2 & 0x1F) << 23;  // rt = rs2 (value to store)
            w |= (i.rs1 & 0x1F) << 18;  // base = rs1
        }
        w |= (i.imm & 0x3FFFF);
        return w;
    }

    // Control (op 21–26)
    int field = op - 21;  // 0=BEQ, 1=BEQI, 2=J, 3=JR, 4=JAL, 5=JALR
    w |= (0b11 << 30);
    w |= (field & 0xF) << 26;

    switch (i.op) {
        case Opcode::BEQ:
            w |= (i.rs1 & 0x1F) << 21;
            w |= (i.rs2 & 0x1F) << 16;
            w |= (i.imm & 0xFFFF);
            break;
        case Opcode::BEQI:
            w |= (i.rs1 & 0x1F) << 21;
            w |= (i.rs2 & 0x1F) << 16;
            w |= (i.rd  & 0x1F) << 11;  // rTgt in rd field
            break;
        case Opcode::J:
        case Opcode::JAL:
            w |= (i.imm & 0x1FFFFF) << 5;
            break;
        case Opcode::JR:
        case Opcode::JALR:
            w |= (i.rs1 & 0x1F) << 21;
            w |= (i.imm & 0xFFFF) << 5;
            break;
        default: break;
    }
    return w;
}

// ---------------------------------------------------------------------------
// Label reconstruction (for display after decode)
// ---------------------------------------------------------------------------
inline std::string makeLabel(const Instruction& i) {
    auto r = [](int reg) { return "r" + std::to_string(reg); };
    switch (i.op) {
        case Opcode::ADD:   return "ADD "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::SUB:   return "SUB "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::MUL:   return "MUL "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::DIV:   return "DIV "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::AND:   return "AND "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::OR:    return "OR "    + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::XOR:   return "XOR "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::NOT:   return "NOT "   + r(i.rd) +","+ r(i.rs1);
        case Opcode::SLL:   return "SLL "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::SLA:   return "SLA "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::SRL:   return "SRL "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::SRA:   return "SRA "   + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::CMPLT: return "CMPLT " + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::CMPGT: return "CMPGT " + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::CMPEQ: return "CMPEQ " + r(i.rd) +","+ r(i.rs1) +","+ r(i.rs2);
        case Opcode::ADDI:  return "ADDI "  + r(i.rd) +","+ r(i.rs1) +","+ std::to_string(i.imm);
        case Opcode::SUBI:  return "SUBI "  + r(i.rd) +","+ r(i.rs1) +","+ std::to_string(i.imm);
        case Opcode::MULI:  return "MULI "  + r(i.rd) +","+ r(i.rs1) +","+ std::to_string(i.imm);
        case Opcode::DIVI:  return "DIVI "  + r(i.rd) +","+ r(i.rs1) +","+ std::to_string(i.imm);
        case Opcode::LOAD:  return "LOAD "  + r(i.rd) +","+ r(i.rs1) +","+ std::to_string(i.imm);
        case Opcode::STORE: return "STORE " + r(i.rs2)+","+ r(i.rs1) +","+ std::to_string(i.imm);
        case Opcode::BEQ:   return "BEQ "   + r(i.rs1)+","+ r(i.rs2) +","+ std::to_string(i.imm);
        case Opcode::BEQI:  return "BEQI "  + r(i.rs1)+","+ r(i.rs2) +","+ r(i.rd);
        case Opcode::J:     return "J "     + std::to_string(i.imm);
        case Opcode::JR:    return "JR "    + r(i.rs1)  +","+ std::to_string(i.imm);
        case Opcode::JAL:   return "JAL "   + std::to_string(i.imm);
        case Opcode::JALR:  return "JALR "  + r(i.rs1)  +","+ std::to_string(i.imm);
        case Opcode::NOP:   return "NOP";
        case Opcode::HALT:  return "HALT";
        default:            return "???";
    }
}

// ---------------------------------------------------------------------------
// Decoding — unpacks a 32-bit word into an Instruction struct
// ---------------------------------------------------------------------------
inline Instruction decode(int word) {
    if (word == 0) return { Opcode::NOP, 0, 0, 0, 0, "NOP" };

    Instruction i = {};
    int typeCode  = (word >> 30) & 0x3;

    if (typeCode == 0b00) {
        // R-type
        int opField = (word >> 26) & 0xF;
        i.op  = static_cast<Opcode>(opField);   // ADD=0 … CMPEQ=14
        i.rd  = (word >> 21) & 0x1F;
        i.rs1 = (word >> 16) & 0x1F;
        i.rs2 = (word >> 11) & 0x1F;
        i.imm = 0;

    } else if (typeCode == 0b01) {
        // I-type
        int opField = (word >> 26) & 0xF;
        i.op  = static_cast<Opcode>(opField + 15); // ADDI=15 … DIVI=18
        i.rd  = (word >> 21) & 0x1F;
        i.rs1 = (word >> 16) & 0x1F;
        i.rs2 = 0;
        i.imm = signExtend(word & 0xFFFF, 16);

    } else if (typeCode == 0b10) {
        // M-type
        int opField = (word >> 28) & 0x3;
        int rt      = (word >> 23) & 0x1F;
        int base    = (word >> 18) & 0x1F;
        int off18   = signExtend(word & 0x3FFFF, 18);

        if (opField == 0) {
            // LOAD: rt is destination
            i.op  = Opcode::LOAD;
            i.rd  = rt;
            i.rs1 = base;
            i.rs2 = 0;
        } else {
            // STORE: rt is source value
            i.op  = Opcode::STORE;
            i.rd  = 0;
            i.rs1 = base;
            i.rs2 = rt;
        }
        i.imm = off18;

    } else {
        // Control (type 11)
        int opField = (word >> 26) & 0xF;

        if (opField == 0xF) {
            i.op = Opcode::HALT;
        } else {
            switch (opField) {
                case 0: // BEQ
                    i.op  = Opcode::BEQ;
                    i.rs1 = (word >> 21) & 0x1F;
                    i.rs2 = (word >> 16) & 0x1F;
                    i.imm = signExtend(word & 0xFFFF, 16);
                    break;
                case 1: // BEQI
                    i.op  = Opcode::BEQI;
                    i.rs1 = (word >> 21) & 0x1F;
                    i.rs2 = (word >> 16) & 0x1F;
                    i.rd  = (word >> 11) & 0x1F;  // rTgt
                    break;
                case 2: // J
                    i.op  = Opcode::J;
                    i.rd  = 0;
                    i.imm = signExtend((word >> 5) & 0x1FFFFF, 21);
                    break;
                case 3: // JR
                    i.op  = Opcode::JR;
                    i.rs1 = (word >> 21) & 0x1F;
                    i.imm = signExtend((word >> 5) & 0xFFFF, 16);
                    break;
                case 4: // JAL — always writes r1
                    i.op  = Opcode::JAL;
                    i.rd  = REG_RA;
                    i.imm = signExtend((word >> 5) & 0x1FFFFF, 21);
                    break;
                case 5: // JALR — always writes r1
                    i.op  = Opcode::JALR;
                    i.rd  = REG_RA;
                    i.rs1 = (word >> 21) & 0x1F;
                    i.imm = signExtend((word >> 5) & 0xFFFF, 16);
                    break;
                default:
                    i.op = Opcode::NOP;
                    break;
            }
        }
    }

    i.label = makeLabel(i);
    return i;
}

// ---------------------------------------------------------------------------
// Load encoded program into DRAM word by word starting at baseAddress.
// Pads any remaining words in the last cache line with 0 (=NOP).
// ---------------------------------------------------------------------------
#include "dram.hpp"

inline void loadProgramToDRAM(const std::vector<Instruction>& program,
                               DRAM& dram, int baseAddress) {
    int lineSize  = dram.getLineSize();
    int n         = static_cast<int>(program.size());
    int numLines  = (n + lineSize - 1) / lineSize;

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
// Demo program — increment array[DATA_BASE .. DATA_BASE+3] by 1
//
// Registers:
//   r3 = DATA_BASE (array base)
//   r4 = loop limit (4)
//   r5 = counter i
//   r6 = element address (r3 + r5)
//   r7 = loaded/incremented value
// ---------------------------------------------------------------------------
inline std::vector<Instruction> makeDemoProgram() {
    return {
        // PC 0: r3 = DATA_BASE
        { Opcode::ADDI,  3, 0, 0, DATA_BASE, "ADDI r3,r0," + std::to_string(DATA_BASE) },
        // PC 1: r4 = 4  (loop limit)
        { Opcode::ADDI,  4, 0, 0, 4,         "ADDI r4,r0,4" },
        // PC 2: r5 = 0  (counter)
        { Opcode::ADDI,  5, 0, 0, 0,         "ADDI r5,r0,0" },
        // PC 3: r6 = r3 + r5  (element address)   <-- loop start
        { Opcode::ADD,   6, 3, 5, 0,          "ADD r6,r3,r5" },
        // PC 4: r7 = mem[r6 + 0]
        { Opcode::LOAD,  7, 6, 0, 0,          "LOAD r7,r6,0" },
        // PC 5: r7 = r7 + 1
        { Opcode::ADDI,  7, 7, 0, 1,          "ADDI r7,r7,1" },
        // PC 6: r6 = r3 + r5  (recompute for store)
        { Opcode::ADD,   6, 3, 5, 0,          "ADD r6,r3,r5" },
        // PC 7: mem[r6 + 0] = r7
        { Opcode::STORE, 0, 6, 7, 0,          "STORE r7,r6,0" },
        // PC 8: r5 = r5 + 1
        { Opcode::ADDI,  5, 5, 0, 1,          "ADDI r5,r5,1" },
        // PC 9: if r5 != r4, jump to PC 3  (BEQ r5,r4 is fall-through, so use BEQ inverted...)
        // Actually use BEQ: if r5 == r4, branch to HALT; otherwise loop
        // Since we don't have BNE, use: CMPLT r8,r5,r4; BEQ r8,r0,-7 (branch back if r8=1 means r5<r4)
        // Simpler: BEQ r5, r4, +1  skips J if equal; J loops back
        { Opcode::BEQ,   0, 5, 4, 1,          "BEQ r5,r4,1" },   // PC 9: if r5==r4 skip next
        { Opcode::J,     0, 0, 0, -8,         "J -8" },           // PC 10: 10+1+(-8)=3 -> loop start
        { Opcode::HALT,  0, 0, 0, 0,          "HALT" },           // PC 11
    };
}
// Branch math:
//   BEQ at PC9: if r5==r4, target = 9+1+1 = 11 (HALT) ✓
//   J   at PC10: target = 10+1+(-8) = 3 (loop start) ✓
