#include "pipeline.hpp"

Pipeline::Pipeline(std::vector<Instruction> program, Cache* cache)
    : program_(std::move(program)), cache_(cache) {}

// -----------------------------------------------------------------------------
// tick() — advances the pipeline one clock cycle.
//
// Execution order:
//   1. WB  — always runs, writes result to register file
//   2. MEM — may stall if cache is not ready
//   3. EX  — computes ALU result, resolves branches at end of EX
//   4. ID  — reads register file (skipped/frozen on data hazard)
//   5. IF  — fetches next instruction (skipped/frozen on data hazard)
//
// After computing all next-state values from current latches, stall and
// squash overrides are applied before committing.
// -----------------------------------------------------------------------------

bool Pipeline::tick() {
    if (done_) return false;
    cycle_++;

    // Save PC so we can restore it if a MEM stall forces a freeze.
    int savedPc = pc_;

    // -------------------------------------------------------------------------
    // WB — consumes memwb_, writes to register file.
    // This always runs regardless of stalls downstream.
    // After WB, nextMemwb starts as a bubble; doMEM will fill it if MEM completes.
    // -------------------------------------------------------------------------
    MEMWBReg nextMemwb = {};    // bubble by default — WB consumed the latch

    if (memwb_.valid) {
        wbLabel_ = memwb_.instr.label;
        if (writesRegister(memwb_.instr) && memwb_.instr.rd != 0) {
            rf_.write(memwb_.instr.rd, memwb_.result);
        }
    } else {
        wbLabel_ = "---";
    }

    // -------------------------------------------------------------------------
    // MEM — reads exmem_, calls cache for LOAD/STORE, produces nextMemwb.
    // Returns true if the cache is not ready (stall this cycle).
    // -------------------------------------------------------------------------
    bool memStall = doMEM(nextMemwb);

    // -------------------------------------------------------------------------
    // EX — reads idex_, computes ALU result, detects branch outcome.
    // Returns true if a branch is taken, and sets branchTarget.
    // -------------------------------------------------------------------------
    EXMEMReg nextExmem = {};
    int  branchTarget = 0;
    bool branchTaken  = doEX(nextExmem, branchTarget);

    // -------------------------------------------------------------------------
    // Data hazard detection.
    // A hazard exists when the instruction currently in ID (ifid_) reads a
    // register that is still being produced by an instruction in EX (idex_)
    // or MEM (exmem_).  Without forwarding we must stall until WB completes.
    // -------------------------------------------------------------------------
    bool dataHazard = hasDataHazard();

    // -------------------------------------------------------------------------
    // ID — reads register file, produces nextIdex.
    // Frozen on data hazard: injects a bubble into EX instead.
    // -------------------------------------------------------------------------
    IDEXReg nextIdex = {};

    if (dataHazard) {
        // Freeze ID in place; inject a bubble into EX.
        idLabel_ = ifid_.valid ? (ifid_.instr.label + " [stall]") : "---";
        nextIdex = {};  // bubble
    } else {
        if (ifid_.valid) {
            idLabel_ = ifid_.instr.label;
            nextIdex = {
                ifid_.instr,
                ifid_.pc,
                rf_.read(ifid_.instr.rs1),
                rf_.read(ifid_.instr.rs2),
                true
            };
        } else {
            idLabel_ = "---";
        }
    }

    // -------------------------------------------------------------------------
    // IF — fetches next instruction from program array, produces nextIfid.
    // Frozen on data hazard: keeps current ifid_ unchanged.
    // -------------------------------------------------------------------------
    IFIDReg nextIfid = {};

    if (dataHazard) {
        // Freeze IF in place.
        ifLabel_ = ifid_.valid ? (ifid_.instr.label + " [frozen]") : "---";
        nextIfid = ifid_;
    } else {
        if (pc_ < (int)program_.size()) {
            ifLabel_ = program_[pc_].label;
            nextIfid = { program_[pc_], pc_, true };
            pc_++;
        } else {
            ifLabel_ = "---";
        }
    }

    // -------------------------------------------------------------------------
    // Branch squash — overwrites IF and ID results.
    // Branch resolved at end of EX: squash the two instructions already
    // fetched behind the branch (currently in IF and ID), redirect PC.
    // -------------------------------------------------------------------------
    if (branchTaken) {
        nextIfid = {};   // squash instruction in IF
        nextIdex = {};   // squash instruction in ID
        ifLabel_ = "XXX [squashed]";
        idLabel_ = "XXX [squashed]";
        pc_ = branchTarget;
    }

    // -------------------------------------------------------------------------
    // MEM stall override — freezes everything except WB (which already ran).
    // Restores PC so IF does not advance, and keeps all latches in place
    // so every stage retries from the same state next cycle.
    // -------------------------------------------------------------------------
    if (memStall) {
        pc_      = savedPc;   // undo any PC advance from IF
        nextIfid = ifid_;     // freeze IF
        nextIdex = idex_;     // freeze ID/EX boundary
        nextExmem = exmem_;   // freeze MEM (retry cache next cycle)
        // nextMemwb stays as bubble — WB already ran and consumed the old value
        ifLabel_  = ifid_.valid  ? (ifid_.instr.label  + " [mem stall]") : "---";
        idLabel_  = idex_.valid  ? (idex_.instr.label  + " [mem stall]") : "---";
        exLabel_  = exmem_.valid ? (exmem_.instr.label + " [mem stall]") : "---";
    }

    // -------------------------------------------------------------------------
    // Commit all next-state values.
    // -------------------------------------------------------------------------
    memwb_ = nextMemwb;
    exmem_ = nextExmem;
    idex_  = nextIdex;
    ifid_  = nextIfid;

    // -------------------------------------------------------------------------
    // Done check: PC is past the program and all latches have drained.
    // -------------------------------------------------------------------------
    if (pc_ >= (int)program_.size() &&
        !ifid_.valid && !idex_.valid && !exmem_.valid && !memwb_.valid) {
        done_ = true;
    }

    return !done_;
}

