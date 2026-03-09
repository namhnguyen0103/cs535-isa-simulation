#include "dram.hpp"

#include <iostream>
#include <iomanip>

DRAM::DRAM(int numLines, int lineSize, int delay)
    : numLines_(numLines),
      lineSize_(lineSize),
      delay_(delay),
      memory_(numLines, Line(lineSize, 0)) {
    if (numLines_ <= 0) {
        throw std::invalid_argument("numLines must be > 0");
    }
    if (lineSize_ <= 0) {
        throw std::invalid_argument("lineSize must be > 0");
    }
    if (delay_ < 0) {
        throw std::invalid_argument("delay must be >= 0");
    }
}

DRAM::LoadResult DRAM::load(int address, int requesterId) {
    int normalized = normalizeAddress(address);
    int lineIndex = getLineIndex(normalized);

    // If DRAM is idle, start servicing this load
    if (!busy()) {
        active_.type = RequestType::LOAD;
        active_.requesterId = requesterId;
        active_.address = normalized;
        active_.lineIndex = lineIndex;
        active_.cyclesLeft = delay_;
        active_.pendingStoreLine.clear();
        return {true, {}};
    }

    // If a different requester is trying to use DRAM, do not decrement
    if (active_.requesterId != requesterId || active_.type != RequestType::LOAD || active_.address != normalized) {
        return {true, {}};
    }

    // Same requester, same request: continue progress
    if (active_.cyclesLeft > 0) {
        --active_.cyclesLeft;
        return {true, {}};
    }

    // Done
    Line result = memory_[active_.lineIndex];
    reset();
    return {false, result};
}

DRAM::StoreResult DRAM::store(int address, int requesterId, const Line& newLine) {
    if (static_cast<int>(newLine.size()) != lineSize_) {
        throw std::invalid_argument("store line size does not match DRAM line size");
    }

    int normalized = normalizeAddress(address);
    int lineIndex = getLineIndex(normalized);

    // If DRAM is idle, start servicing this store
    if (!busy()) {
        active_.type = RequestType::STORE;
        active_.requesterId = requesterId;
        active_.address = normalized;
        active_.lineIndex = lineIndex;
        active_.cyclesLeft = delay_;
        active_.pendingStoreLine = newLine;
        return {true};
    }

    // If a different requester is trying to use DRAM, do not decrement
    if (active_.requesterId != requesterId || active_.type != RequestType::STORE || active_.address != normalized) {
        return {true};
    }

    // Same requester, same request: continue progress
    if (active_.cyclesLeft > 0) {
        --active_.cyclesLeft;
        return {true};
    }

    // Done
    memory_[active_.lineIndex] = active_.pendingStoreLine;
    reset();
    return {false};
}

bool DRAM::busy() const {
    return active_.type != RequestType::NONE;
}

void DRAM::reset() {
    active_ = ActiveRequest{};
}

int DRAM::getNumLines() const {
    return numLines_;
}

int DRAM::getLineSize() const {
    return lineSize_;
}

int DRAM::getDelay() const {
    return delay_;
}

DRAM::Line DRAM::peekLine(int address) const {
    int normalized = normalizeAddress(address);
    int lineIndex = getLineIndex(normalized);
    return memory_[lineIndex];
}

void DRAM::dump() const {
    std::cout << "DRAM contents:\n";
    for (int i = 0; i < numLines_; ++i) {
        std::cout << "Line " << std::setw(2) << i << ": ";
        for (int j = 0; j < lineSize_; ++j) {
            std::cout << memory_[i][j] << " ";
        }
        std::cout << "\n";
    }
}

int DRAM::normalizeAddress(int address) const {
    int words = totalWords();

    // Safe modulo for negative values too
    int normalized = address % words;
    if (normalized < 0) {
        normalized += words;
    }
    return normalized;
}

int DRAM::getLineIndex(int address) const {
    return (address / lineSize_) % numLines_;
}

int DRAM::getOffset(int address) const {
    return address % lineSize_;
}

int DRAM::totalWords() const {
    return numLines_ * lineSize_;
}