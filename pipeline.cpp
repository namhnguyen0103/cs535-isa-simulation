#include "pipeline.hpp"
#include <climits>

Pipeline::Pipeline(int programBase, int programSize, MemIF* mem, bool noOverlap)
    : programBase_(programBase), programEndPc_(programBase + programSize),
      mem_(mem), noOverlap_(noOverlap), pc_(programBase) {}

// ---------------------------------------------------------------------------
// downstreamBusy — true when any stage after IF holds a valid latch.
// Used by no-overlap mode to prevent IF from fetching until the pipeline drains.
// ---------------------------------------------------------------------------
bool Pipeline::downstreamBusy() const {
    return ifid_.valid || idex_.valid || exmem_.valid || memwb_.valid;
}

// ---------------------------------------------------------------------------
// stepInstruction — advance until one real (non-squashed) instruction exits WB
// ---------------------------------------------------------------------------
int Pipeline::stepInstruction() {
    if (done_) return 0;
    int cyclesBefore = cycle_;

    // Tick until a non-squashed instruction completes WB, or until done
    while (!done_) {
        // Remember the WB latch before the tick
        bool hadRealWB = memwb_.valid && !memwb_.squashed &&
                         memwb_.instr.op != Opcode::NOP;
        tick();
        // After the tick, check if WB just consumed a real instruction
        // (memwb_ is now cleared/replaced; the old value was consumed in WB)
        if (hadRealWB) break;
        if (done_) break;
    }
    return cycle_ - cyclesBefore;
}

