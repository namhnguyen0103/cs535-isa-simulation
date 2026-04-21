#include "pipeline.hpp"
#include <climits>

Pipeline::Pipeline(int programBase, int programSize, Cache* cache)
    : programBase_(programBase),
      programEndPc_(programBase + programSize),
      cache_(cache),
      pc_(programBase) {}

// ---------------------------------------------------------------------------
// tick()
// ---------------------------------------------------------------------------
bool Pipeline::tick() {
    if (done_) return false;
    cycle_++;

    // Snapshot PCs at start of cycle for GUI highlighting
    wbPC_  = (memwb_.valid && !memwb_.squashed) ? memwb_.pc  : -1;
    memPC_ = (exmem_.valid && !exmem_.squashed) ? exmem_.pc  : -1;
    exPC_  = (idex_.valid  && !idex_.squashed)  ? idex_.pc   : -1;
    idPC_  = (ifid_.valid  && !ifid_.squashed)  ? ifid_.pc   : -1;
    ifPC_  = (!haltInPipeline_ && pc_ < programEndPc_) ? pc_ : -1;

    int savedPc = pc_;

    // -----------------------------------------------------------------------
    // WB — write results; skip everything for squashed instructions
    // -----------------------------------------------------------------------
    MEMWBReg nextMemwb = {};

    if (memwb_.valid) {
        if (memwb_.squashed) {
            // Squashed: visible in display but produces no side effects
            wbLabel_ = memwb_.instr.label + " [squashed]";
        } else {
            wbLabel_ = memwb_.instr.label;

            if (memwb_.instr.op == Opcode::HALT) {
                done_ = true;
                return false;
            }

            if (writesRegister(memwb_.instr) && memwb_.instr.rd != 0)
                rf_.write(memwb_.instr.rd, memwb_.result);

            if (memwb_.writesFlags) {
                int cur = rf_.read(REG_FLAGS);
                rf_.write(REG_FLAGS, (cur & ~0xF) | (memwb_.flagsValue & 0xF));
            }
        }
    } else {
        wbLabel_ = "---";
    }

    // -----------------------------------------------------------------------
    // MEM — cache access; skip for squashed instructions
    // -----------------------------------------------------------------------
    bool memNeedsCache = exmem_.valid && !exmem_.squashed &&
        (exmem_.instr.op == Opcode::LOAD || exmem_.instr.op == Opcode::STORE);

    if (memNeedsCache) cache_->cancelRequestFrom(Cache::StageId::IF);

    bool memStall = doMEM(nextMemwb);

    // -----------------------------------------------------------------------
    // EX — ALU; squashed instructions produce no branch and no flags
    // -----------------------------------------------------------------------
    EXMEMReg nextExmem = {};
    int  branchTarget = 0;
    bool branchTaken  = doEX(nextExmem, branchTarget);

    // -----------------------------------------------------------------------
    // Data hazard detection
    // -----------------------------------------------------------------------
    bool dataHazard = hasDataHazard();

    // -----------------------------------------------------------------------
    // ID — register file read
    // -----------------------------------------------------------------------
    IDEXReg nextIdex = {};

    if (dataHazard || memStall) {
        idLabel_ = ifid_.valid ? (ifid_.instr.label + " [stall]") : "---";
        nextIdex = {};   // structural bubble
    } else if (ifid_.valid) {
        idLabel_ = ifid_.squashed
            ? (ifid_.instr.label + " [squashed]")
            : ifid_.instr.label;
        nextIdex = {
            ifid_.instr,
            ifid_.pc,
            rf_.read(ifid_.instr.rs1),
            rf_.read(ifid_.instr.rs2),
            rf_.read(ifid_.instr.rd),
            true,
            ifid_.squashed    // propagate squash flag forward
        };
    } else {
        idLabel_ = "---";
    }

    // -----------------------------------------------------------------------
    // IF — fetch from cache
    // -----------------------------------------------------------------------
    IFIDReg nextIfid = {};
    bool    ifStall  = false;

    if (dataHazard || memStall) {
        nextIfid = ifid_;
        ifLabel_ = ifid_.valid
            ? (ifid_.instr.label + (ifid_.squashed ? " [squashed]" : "") + " [frozen]")
            : "---";

    } else if (haltInPipeline_ || memNeedsCache) {
        ifStall  = true;
        nextIfid = {};
        ifLabel_ = haltInPipeline_
            ? "--- [halt drain]"
            : ("PC" + std::to_string(pc_) + " [IF blocked]");

    } else if (pc_ < programEndPc_) {
        auto result = cache_->load(pc_, Cache::StageId::IF);
        if (result.wait) {
            ifStall  = true;
            nextIfid = {};
            ifLabel_ = "PC" + std::to_string(pc_) + " [IF cache wait]";
        } else {
            Instruction instr = decode(result.value);
            if (instr.op == Opcode::HALT) haltInPipeline_ = true;
            nextIfid = { instr, pc_, true, false };
            ifLabel_ = instr.label;
            pc_++;
        }
    } else {
        ifLabel_ = "---";
    }

    // -----------------------------------------------------------------------
    // Branch squash
    //
    // Key change from naive approach: instructions already in IF and ID are
    // NOT replaced with bubbles. Instead they are marked squashed=true and
    // stay in their latches. They will flow through EX → MEM → WB visibly
    // but all side effects are suppressed at each stage.
    // -----------------------------------------------------------------------
    if (branchTaken) {
        // Mark the instruction currently in IF as squashed
        if (nextIfid.valid) {
            nextIfid.squashed = true;
            ifLabel_ = nextIfid.instr.label + " [squashed]";
        }

        // Mark the instruction currently in ID as squashed
        if (nextIdex.valid) {
            nextIdex.squashed = true;
            idLabel_ = nextIdex.instr.label + " [squashed]";
        }

        // Redirect the program counter to the branch target
        pc_ = branchTarget;
        haltInPipeline_ = false;
    }

    // -----------------------------------------------------------------------
    // MEM stall override — freeze everything except WB
    // -----------------------------------------------------------------------
    if (memStall) {
        pc_       = savedPc;
        nextIfid  = ifid_;
        nextIdex  = idex_;
        nextExmem = exmem_;
        ifLabel_  = ifid_.valid
            ? (ifid_.instr.label + (ifid_.squashed ? " [squashed]" : "") + " [mem stall]")
            : "---";
        idLabel_  = idex_.valid
            ? (idex_.instr.label + (idex_.squashed ? " [squashed]" : "") + " [mem stall]")
            : "---";
        exLabel_  = exmem_.valid
            ? (exmem_.instr.label + (exmem_.squashed ? " [squashed]" : "") + " [mem stall]")
            : "---";
    }

    // -----------------------------------------------------------------------
    // Commit
    // -----------------------------------------------------------------------
    memwb_ = nextMemwb;
    exmem_ = nextExmem;
    idex_  = nextIdex;
    ifid_  = nextIfid;

    if (!haltInPipeline_ && pc_ >= programEndPc_ && !ifStall &&
        !ifid_.valid && !idex_.valid && !exmem_.valid && !memwb_.valid) {
        done_ = true;
    }

    return !done_;
}

