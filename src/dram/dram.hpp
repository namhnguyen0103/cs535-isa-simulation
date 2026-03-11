#pragma once

#include <vector>
#include <utility>
#include <stdexcept>

class DRAM {
public:
    using Line = std::vector<int>;

    struct LoadResult {
        bool wait;
        Line line;
    };

    struct StoreResult {
        bool wait;
    };

    DRAM(int numLines, int lineSize, int delay);

    LoadResult load(int address, int requesterId);
    StoreResult store(int address, int requesterId, const Line& newLine);

    bool busy() const;
    void reset();

    int getNumLines() const;
    int getLineSize() const;
    int getDelay() const;

    void setLineDirect(int address, const Line& line);

    // Optional debug helpers
    Line peekLine(int address) const;
    void dump() const;
    void dump(int startLine, int endLine) const;

private:
    enum class RequestType {
        NONE,
        LOAD,
        STORE
    };

    struct ActiveRequest {
        RequestType type = RequestType::NONE;
        int requesterId = -1;
        int address = 0;
        int lineIndex = 0;
        int cyclesLeft = 0;
        Line pendingStoreLine;
    };

    int numLines_;
    int lineSize_;
    int delay_;

    std::vector<Line> memory_;
    ActiveRequest active_;

    int normalizeAddress(int address) const;
    int getLineIndex(int address) const;
    int getOffset(int address) const;
    int totalWords() const;
};