// ---------------------------------------------------------------------------
// tick()
// ---------------------------------------------------------------------------
bool Pipeline::tick() {
    if (done_) return false;
    cycle_++;

    wbPC_  = (memwb_.valid && !memwb_.squashed) ? memwb_.pc  : -1;
    memPC_ = (exmem_.valid && !exmem_.squashed) ? exmem_.pc  : -1;
    exPC_  = (idex_.valid  && !idex_.squashed)  ? idex_.pc   : -1;
    idPC_  = (ifid_.valid  && !ifid_.squashed)  ? ifid_.pc   : -1;
    ifPC_  = (!haltInPipeline_ && pc_ < programEndPc_) ? pc_ : -1;

    int savedPc = pc_;

    // ---- WB ----
    MEMWBReg nextMemwb = {};
    if (memwb_.valid) {
        if (memwb_.squashed) {
            wbLabel_ = memwb_.instr.label + " [squashed]";
        } else {
            wbLabel_ = memwb_.instr.label;
            if (memwb_.instr.op == Opcode::HALT) { done_ = true; return false; }
            if (writesRegister(memwb_.instr) && memwb_.instr.rd != 0)
                rf_.write(memwb_.instr.rd, memwb_.result);
            if (memwb_.writesFlags) {
                int cur = rf_.read(REG_FLAGS);
                rf_.write(REG_FLAGS, (cur & ~0xF) | (memwb_.flagsValue & 0xF));
            }
        }
    } else { wbLabel_ = "---"; }

    // ---- MEM ----
    bool memNeedsAccess = exmem_.valid && !exmem_.squashed &&
        (exmem_.instr.op == Opcode::LOAD || exmem_.instr.op == Opcode::STORE);
    if (memNeedsAccess)
        mem_->cancelRequestFrom(MemIF::StageId::IF);

    bool memStall = doMEM(nextMemwb);

    // ---- EX ----
    EXMEMReg nextExmem = {};
    int  branchTarget = 0;
    bool branchTaken  = doEX(nextExmem, branchTarget);

    // ---- Hazard detection ----
    bool dataHazard = hasDataHazard();

    // ---- ID ----
    IDEXReg nextIdex = {};
    if (dataHazard || memStall) {
        idLabel_ = ifid_.valid ? (ifid_.instr.label + " [stall]") : "---";
    } else if (ifid_.valid) {
        idLabel_ = ifid_.squashed
            ? (ifid_.instr.label + " [squashed]")
            : ifid_.instr.label;
        nextIdex.instr    = ifid_.instr;
        nextIdex.pc       = ifid_.pc;
        nextIdex.rv1      = rf_.read(ifid_.instr.rs1);
        nextIdex.rv2      = rf_.read(ifid_.instr.rs2);
        nextIdex.rvTgt    = rf_.read(ifid_.instr.rd);
        nextIdex.valid    = true;
        nextIdex.squashed = ifid_.squashed;
    } else { idLabel_ = "---"; }

    // ---- IF ----
    // No-overlap mode: do not fetch a new instruction while any downstream
    // stage (ID, EX, MEM, WB) still holds a valid latch.  This keeps exactly
    // one instruction in the pipeline at a time.
    IFIDReg nextIfid = {};
    bool    ifStall  = false;

    bool noOverlapBlock = noOverlap_ && downstreamBusy();

    if (dataHazard || memStall) {
        nextIfid = ifid_;
        ifLabel_ = ifid_.valid
            ? (ifid_.instr.label + (ifid_.squashed ? " [squashed]" : "") + " [frozen]")
            : "---";
    } else if (noOverlapBlock) {
        // No-overlap stall: pipeline has an instruction ahead, wait for it to drain
        ifStall  = true;
        ifLabel_ = "--- [waiting for drain]";
    } else if (haltInPipeline_ || memNeedsAccess) {
        ifStall  = true;
        ifLabel_ = haltInPipeline_
            ? "--- [halt drain]"
            : ("PC" + std::to_string(pc_) + " [IF blocked]");
    } else if (pc_ < programEndPc_) {
        auto r = mem_->load(pc_, MemIF::StageId::IF);
        if (r.wait) {
            ifStall  = true;
            ifLabel_ = "PC" + std::to_string(pc_) + " [IF wait]";
        } else {
            Instruction instr = decode(r.value);
            if (instr.op == Opcode::HALT) haltInPipeline_ = true;
            nextIfid.instr    = instr;
            nextIfid.pc       = pc_;
            nextIfid.valid    = true;
            nextIfid.squashed = false;
            ifLabel_ = instr.label;
            pc_++;
        }
    } else { ifLabel_ = "---"; }

    // ---- Branch squash ----
    if (branchTaken) {
        mem_->cancelRequestFrom(MemIF::StageId::IF);
        if (nextIfid.valid) {
            nextIfid.squashed = true;
            ifLabel_ = nextIfid.instr.label + " [squashed]";
        }
        if (nextIdex.valid) {
            nextIdex.squashed = true;
            idLabel_ = nextIdex.instr.label + " [squashed]";
        }
        pc_ = branchTarget;
        haltInPipeline_ = false;
    }

    // ---- MEM stall ----
    if (memStall) {
        pc_ = savedPc; nextIfid = ifid_; nextIdex = idex_; nextExmem = exmem_;
        auto sq = [](bool s){ return s ? " [squashed]" : ""; };
        ifLabel_  = ifid_.valid  ? (ifid_.instr.label  + sq(ifid_.squashed)  + " [mem stall]") : "---";
        idLabel_  = idex_.valid  ? (idex_.instr.label  + sq(idex_.squashed)  + " [mem stall]") : "---";
        exLabel_  = idex_.valid  ? (idex_.instr.label  + sq(idex_.squashed)  + " [mem stall]") : "--- [mem stall]";
    }

    // ---- Commit ----
    memwb_ = nextMemwb; exmem_ = nextExmem; idex_ = nextIdex; ifid_ = nextIfid;

    if (!haltInPipeline_ && pc_ >= programEndPc_ && !ifStall &&
        !ifid_.valid && !idex_.valid && !exmem_.valid && !memwb_.valid)
        done_ = true;

    return !done_;
}