// ---------------------------------------------------------------------------
// MEM stage — skip cache for squashed instructions
// ---------------------------------------------------------------------------
bool Pipeline::doMEM(MEMWBReg& next) {
    if (!exmem_.valid) { memLabel_ = "---"; return false; }

    memLabel_ = exmem_.squashed
        ? (exmem_.instr.label + " [squashed]")
        : exmem_.instr.label;

    // Squashed: pass through without touching cache
    if (exmem_.squashed) {
        next = { exmem_.instr, exmem_.pc, exmem_.aluResult,
                 false, 0, true, true };
        return false;
    }

    switch (exmem_.instr.op) {
        case Opcode::LOAD: {
            auto r = cache_->load(exmem_.aluResult, Cache::StageId::MEM);
            if (r.wait) { memLabel_ += " [cache wait]"; return true; }
            next = { exmem_.instr, exmem_.pc, r.value,
                     exmem_.writesFlags, exmem_.flagsValue, true, false };
            return false;
        }
        case Opcode::STORE: {
            auto r = cache_->store(exmem_.aluResult, Cache::StageId::MEM, exmem_.storeValue);
            if (r.wait) { memLabel_ += " [cache wait]"; return true; }
            next = { exmem_.instr, exmem_.pc, 0,
                     false, 0, true, false };
            return false;
        }
        default:
            next = { exmem_.instr, exmem_.pc, exmem_.aluResult,
                     exmem_.writesFlags, exmem_.flagsValue, true, false };
            return false;
    }
}

