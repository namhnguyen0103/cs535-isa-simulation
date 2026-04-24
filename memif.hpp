#pragma once

// ---------------------------------------------------------------------------
// MemIF — abstract word-level memory interface
//
// Both Cache and DirectMemIF implement this so the pipeline and sequential
// executor work identically in all four modes — no special-casing needed.
//
// Protocol (same as Cache's existing protocol):
//   - Caller calls load() or store() every cycle until wait == false.
//   - The implementation tracks who started a request via requesterId.
//   - A different stage calling while the interface is busy gets wait=true.
// ---------------------------------------------------------------------------
class MemIF {
public:
    enum class StageId {
        NONE = -1,   // idle / operation just completed
        IF   =  0,   // Instruction Fetch stage
        MEM  =  1,   // Memory stage (LOADs and STOREs)
    };

    struct LoadResult {
        bool    wait;
        int     value;
        StageId activeStage;
    };

    struct StoreResult {
        bool    wait;
        StageId activeStage;
    };

    virtual ~MemIF() = default;

    virtual LoadResult  load (int address, StageId stage) = 0;
    virtual StoreResult store(int address, StageId stage, int value) = 0;

    // Cancel in-progress request if owned by the given stage.
    // Pipeline MEM calls this before taking the interface from IF.
    virtual void cancelRequestFrom(StageId stage) { (void)stage; }

    virtual bool busy()  const = 0;
    virtual void reset()       = 0;
};
