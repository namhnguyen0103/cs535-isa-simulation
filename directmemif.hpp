#pragma once

#include "memif.hpp"
#include "dram.hpp"

// ---------------------------------------------------------------------------
// DirectMemIF — implements MemIF by hitting DRAM directly, no caching.
//
// Used in NO_PIPE_NO_CACHE and PIPE_ONLY modes.
//
// Load:  wait DRAM_DELAY cycles, return the requested word.
// Store: phase 1 — read the line from DRAM (DRAM_DELAY cycles)
//        phase 2 — modify the word, write the line back (DRAM_DELAY cycles)
//
// This read-modify-write is necessary because DRAM is line-addressed but
// we need word-level stores.
// ---------------------------------------------------------------------------
class DirectMemIF : public MemIF {
public:
    explicit DirectMemIF(DRAM* dram);

    LoadResult  load (int address, StageId stage) override;
    StoreResult store(int address, StageId stage, int value) override;
    void cancelRequestFrom(StageId stage) override;
    bool busy()  const override;
    void reset()       override;

private:
    DRAM* dram_;

    enum class ReqType    { NONE, LOAD, STORE };
    enum class StorePhase { READ, WRITE };

    struct Active {
        ReqType    type        = ReqType::NONE;
        int        requesterId = -1;
        int        address     = 0;
        int        lineBase    = 0;
        int        offset      = 0;
        int        storeValue  = 0;
        StorePhase storePhase  = StorePhase::READ;
        DRAM::Line pendingLine;
    } active_;

    int normalizeAddress(int address) const;
    StageId getActiveStage() const;
};
