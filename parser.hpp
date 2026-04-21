#pragma once

#include "instruction.hpp"
#include "dram.hpp"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// ParseResult
// ---------------------------------------------------------------------------
struct ParseResult {
    std::vector<Instruction>               program;
    std::vector<std::pair<int,DRAM::Line>> dataBlocks;
};

// ---------------------------------------------------------------------------
// Assembly file format (one instruction per line)
//
//   # Comments (also supported inline after the instruction)
//
//   ADD   rd, rs1, rs2
//   SUB   rd, rs1, rs2
//   MUL   rd, rs1, rs2
//   DIV   rd, rs1, rs2
//   AND   rd, rs1, rs2
//   OR    rd, rs1, rs2
//   XOR   rd, rs1, rs2
//   NOT   rd, rs1
//   SLL   rd, rs1, rs2
//   SLA   rd, rs1, rs2
//   SRL   rd, rs1, rs2
//   SRA   rd, rs1, rs2
//   CMPLT rd, rs1, rs2
//   CMPGT rd, rs1, rs2
//   CMPEQ rd, rs1, rs2
//
//   ADDI  rd, rs1, imm
//   SUBI  rd, rs1, imm
//   MULI  rd, rs1, imm
//   DIVI  rd, rs1, imm
//
//   LOAD  rt, base, off18
//   STORE rt, base, off18    (mem[base + off18] = rt)
//
//   BEQ   rs1, rs2, off16
//   BEQI  rs1, rs2, rTgt
//   J     off21
//   JR    rs1, imm16
//   JAL   off21
//   JALR  rs1, imm16
//
//   NOP
//   HALT
//
//   DATA  addr v0 v1 v2 …    (pre-load words into DRAM at word address addr)
//
// Registers: r0–r31 or plain integers 0–31.
// Commas between operands are optional.
// ---------------------------------------------------------------------------

namespace parser_detail {

inline std::string toUpper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

// Strip trailing inline comment, trim whitespace
inline std::string cleanLine(std::string line) {
    auto pos = line.find('#');
    if (pos != std::string::npos) line = line.substr(0, pos);
    while (!line.empty() && isspace((unsigned char)line.front())) line.erase(line.begin());
    while (!line.empty() && isspace((unsigned char)line.back()))  line.pop_back();
    return line;
}

// Replace commas with spaces so we can use >> uniformly
inline std::string deComma(std::string s) {
    for (auto& c : s) if (c == ',') c = ' ';
    return s;
}

inline int parseReg(const std::string& tok) {
    if (tok.empty()) throw std::runtime_error("Empty register token");
    const std::string& t = (tok[0]=='r'||tok[0]=='R') ? tok.substr(1) : tok;
    try { return std::stoi(t); }
    catch (...) { throw std::runtime_error("Bad register: '" + tok + "'"); }
}

inline int parseInt(const std::string& tok) {
    try { return std::stoi(tok); }
    catch (...) { throw std::runtime_error("Bad integer: '" + tok + "'"); }
}

} // namespace parser_detail