// ---------------------------------------------------------------------------
// MEM stage
// ---------------------------------------------------------------------------
bool Pipeline::doMEM(MEMWBReg& next) {
    if (!exmem_.valid) { memLabel_ = "---"; return false; }
    memLabel_ = exmem_.squashed
        ? (exmem_.instr.label + " [squashed]")
        : exmem_.instr.label;

    if (exmem_.squashed) {
        next.instr = exmem_.instr; next.pc = exmem_.pc;
        next.result = exmem_.aluResult; next.flagsValue = 0;
        next.writesFlags = false; next.valid = true; next.squashed = true;
        return false;
    }

    switch (exmem_.instr.op) {
        case Opcode::LOAD: {
            auto r = mem_->load(exmem_.aluResult, MemIF::StageId::MEM);
            if (r.wait) { memLabel_ += " [mem wait]"; return true; }
            next.instr = exmem_.instr; next.pc = exmem_.pc;
            next.result = r.value; next.flagsValue = exmem_.flagsValue;
            next.writesFlags = exmem_.writesFlags; next.valid = true; next.squashed = false;
            return false;
        }
        case Opcode::STORE: {
            auto r = mem_->store(exmem_.aluResult, MemIF::StageId::MEM, exmem_.storeValue);
            if (r.wait) { memLabel_ += " [mem wait]"; return true; }
            next.instr = exmem_.instr; next.pc = exmem_.pc;
            next.result = 0; next.flagsValue = 0;
            next.writesFlags = false; next.valid = true; next.squashed = false;
            return false;
        }
        default:
            next.instr = exmem_.instr; next.pc = exmem_.pc;
            next.result = exmem_.aluResult; next.flagsValue = exmem_.flagsValue;
            next.writesFlags = exmem_.writesFlags; next.valid = true; next.squashed = false;
            return false;
    }
}

