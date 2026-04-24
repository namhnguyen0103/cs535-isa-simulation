#include "seqexecutor.hpp"
#include <climits>

SequentialExecutor::SequentialExecutor(int programBase, int programSize, MemIF* mem)
    : programBase_(programBase),
      programEndPc_(programBase + programSize),
      mem_(mem),
      pc_(programBase) {}

// ---------------------------------------------------------------------------
// step() — fetch, decode, execute, writeback one instruction
// ---------------------------------------------------------------------------
int SequentialExecutor::step() {
    if (done_) return 0;

    int cycles = 0;

    // ---- Fetch: load the encoded word from memory ----
    int encoded = 0;
    cycles += fetchInstruction(pc_, encoded);
    Instruction instr = decode(encoded);

    // ---- Decode: read register values ----
    int rv1  = rf_.read(instr.rs1);
    int rv2  = rf_.read(instr.rs2);
    int rvTgt = rf_.read(instr.rd);   // used by BEQI as branch target

    // ---- Execute + Memory ----
    int  result       = 0;
    int  storeValue   = rv2;
    bool branchTaken  = false;
    int  branchTarget = 0;
    bool writesReg    = false;
    bool isHalt       = false;
    bool writesFlags  = false;
    int  flagsValue   = 0;

    auto setFlags = [&](int r, bool dz = false, bool ov = false) {
        writesFlags = true;
        int f = 0;
        if (r == 0)  f |= FLAG_ZERO;
        if (r <  0)  f |= FLAG_NEGATIVE;
        if (ov)      f |= FLAG_OVERFLOW;
        if (dz)      f |= FLAG_DIVZERO;
        flagsValue = f;
    };

    switch (instr.op) {
        // R-type
        case Opcode::ADD:  { int64_t r=(int64_t)rv1+rv2; result=(int)r; writesReg=true; setFlags(result,false,r>INT_MAX||r<INT_MIN); break; }
        case Opcode::SUB:  { int64_t r=(int64_t)rv1-rv2; result=(int)r; writesReg=true; setFlags(result,false,r>INT_MAX||r<INT_MIN); break; }
        case Opcode::MUL:  { int64_t r=(int64_t)rv1*rv2; result=(int)r; writesReg=true; setFlags(result,false,r>INT_MAX||r<INT_MIN); break; }
        case Opcode::DIV:  if(rv2==0){result=0;writesReg=true;setFlags(0,true);}else{result=rv1/rv2;writesReg=true;setFlags(result);} break;
        case Opcode::AND:  result=rv1&rv2; writesReg=true; setFlags(result); break;
        case Opcode::OR:   result=rv1|rv2; writesReg=true; setFlags(result); break;
        case Opcode::XOR:  result=rv1^rv2; writesReg=true; setFlags(result); break;
        case Opcode::NOT:  result=~rv1;    writesReg=true; setFlags(result); break;
        case Opcode::SLL:  result=rv1<<(rv2&31); writesReg=true; setFlags(result); break;
        case Opcode::SLA:  { int b=rv1; result=rv1<<(rv2&31); writesReg=true; setFlags(result,false,(b<0)!=(result<0)); break; }
        case Opcode::SRL:  result=(int)((unsigned)rv1>>(rv2&31)); writesReg=true; setFlags(result); break;
        case Opcode::SRA:  result=rv1>>(rv2&31); writesReg=true; setFlags(result); break;
        case Opcode::CMPLT: result=(rv1< rv2)?1:0; writesReg=true; setFlags(result); break;
        case Opcode::CMPGT: result=(rv1> rv2)?1:0; writesReg=true; setFlags(result); break;
        case Opcode::CMPEQ: result=(rv1==rv2)?1:0; writesReg=true; setFlags(result); break;

        // I-type
        case Opcode::ADDI: { int64_t r=(int64_t)rv1+instr.imm; result=(int)r; writesReg=true; setFlags(result,false,r>INT_MAX||r<INT_MIN); break; }
        case Opcode::SUBI: { int64_t r=(int64_t)rv1-instr.imm; result=(int)r; writesReg=true; setFlags(result,false,r>INT_MAX||r<INT_MIN); break; }
        case Opcode::MULI: { int64_t r=(int64_t)rv1*instr.imm; result=(int)r; writesReg=true; setFlags(result,false,r>INT_MAX||r<INT_MIN); break; }
        case Opcode::DIVI: if(instr.imm==0){result=0;writesReg=true;setFlags(0,true);}else{result=rv1/instr.imm;writesReg=true;setFlags(result);} break;

        // M-type — address is computed in EX, then memory is accessed
        case Opcode::LOAD: {
            int addr = rv1 + instr.imm;
            int val  = 0;
            cycles += doLoad(addr, val);
            result   = val;
            writesReg = true;
            break;
        }
        case Opcode::STORE: {
            int addr = rv1 + instr.imm;
            cycles += doStore(addr, rv2);
            writesReg = false;
            break;
        }

        // Control
        case Opcode::BEQ:
            if (rv1 == rv2) { branchTaken = true; branchTarget = pc_ + 1 + instr.imm; }
            break;
        case Opcode::BEQI:
            if (rv1 == rv2) { branchTaken = true; branchTarget = rvTgt; }
            break;
        case Opcode::J:
            branchTaken = true; branchTarget = pc_ + 1 + instr.imm;
            break;
        case Opcode::JR:
            branchTaken = true; branchTarget = rv1 + instr.imm;
            break;
        case Opcode::JAL:
            branchTaken = true; branchTarget = pc_ + 1 + instr.imm;
            result = pc_ + 1; writesReg = true;
            break;
        case Opcode::JALR:
            branchTaken = true; branchTarget = rv1 + instr.imm;
            result = pc_ + 1; writesReg = true;
            break;

        case Opcode::NOP:  break;
        case Opcode::HALT: isHalt = true; break;
    }

    // ---- Non-memory instructions cost at least 1 cycle ----
    if (instr.op != Opcode::LOAD && instr.op != Opcode::STORE)
        cycles += 1;

    // ---- Write back ----
    if (writesReg && instr.rd != 0)
        rf_.write(instr.rd, result);

    if (writesFlags) {
        int cur = rf_.read(REG_FLAGS);
        rf_.write(REG_FLAGS, (cur & ~0xF) | (flagsValue & 0xF));
    }

    // ---- Update PC ----
    lastLabel_ = instr.label;
    lastPC_    = pc_;
    cycle_    += cycles;

    if (isHalt) { done_ = true; return cycles; }

    if (branchTaken)
        pc_ = branchTarget;
    else
        pc_++;

    if (pc_ >= programEndPc_) done_ = true;

    return cycles;
}

// ---------------------------------------------------------------------------
// Memory helpers — each call to mem_ counts as 1 cycle
// ---------------------------------------------------------------------------
int SequentialExecutor::fetchInstruction(int address, int& wordOut) {
    int cycles = 0;
    while (true) {
        auto r = mem_->load(address, MemIF::StageId::IF);
        cycles++;
        if (!r.wait) { wordOut = r.value; return cycles; }
    }
}

int SequentialExecutor::doLoad(int address, int& valueOut) {
    int cycles = 0;
    while (true) {
        auto r = mem_->load(address, MemIF::StageId::MEM);
        cycles++;
        if (!r.wait) { valueOut = r.value; return cycles; }
    }
}

int SequentialExecutor::doStore(int address, int value) {
    int cycles = 0;
    while (true) {
        auto r = mem_->store(address, MemIF::StageId::MEM, value);
        cycles++;
        if (!r.wait) return cycles;
    }
}
