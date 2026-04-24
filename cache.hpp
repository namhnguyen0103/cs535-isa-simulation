#pragma once

#include "memif.hpp"
#include "dram.hpp"

#include <vector>
#include <stdexcept>

class Cache : public MemIF {
public:
    using Line = std::vector<int>;

    // Re-export so existing code using Cache::StageId still compiles
    using StageId     = MemIF::StageId;
    using LoadResult  = MemIF::LoadResult;
    using StoreResult = MemIF::StoreResult;

    enum class WritePolicy    { WRITE_THROUGH, WRITE_BACK };
    enum class AllocatePolicy { WRITE_ALLOCATE, NO_WRITE_ALLOCATE };

    // Snapshot of one cache line — for GUI display
    struct CacheLineInfo {
        bool valid = false;
        bool dirty = false;
        int  tag   = 0;
        std::vector<int> data;
    };

    Cache(int numLines, int lineSize, int delay,
          DRAM* lowerLevel,
          WritePolicy    writePolicy,
          AllocatePolicy allocatePolicy);

    // MemIF interface
    LoadResult  load (int address, StageId stage) override;
    StoreResult store(int address, StageId stage, int value) override;
    void cancelRequestFrom(StageId stage) override;
    bool busy()  const override;
    void reset()       override;

    void cancelCurrentRequest();
    int  getNumLines() const;
    int  getLineSize() const;
    int  getDelay()    const;

    CacheLineInfo getCacheLine(int index) const;

    void dump() const;
    void dump(int startLine, int endLine) const;

private:
    struct CacheLine {
        bool valid = false;
        bool dirty = false;
        int  tag   = 0;
        Line data;
    };

    enum class RequestType { NONE, LOAD, STORE };

    struct ActiveRequest {
        RequestType type        = RequestType::NONE;
        int         requesterId = -1;
        int         address     = 0;
        int         index = 0, tag = 0, offset = 0;
        int         cyclesLeft  = 0;
        int         storeValue  = 0;
        bool        miss              = false;
        bool        hasFlushedDirty   = false;
        int         dirtyFlushAddress = 0;
        bool        hasFetchedLine    = false;
        Line        fetchedLine;
    };

    int numLines_, lineSize_, delay_;
    DRAM*          lowerLevel_;
    WritePolicy    writePolicy_;
    AllocatePolicy allocatePolicy_;

    std::vector<CacheLine> lines_;
    ActiveRequest          active_;

    int  normalizeAddress  (int a) const;
    int  getIndex          (int a) const;
    int  getOffset         (int a) const;
    int  getTag            (int a) const;
    int  getLineBaseAddress(int a) const;
    bool isHit(int index, int tag) const;
    int  victimBaseAddress (int idx) const;
    StageId getActiveStage() const;
};
