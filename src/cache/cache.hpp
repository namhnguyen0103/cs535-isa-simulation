#pragma once

#include "../dram/dram.hpp"

#include <vector>
#include <stdexcept>

class Cache {
public:
    using Line = std::vector<int>;

    enum class WritePolicy {
        WRITE_THROUGH,
        WRITE_BACK
    };

    enum class AllocatePolicy {
        WRITE_ALLOCATE,
        NO_WRITE_ALLOCATE
    };

    struct LoadResult {
        bool wait;
        int value;
    };

    struct StoreResult {
        bool wait;
    };

    Cache(int numLines,
          int lineSize,
          int delay,
          DRAM* lowerLevel,
          WritePolicy writePolicy,
          AllocatePolicy allocatePolicy);

    LoadResult load(int address, int requesterId);
    StoreResult store(int address, int requesterId, int value);

    bool busy() const;
    void reset();
    void cancelCurrentRequest();

    int getNumLines() const;
    int getLineSize() const;
    int getDelay() const;

    // Optional debug helpers
    void dump() const;
    void dump(int startLine, int endLine) const;

private:
    struct CacheLine {
        bool valid = false;
        bool dirty = false;
        int tag = 0;
        Line data;
    };

    enum class RequestType {
        NONE,
        LOAD,
        STORE
    };

    struct ActiveRequest {
        RequestType type = RequestType::NONE;
        int requesterId = -1;
        int address = 0;

        int index = 0;
        int tag = 0;
        int offset = 0;

        int cyclesLeft = 0;

        int storeValue = 0;

        bool miss = false;
        bool waitingOnLower = false;
        bool hasFetchedLine = false;
        Line fetchedLine;
    };

    int numLines_;
    int lineSize_;
    int delay_;

    DRAM* lowerLevel_;
    WritePolicy writePolicy_;
    AllocatePolicy allocatePolicy_;

    std::vector<CacheLine> lines_;
    ActiveRequest active_;

    int normalizeAddress(int address) const;
    int getIndex(int address) const;
    int getOffset(int address) const;
    int getTag(int address) const;
    int getLineBaseAddress(int address) const;
    int totalWords() const;

    bool isHit(int index, int tag) const;
};