// ---------------------------------------------------------------------------
// parseInstructionFile
// ---------------------------------------------------------------------------
inline ParseResult parseInstructionFile(const std::string& filename) {
    using namespace parser_detail;

    ParseResult result;
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Cannot open file: " + filename);

    std::string raw;
    int lineNum = 0;

    while (std::getline(file, raw)) {
        ++lineNum;
        std::string line = cleanLine(deComma(raw));
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string opStr;
        ss >> opStr;
        opStr = toUpper(opStr);

        auto need = [&](std::string& tok) {
            if (!(ss >> tok))
                throw std::runtime_error("Not enough operands for " + opStr);
        };

        try {
            Instruction i = {};

            // ---- R-type ----
            if (opStr == "ADD"   || opStr == "SUB"  || opStr == "MUL"  || opStr == "DIV"  ||
                opStr == "AND"   || opStr == "OR"   || opStr == "XOR"  ||
                opStr == "SLL"   || opStr == "SLA"  || opStr == "SRL"  || opStr == "SRA"  ||
                opStr == "CMPLT" || opStr == "CMPGT"|| opStr == "CMPEQ") {
                std::string s1, s2, s3;
                need(s1); need(s2); need(s3);
                static const std::map<std::string,Opcode> rtable = {
                    {"ADD",Opcode::ADD},{"SUB",Opcode::SUB},{"MUL",Opcode::MUL},{"DIV",Opcode::DIV},
                    {"AND",Opcode::AND},{"OR",Opcode::OR},{"XOR",Opcode::XOR},
                    {"SLL",Opcode::SLL},{"SLA",Opcode::SLA},{"SRL",Opcode::SRL},{"SRA",Opcode::SRA},
                    {"CMPLT",Opcode::CMPLT},{"CMPGT",Opcode::CMPGT},{"CMPEQ",Opcode::CMPEQ}
                };
                i.op = rtable.at(opStr);
                i.rd  = parseReg(s1);
                i.rs1 = parseReg(s2);
                i.rs2 = parseReg(s3);

            } else if (opStr == "NOT") {
                std::string s1, s2;
                need(s1); need(s2);
                i.op  = Opcode::NOT;
                i.rd  = parseReg(s1);
                i.rs1 = parseReg(s2);
                i.rs2 = 0;

            // ---- I-type ----
            } else if (opStr == "ADDI" || opStr == "SUBI" ||
                       opStr == "MULI" || opStr == "DIVI") {
                std::string s1, s2, s3;
                need(s1); need(s2); need(s3);
                static const std::map<std::string,Opcode> itable = {
                    {"ADDI",Opcode::ADDI},{"SUBI",Opcode::SUBI},
                    {"MULI",Opcode::MULI},{"DIVI",Opcode::DIVI}
                };
                i.op  = itable.at(opStr);
                i.rd  = parseReg(s1);
                i.rs1 = parseReg(s2);
                i.imm = parseInt(s3);

            // ---- M-type ----
            } else if (opStr == "LOAD") {
                std::string s1, s2, s3;
                need(s1); need(s2); need(s3);
                i.op  = Opcode::LOAD;
                i.rd  = parseReg(s1);   // rt (destination)
                i.rs1 = parseReg(s2);   // base
                i.imm = parseInt(s3);   // off18

            } else if (opStr == "STORE") {
                std::string s1, s2, s3;
                need(s1); need(s2); need(s3);
                i.op  = Opcode::STORE;
                i.rs2 = parseReg(s1);   // rt (source value)
                i.rs1 = parseReg(s2);   // base
                i.imm = parseInt(s3);   // off18
                i.rd  = 0;

            // ---- BEQ ----
            } else if (opStr == "BEQ") {
                std::string s1, s2, s3;
                need(s1); need(s2); need(s3);
                i.op  = Opcode::BEQ;
                i.rs1 = parseReg(s1);
                i.rs2 = parseReg(s2);
                i.imm = parseInt(s3);

            // ---- BEQI ----
            } else if (opStr == "BEQI") {
                std::string s1, s2, s3;
                need(s1); need(s2); need(s3);
                i.op  = Opcode::BEQI;
                i.rs1 = parseReg(s1);
                i.rs2 = parseReg(s2);
                i.rd  = parseReg(s3);   // rTgt

            // ---- J ----
            } else if (opStr == "J") {
                std::string s1;
                need(s1);
                i.op  = Opcode::J;
                i.imm = parseInt(s1);

            // ---- JR ----
            } else if (opStr == "JR") {
                std::string s1, s2;
                need(s1); need(s2);
                i.op  = Opcode::JR;
                i.rs1 = parseReg(s1);
                i.imm = parseInt(s2);

            // ---- JAL ----
            } else if (opStr == "JAL") {
                std::string s1;
                need(s1);
                i.op  = Opcode::JAL;
                i.rd  = REG_RA;
                i.imm = parseInt(s1);

            // ---- JALR ----
            } else if (opStr == "JALR") {
                std::string s1, s2;
                need(s1); need(s2);
                i.op  = Opcode::JALR;
                i.rd  = REG_RA;
                i.rs1 = parseReg(s1);
                i.imm = parseInt(s2);

            // ---- NOP / HALT ----
            } else if (opStr == "NOP") {
                i.op = Opcode::NOP;
            } else if (opStr == "HALT") {
                i.op = Opcode::HALT;

            // ---- DATA ----
            } else if (opStr == "DATA") {
                std::string addrStr;
                need(addrStr);
                int addr = parseInt(addrStr);
                DRAM::Line words;
                std::string tok;
                while (ss >> tok) words.push_back(parseInt(tok));
                if (words.empty())
                    throw std::runtime_error("DATA line has no values");
                result.dataBlocks.push_back({ addr, words });
                continue;   // not an instruction

            } else {
                throw std::runtime_error("Unknown mnemonic: " + opStr);
            }

            i.label = makeLabel(i);
            result.program.push_back(i);

        } catch (const std::exception& ex) {
            throw std::runtime_error("Line " + std::to_string(lineNum) + ": " + ex.what());
        }
    }

    return result;
}
