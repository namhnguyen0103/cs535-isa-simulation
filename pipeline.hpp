#pragma once

#include "instruction.hpp"
#include "registerfile.hpp"
#include "cache.hpp"

#include <vector>
#include <string>
#include <iostream>
#include <iomanip>

class Pipeline {
public:
    Pipeline(int programBase, int programSize, Cache* cache);

    // Advance one clock cycle. Returns false when done.
    bool tick();
    void dump() const;

    int  getCycleCount() const { return cycle_; }
    bool isDone()        const { return done_;  }

    // -----------------------------------------------------------------------
    // GUI accessors — valid after each tick()
    // -----------------------------------------------------------------------
    std::string getIFLabel()  const { return ifLabel_;  }
    std::string getIDLabel()  const { return idLabel_;  }
    std::string getEXLabel()  const { return exLabel_;  }
    std::string getMEMLabel() const { return memLabel_; }
    std::string getWBLabel()  const { return wbLabel_;  }

    int getIFPC()  const { return ifPC_;  }
    int getIDPC()  const { return idPC_;  }
    int getEXPC()  const { return exPC_;  }
    int getMEMPC() const { return memPC_; }
    int getWBPC()  const { return wbPC_;  }

    int readRegister(int reg) const { return rf_.read(reg); }
    int getProgramBase()      const { return programBase_;  }

private:
    // -----------------------------------------------------------------------
    // Pipeline latches
    //
    // squashed=true means the instruction was fetched on a wrong path after
    // a branch. It stays in the pipeline and is visible in the display, but
    // all side effects (register write, cache access, branch, flags) are
    // suppressed. valid=false means the slot is a structural bubble (no
    // instruction at all — used for stall-injected NOPs).
    // -----------------------------------------------------------------------

    struct IFIDReg {
        Instruction instr    = makeNop();
        int  pc      = 0;
        bool valid   = false;
        bool squashed = false;
    };

    struct IDEXReg {
        Instruction instr    = makeNop();
        int  pc      = 0;
        int  rv1     = 0;
        int  rv2     = 0;
        int  rvTgt   = 0;    // value of rd register (used by BEQI)
        bool valid   = false;
        bool squashed = false;
    };

    struct EXMEMReg {
        Instruction instr        = makeNop();
        int  pc           = 0;
        int  aluResult    = 0;
        int  storeValue   = 0;
        bool branchTaken  = false;
        int  branchTarget = 0;
        bool writesFlags  = false;
        int  flagsValue   = 0;
        bool valid        = false;
        bool squashed     = false;
    };

    struct MEMWBReg {
        Instruction instr       = makeNop();
        int  pc          = 0;
        int  result      = 0;
        bool writesFlags = false;
        int  flagsValue  = 0;
        bool valid       = false;
        bool squashed    = false;
    };

    // -----------------------------------------------------------------------
    // Stage helpers
    // -----------------------------------------------------------------------
    bool doMEM(MEMWBReg& next);
    bool doEX (EXMEMReg& next, int& branchTarget);
    bool hasDataHazard() const;
    static bool writesRegister(const Instruction& instr);
    static bool setsFlags     (const Instruction& instr);
    static int  computeFlags  (int result, bool divZero = false, bool overflow = false);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    int    programBase_;
    int    programEndPc_;
    Cache* cache_;
    RegisterFile rf_;

    int  pc_             = 0;
    int  cycle_          = 0;
    bool done_           = false;
    bool haltInPipeline_ = false;

    IFIDReg  ifid_;
    IDEXReg  idex_;
    EXMEMReg exmem_;
    MEMWBReg memwb_;

    std::string ifLabel_, idLabel_, exLabel_, memLabel_, wbLabel_;
    int ifPC_ = -1, idPC_ = -1, exPC_ = -1, memPC_ = -1, wbPC_ = -1;
};