// -----------------------------------------------------------------------------
// MEM stage
// -----------------------------------------------------------------------------

bool Pipeline::doMEM(MEMWBReg& next) {
    if (!exmem_.valid) {
        memLabel_ = "---";
        return false;
    }

    memLabel_ = exmem_.instr.label;

    switch (exmem_.instr.op) {
        case Opcode::LOAD: {
            auto result = cache_->load(exmem_.aluResult, Cache::StageId::MEM);
            if (result.wait) {
                memLabel_ += " [cache wait]";
                return true;    // stall
            }
            next = { exmem_.instr, exmem_.pc, result.value, true };
            return false;
        }
        case Opcode::STORE: {
            auto result = cache_->store(exmem_.aluResult, Cache::StageId::MEM, exmem_.rv2);
            if (result.wait) {
                memLabel_ += " [cache wait]";
                return true;    // stall
            }
            next = { exmem_.instr, exmem_.pc, 0, true };
            return false;
        }
        default:
            // Non-memory instruction: pass ALU result straight through
            next = { exmem_.instr, exmem_.pc, exmem_.aluResult, true };
            return false;
    }
}

// -----------------------------------------------------------------------------
// EX stage
// -----------------------------------------------------------------------------

bool Pipeline::doEX(EXMEMReg& next, int& branchTarget) {
    if (!idex_.valid) {
        exLabel_ = "---";
        return false;
    }

    exLabel_ = idex_.instr.label;

    int  aluResult = 0;
    bool taken     = false;
    branchTarget   = 0;

    switch (idex_.instr.op) {
        case Opcode::ADD:
            // rd = rs1 + rs2 + imm  (set rs2=0 for ADDI, imm=0 for plain ADD)
            aluResult = idex_.rv1 + idex_.rv2 + idex_.instr.imm;
            break;

        case Opcode::LOAD:
            // address = rs1 + imm
            aluResult = idex_.rv1 + idex_.instr.imm;
            break;

        case Opcode::STORE:
            // address = rs1 + imm  (store value is rv2, carried in EXMEMReg)
            aluResult = idex_.rv1 + idex_.instr.imm;
            break;

        case Opcode::BNE:
            taken        = (idex_.rv1 != idex_.rv2);
            branchTarget = idex_.pc + idex_.instr.imm;
            aluResult    = taken ? 1 : 0;
            break;
    }

    next = {
        idex_.instr,
        idex_.pc,
        aluResult,
        idex_.rv2,      // carry rs2 value for STORE
        taken,
        branchTarget,
        true
    };

    return taken;
}

// -----------------------------------------------------------------------------
// Data hazard detection
//
// A RAW (read-after-write) hazard exists when the instruction in ID (ifid_)
// reads a register that is still being produced by an instruction in EX
// (idex_) or MEM (exmem_).
//
// Without forwarding, we stall until the producing instruction reaches WB
// and writes the register file, so we must stall whenever the producer is
// in EX or MEM.
// -----------------------------------------------------------------------------

bool Pipeline::hasDataHazard() const {
    if (!ifid_.valid) return false;

    const Instruction& consuming = ifid_.instr;

    // Returns true if register rd conflicts with either source of the
    // consuming instruction.
    auto conflicts = [&](int rd) -> bool {
        if (rd == 0) return false;  // r0 never conflicts
        return (rd == consuming.rs1 || rd == consuming.rs2);
    };

    // Producer in EX
    if (idex_.valid && writesRegister(idex_.instr)) {
        if (conflicts(idex_.instr.rd)) return true;
    }

    // Producer in MEM
    if (exmem_.valid && writesRegister(exmem_.instr)) {
        if (conflicts(exmem_.instr.rd)) return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

bool Pipeline::writesRegister(const Instruction& instr) {
    return instr.op == Opcode::ADD || instr.op == Opcode::LOAD;
}

// -----------------------------------------------------------------------------
// dump() — prints one line of pipeline state per cycle
// -----------------------------------------------------------------------------

void Pipeline::dump() const {
    const int w = 28;
    std::cout << "Cycle " << std::setw(3) << cycle_ << " | "
              << "IF: "  << std::setw(w) << std::left << ifLabel_
              << "| ID: "  << std::setw(w) << idLabel_
              << "| EX: "  << std::setw(w) << exLabel_
              << "| MEM: " << std::setw(w) << memLabel_
              << "| WB: "  << wbLabel_
              << std::right << "\n";
}