// ---------------------------------------------------------------------------
// EX stage
// ---------------------------------------------------------------------------
bool Pipeline::doEX(EXMEMReg& next, int& branchTarget) {
    if (!idex_.valid) { exLabel_ = "---"; return false; }
    exLabel_ = idex_.squashed
        ? (idex_.instr.label + " [squashed]")
        : idex_.instr.label;

    if (idex_.squashed) {
        next.instr = idex_.instr; next.pc = idex_.pc;
        next.aluResult = 0; next.storeValue = 0; next.branchTarget = 0;
        next.flagsValue = 0; next.branchTaken = false;
        next.writesFlags = false; next.valid = true; next.squashed = true;
        return false;
    }

    int rv1=idex_.rv1, rv2=idex_.rv2, imm=idex_.instr.imm, pc=idex_.pc;
    int result=0; bool taken=false, wFlags=setsFlags(idex_.instr), dz=false, ov=false;
    branchTarget = 0;

    switch (idex_.instr.op) {
        case Opcode::ADD:   { int64_t r=(int64_t)rv1+rv2; result=(int)r; ov=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::SUB:   { int64_t r=(int64_t)rv1-rv2; result=(int)r; ov=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::MUL:   { int64_t r=(int64_t)rv1*rv2; result=(int)r; ov=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::DIV:   if(rv2==0){dz=true;}else{result=rv1/rv2;} break;
        case Opcode::AND:   result=rv1&rv2; break;
        case Opcode::OR:    result=rv1|rv2; break;
        case Opcode::XOR:   result=rv1^rv2; break;
        case Opcode::NOT:   result=~rv1; break;
        case Opcode::SLL:   result=rv1<<(rv2&31); break;
        case Opcode::SLA:   { int b=rv1; result=rv1<<(rv2&31); ov=(b<0)!=(result<0); break; }
        case Opcode::SRL:   result=(int)((unsigned int)rv1>>(rv2&31)); break;
        case Opcode::SRA:   result=rv1>>(rv2&31); break;
        case Opcode::CMPLT: result=(rv1<rv2)?1:0; break;
        case Opcode::CMPGT: result=(rv1>rv2)?1:0; break;
        case Opcode::CMPEQ: result=(rv1==rv2)?1:0; break;
        case Opcode::ADDI:  { int64_t r=(int64_t)rv1+imm; result=(int)r; ov=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::SUBI:  { int64_t r=(int64_t)rv1-imm; result=(int)r; ov=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::MULI:  { int64_t r=(int64_t)rv1*imm; result=(int)r; ov=(r>INT_MAX||r<INT_MIN); break; }
        case Opcode::DIVI:  if(imm==0){dz=true;}else{result=rv1/imm;} break;
        case Opcode::LOAD:  result=rv1+imm; wFlags=false; break;
        case Opcode::STORE: result=rv1+imm; wFlags=false; break;
        case Opcode::BEQ:   taken=(rv1==rv2);  branchTarget=pc+1+imm; wFlags=false; break;
        case Opcode::BEQI:  taken=(rv1==rv2);  branchTarget=idex_.rvTgt; wFlags=false; break;
        case Opcode::J:     taken=true;         branchTarget=pc+1+imm; wFlags=false; break;
        case Opcode::JR:    taken=true;         branchTarget=rv1+imm;  wFlags=false; break;
        case Opcode::JAL:   taken=true;         branchTarget=pc+1+imm; result=pc+1; wFlags=false; break;
        case Opcode::JALR:  taken=true;         branchTarget=rv1+imm;  result=pc+1; wFlags=false; break;
        case Opcode::NOP: case Opcode::HALT: wFlags=false; break;
    }

    next.instr = idex_.instr; next.pc = pc;
    next.aluResult = result; next.storeValue = rv2;
    next.branchTarget = branchTarget;
    next.flagsValue = computeFlags(result, dz, ov);
    next.branchTaken = taken; next.writesFlags = wFlags;
    next.valid = true; next.squashed = false;
    return taken;
}

// ---------------------------------------------------------------------------
// Data hazard detection — squashed instructions are not producers
// ---------------------------------------------------------------------------
bool Pipeline::hasDataHazard() const {
    if (!ifid_.valid || ifid_.squashed) return false;
    const auto& c = ifid_.instr;
    auto conflicts = [&](int rd) -> bool {
        if (rd == 0) return false;
        if (rd == c.rs1 || rd == c.rs2) return true;
        if (c.op == Opcode::BEQI && rd == c.rd) return true;
        return false;
    };
    if (idex_.valid  && !idex_.squashed  && writesRegister(idex_.instr)  && conflicts(idex_.instr.rd))  return true;
    if (exmem_.valid && !exmem_.squashed && writesRegister(exmem_.instr) && conflicts(exmem_.instr.rd)) return true;
    return false;
}

bool Pipeline::writesRegister(const Instruction& i) {
    switch (i.op) {
        case Opcode::ADD: case Opcode::SUB: case Opcode::MUL: case Opcode::DIV:
        case Opcode::AND: case Opcode::OR:  case Opcode::XOR: case Opcode::NOT:
        case Opcode::SLL: case Opcode::SLA: case Opcode::SRL: case Opcode::SRA:
        case Opcode::CMPLT: case Opcode::CMPGT: case Opcode::CMPEQ:
        case Opcode::ADDI: case Opcode::SUBI: case Opcode::MULI: case Opcode::DIVI:
        case Opcode::LOAD: case Opcode::JAL: case Opcode::JALR: return true;
        default: return false;
    }
}
bool Pipeline::setsFlags(const Instruction& i) {
    switch (i.op) {
        case Opcode::ADD: case Opcode::SUB: case Opcode::MUL: case Opcode::DIV:
        case Opcode::AND: case Opcode::OR:  case Opcode::XOR: case Opcode::NOT:
        case Opcode::SLL: case Opcode::SLA: case Opcode::SRL: case Opcode::SRA:
        case Opcode::CMPLT: case Opcode::CMPGT: case Opcode::CMPEQ:
        case Opcode::ADDI: case Opcode::SUBI: case Opcode::MULI: case Opcode::DIVI:
            return true;
        default: return false;
    }
}
int Pipeline::computeFlags(int r, bool dz, bool ov) {
    int f=0;
    if(r==0)f|=FLAG_ZERO; if(r<0)f|=FLAG_NEGATIVE;
    if(ov)f|=FLAG_OVERFLOW; if(dz)f|=FLAG_DIVZERO;
    return f;
}
void Pipeline::dump() const {
    const int w=28;
    std::cout<<"Cycle "<<std::setw(3)<<cycle_
             <<" | IF: "<<std::setw(w)<<std::left<<ifLabel_
             <<"| ID: "<<std::setw(w)<<idLabel_
             <<"| EX: "<<std::setw(w)<<exLabel_
             <<"| MEM: "<<std::setw(w)<<memLabel_
             <<"| WB: "<<wbLabel_<<std::right<<"\n";
}
