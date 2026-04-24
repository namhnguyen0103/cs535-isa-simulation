#pragma once

#include "instruction.hpp"
#include "registerfile.hpp"
#include "memif.hpp"

#include <string>

// ---------------------------------------------------------------------------
// SequentialExecutor
//
// Executes one complete instruction per step() call.
// Used in NO_PIPE_NO_CACHE (mem = DirectMemIF) and CACHE_ONLY (mem = Cache).
//
// Cycle counting:
//   Each call to mem->load/store counts as one cycle. Non-memory instructions
//   cost 1 cycle. This makes cycle counts directly comparable across modes.
// ---------------------------------------------------------------------------
class SequentialExecutor {
public:
    SequentialExecutor(int programBase, int programSize, MemIF* mem);

    // Execute one complete instruction. Returns cycles consumed (>= 1).
    // Returns 0 if already done.
    int step();

    bool isDone()              const { return done_;  }
    int  getCycleCount()       const { return cycle_; }
    int  readRegister(int reg) const { return rf_.read(reg); }
    int  getProgramBase()      const { return programBase_; }

    // Current PC — address of the next instruction to be executed
    int getCurrentPC() const { return pc_; }

    // For GUI display — set after each step()
    std::string getLastLabel() const { return lastLabel_; }
    int         getLastPC()    const { return lastPC_;    }

private:
    int fetchInstruction(int address, int& wordOut);
    int doLoad (int address, int& valueOut);
    int doStore(int address, int value);

    int    programBase_, programEndPc_;
    MemIF* mem_;
    RegisterFile rf_;

    int  pc_    = 0;
    int  cycle_ = 0;
    bool done_  = false;

    std::string lastLabel_ = "---";
    int         lastPC_    = -1;
};
