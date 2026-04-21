#pragma once

#include "dram.hpp"

#include <vector>
#include <stdexcept>

class Cache {
public:
    using Line = std::vector<int>;

    enum class StageId {
        NONE = -1,
        IF   =  0,
        MEM  =  1,
    };

    enum class WritePolicy {
        WRITE_THROUGH,
        WRITE_BACK
    };

    enum class AllocatePolicy {
        WRITE_ALLOCATE,
        NO_WRITE_ALLOCATE
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

    // GUI accessor — snapshot of one cache line for display
    struct CacheLineInfo {
        bool valid = false;
        bool dirty = false;
        int  tag   = 0;
        std::vector<int> data;
    };

    Cache(int numLines,
          int lineSize,
          int delay,
          DRAM* lowerLevel,
          WritePolicy writePolicy,
          AllocatePolicy allocatePolicy);

    LoadResult  load (int address, StageId stage);
    StoreResult store(int address, StageId stage, int value);

    // Cancel the active request only if owned by the given stage.
    void cancelRequestFrom(StageId stage);

    bool busy() const;
    void reset();
    void cancelCurrentRequest();

    int getNumLines() const;
    int getLineSize() const;
    int getDelay()    const;

    // GUI: read a cache line by index for display
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
        int         index       = 0;
        int         tag         = 0;
        int         offset      = 0;
        int         cyclesLeft  = 0;
        int         storeValue  = 0;

        bool miss           = false;
        bool waitingOnLower = false;

        // Dirty victim writeback state
        bool hasFlushedDirty   = false;   // true once dirty eviction is done
        int  dirtyFlushAddress = 0;       // DRAM address of the dirty victim line

        // New line fetch state
        bool hasFetchedLine = false;
        Line fetchedLine;
    };

    int numLines_;
    int lineSize_;
    int delay_;

    DRAM*          lowerLevel_;
    WritePolicy    writePolicy_;
    AllocatePolicy allocatePolicy_;

    std::vector<CacheLine> lines_;
    ActiveRequest          active_;

    int  normalizeAddress  (int address) const;
    int  getIndex          (int address) const;
    int  getOffset         (int address) const;
    int  getTag            (int address) const;
    int  getLineBaseAddress(int address) const;
    int  totalWords        ()            const;
    bool isHit             (int index, int tag) const;

    // Returns the DRAM base address of the line currently at cache index `idx`
    int victimBaseAddress(int idx) const;

    StageId getActiveStage() const;
};
