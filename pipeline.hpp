#pragma once

#include "instruction.hpp"
#include "registerfile.hpp"
#include "memif.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <iomanip>

class Pipeline {
public:
    Pipeline(int programBase, int programSize, MemIF* mem);

    // Advance one clock cycle. Returns false when the pipeline has fully drained.
    bool tick();
    void dump() const;

    int  getCycleCount() const { return cycle_; }
    bool isDone()        const { return done_;  }

    // Current fetch PC — the address of the next instruction to be fetched.
    // In pipeline mode this is the most natural "program counter" to display.
    int getCurrentPC() const { return pc_; }

    // GUI accessors — valid after each tick()
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
    // squashed=true: instruction was on the wrong path after a branch.
    //   Stays visible in the pipeline but all side effects are suppressed.
    // valid=false: structural bubble — stall-injected NOP, no instruction.
    // -----------------------------------------------------------------------
    struct IFIDReg {
        Instruction instr    = makeNop();
        int  pc      = 0;
        bool valid   = false;
        bool squashed= false;
    };
    struct IDEXReg {
        Instruction instr    = makeNop();
        int  pc=0, rv1=0, rv2=0, rvTgt=0;
        bool valid=false, squashed=false;
    };
    struct EXMEMReg {
        Instruction instr        = makeNop();
        int  pc=0, aluResult=0, storeValue=0, branchTarget=0, flagsValue=0;
        bool branchTaken=false, writesFlags=false, valid=false, squashed=false;
    };
    struct MEMWBReg {
        Instruction instr  = makeNop();
        int  pc=0, result=0, flagsValue=0;
        bool writesFlags=false, valid=false, squashed=false;
    };

    bool doMEM(MEMWBReg& next);
    bool doEX (EXMEMReg& next, int& branchTarget);
    bool hasDataHazard() const;
    static bool writesRegister(const Instruction& i);
    static bool setsFlags     (const Instruction& i);
    static int  computeFlags  (int result, bool divZero=false, bool overflow=false);

    int    programBase_, programEndPc_;
    MemIF* mem_;
    RegisterFile rf_;

    int  pc_=0, cycle_=0;
    bool done_=false, haltInPipeline_=false;

    IFIDReg  ifid_;
    IDEXReg  idex_;
    EXMEMReg exmem_;
    MEMWBReg memwb_;

    std::string ifLabel_, idLabel_, exLabel_, memLabel_, wbLabel_;
    int ifPC_=-1, idPC_=-1, exPC_=-1, memPC_=-1, wbPC_=-1;
};