// ---------------------------------------------------------------------------
// EX stage — squashed instructions produce no branch and no flags
// ---------------------------------------------------------------------------
bool Pipeline::doEX(EXMEMReg& next, int& branchTarget) {
    if (!idex_.valid) { exLabel_ = "---"; return false; }

    exLabel_ = idex_.squashed
        ? (idex_.instr.label + " [squashed]")
        : idex_.instr.label;

    // Squashed: pass instruction through with no computation or branch
    if (idex_.squashed) {
        next = { idex_.instr, idex_.pc, 0, 0,
                 false, 0, false, 0, true, true };
        return false;
    }

    int rv1 = idex_.rv1;
    int rv2 = idex_.rv2;
    int imm = idex_.instr.imm;
    int pc  = idex_.pc;

    int  result   = 0;
    bool taken    = false;
    bool wFlags   = setsFlags(idex_.instr);
    bool divZero  = false;
    bool overflow = false;

    switch (idex_.instr.op) {
        case Opcode::ADD:  { int64_t r=(int64_t)rv1+rv2; result=(int)r; overflow=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::SUB:  { int64_t r=(int64_t)rv1-rv2; result=(int)r; overflow=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::MUL:  { int64_t r=(int64_t)rv1*rv2; result=(int)r; overflow=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::DIV:  if(rv2==0){divZero=true;}else{result=rv1/rv2;} break;
        case Opcode::AND:  result=rv1&rv2; break;
        case Opcode::OR:   result=rv1|rv2; break;
        case Opcode::XOR:  result=rv1^rv2; break;
        case Opcode::NOT:  result=~rv1; break;
        case Opcode::SLL:  result=rv1<<(rv2&31); break;
        case Opcode::SLA:  { int b=rv1; result=rv1<<(rv2&31); overflow=((b<0)!=(result<0)); break; }
        case Opcode::SRL:  result=(int)((unsigned)rv1>>(rv2&31)); break;
        case Opcode::SRA:  result=rv1>>(rv2&31); break;
        case Opcode::CMPLT: result=(rv1< rv2)?1:0; break;
        case Opcode::CMPGT: result=(rv1> rv2)?1:0; break;
        case Opcode::CMPEQ: result=(rv1==rv2)?1:0; break;
        case Opcode::ADDI: { int64_t r=(int64_t)rv1+imm; result=(int)r; overflow=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::SUBI: { int64_t r=(int64_t)rv1-imm; result=(int)r; overflow=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::MULI: { int64_t r=(int64_t)rv1*imm; result=(int)r; overflow=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::DIVI: if(imm==0){divZero=true;}else{result=rv1/imm;} break;
        case Opcode::LOAD:  result=rv1+imm; wFlags=false; break;
        case Opcode::STORE: result=rv1+imm; wFlags=false; break;
        case Opcode::BEQ:  taken=(rv1==rv2); branchTarget=pc+1+imm; wFlags=false; break;
        case Opcode::BEQI: taken=(rv1==rv2); branchTarget=idex_.rvTgt; wFlags=false; break;
        case Opcode::J:    taken=true; branchTarget=pc+1+imm; wFlags=false; break;
        case Opcode::JR:   taken=true; branchTarget=rv1+imm; wFlags=false; break;
        case Opcode::JAL:  taken=true; branchTarget=pc+1+imm; result=pc+1; wFlags=false; break;
        case Opcode::JALR: taken=true; branchTarget=rv1+imm;  result=pc+1; wFlags=false; break;
        case Opcode::NOP:
        case Opcode::HALT: wFlags=false; break;
    }

    next = { idex_.instr, pc, result, rv2,
             taken, branchTarget,
             wFlags, computeFlags(result, divZero, overflow),
             true, false };

    return taken;
}

// ---------------------------------------------------------------------------
// Data hazard detection
// Squashed instructions will not write registers so they are NOT producers.
// ---------------------------------------------------------------------------
bool Pipeline::hasDataHazard() const {
    if (!ifid_.valid || ifid_.squashed) return false;

    const Instruction& c = ifid_.instr;

    auto conflicts = [&](int rd) -> bool {
        if (rd == 0) return false;
        if (rd == c.rs1 || rd == c.rs2) return true;
        if (c.op == Opcode::BEQI && rd == c.rd) return true;
        return false;
    };

    // Only count non-squashed producers
    if (idex_.valid  && !idex_.squashed  && writesRegister(idex_.instr)  && conflicts(idex_.instr.rd))  return true;
    if (exmem_.valid && !exmem_.squashed && writesRegister(exmem_.instr) && conflicts(exmem_.instr.rd)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------
bool Pipeline::writesRegister(const Instruction& i) {
    switch (i.op) {
        case Opcode::ADD:  case Opcode::SUB:  case Opcode::MUL:  case Opcode::DIV:
        case Opcode::AND:  case Opcode::OR:   case Opcode::XOR:  case Opcode::NOT:
        case Opcode::SLL:  case Opcode::SLA:  case Opcode::SRL:  case Opcode::SRA:
        case Opcode::CMPLT: case Opcode::CMPGT: case Opcode::CMPEQ:
        case Opcode::ADDI: case Opcode::SUBI: case Opcode::MULI: case Opcode::DIVI:
        case Opcode::LOAD:
        case Opcode::JAL:  case Opcode::JALR:
            return true;
        default: return false;
    }
}

bool Pipeline::setsFlags(const Instruction& i) {
    switch (i.op) {
        case Opcode::ADD:  case Opcode::SUB:  case Opcode::MUL:  case Opcode::DIV:
        case Opcode::AND:  case Opcode::OR:   case Opcode::XOR:  case Opcode::NOT:
        case Opcode::SLL:  case Opcode::SLA:  case Opcode::SRL:  case Opcode::SRA:
        case Opcode::CMPLT: case Opcode::CMPGT: case Opcode::CMPEQ:
        case Opcode::ADDI: case Opcode::SUBI: case Opcode::MULI: case Opcode::DIVI:
            return true;
        default: return false;
    }
}

int Pipeline::computeFlags(int result, bool divZero, bool overflow) {
    int f = 0;
    if (result == 0) f |= FLAG_ZERO;
    if (result <  0) f |= FLAG_NEGATIVE;
    if (overflow)    f |= FLAG_OVERFLOW;
    if (divZero)     f |= FLAG_DIVZERO;
    return f;
}

void Pipeline::dump() const {
    const int w = 30;
    std::cout << "Cycle " << std::setw(3) << cycle_ << " | "
              << "IF: "  << std::setw(w) << std::left << ifLabel_
              << "| ID: " << std::setw(w) << idLabel_
              << "| EX: " << std::setw(w) << exLabel_
              << "| MEM: "<< std::setw(w) << memLabel_
              << "| WB: " << wbLabel_
              << std::right << "\n";
}
