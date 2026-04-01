#pragma once

#include "../instruction/instruction.hpp"
#include "../register/registerfile.hpp"
#include "../cache/cache.hpp"

#include <vector>
#include <string>
#include <iostream>
#include <iomanip>

class Pipeline {
public:
    Pipeline(std::vector<Instruction> program, Cache* cache);

    // Advance one clock cycle.
    // Returns true if still running, false if the pipeline has drained.
    bool tick();

    // Print the current pipeline state. Call after each tick().
    void dump() const;

    int  getCycleCount() const { return cycle_; }
    bool isDone()        const { return done_;  }

private:
    // -----------------------------------------------------------------------
    // Pipeline latches — hold state between stages.
    // valid=false means the slot is a bubble (NOP).
    // -----------------------------------------------------------------------

    struct IFIDReg {
        Instruction instr = makeNop();
        int  pc    = 0;
        bool valid = false;
    };

    struct IDEXReg {
        Instruction instr = makeNop();
        int  pc    = 0;
        int  rv1   = 0;     // value read from rs1
        int  rv2   = 0;     // value read from rs2
        bool valid = false;
    };

    struct EXMEMReg {
        Instruction instr       = makeNop();
        int  pc          = 0;
        int  aluResult   = 0;
        int  rv2         = 0;   // store value (rs2) carried forward for STORE
        bool branchTaken = false;
        int  branchTarget = 0;
        bool valid        = false;
    };

    struct MEMWBReg {
        Instruction instr  = makeNop();
        int  pc     = 0;
        int  result = 0;    // ALU result, or value loaded from cache
        bool valid  = false;
    };

    // -----------------------------------------------------------------------
    // Stage helpers
    // -----------------------------------------------------------------------

    // MEM: returns true if stalling (cache not ready)
    bool doMEM(MEMWBReg& next);

    // EX: returns true if a branch was taken, sets branchTarget
    bool doEX(EXMEMReg& next, int& branchTarget);

    // Hazard detection: true if ID sees a RAW hazard with EX or MEM
    bool hasDataHazard() const;

    // True if the instruction writes a result to rd
    static bool writesRegister(const Instruction& instr);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    std::vector<Instruction> program_;
    Cache*       cache_;
    RegisterFile rf_;

    int  pc_    = 0;
    int  cycle_ = 0;
    bool done_  = false;

    IFIDReg  ifid_;
    IDEXReg  idex_;
    EXMEMReg exmem_;
    MEMWBReg memwb_;

    // Display labels — set each tick() for dump()
    std::string ifLabel_;
    std::string idLabel_;
    std::string exLabel_;
    std::string memLabel_;
    std::string wbLabel_;
};