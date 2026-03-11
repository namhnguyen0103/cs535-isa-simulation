#include "dram.hpp"

#include <iostream>
#include <iomanip>
#include <bitset>

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

    // Start request if idle
    if (!busy()) {
        active_.type = RequestType::LOAD;
        active_.requesterId = requesterId;
        active_.address = normalized;
        active_.lineIndex = lineIndex;
        active_.cyclesLeft = delay_;
        active_.pendingStoreLine.clear();
    }

    // Must be same requester and same request to continue
    if (active_.requesterId != requesterId ||
        active_.type != RequestType::LOAD ||
        active_.address != normalized) {
        return {true, {}};
    }

    // Advance one cycle
    if (active_.cyclesLeft > 0) {
        --active_.cyclesLeft;

        if (active_.cyclesLeft == 0) {
            Line result = memory_[active_.lineIndex];
            reset();
            return {false, result};
        }

        return {true, {}};
    }

    // Safety fallback
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

    // Start request if idle
    if (!busy()) {
        active_.type = RequestType::STORE;
        active_.requesterId = requesterId;
        active_.address = normalized;
        active_.lineIndex = lineIndex;
        active_.cyclesLeft = delay_;
        active_.pendingStoreLine = newLine;
    }

    // Must be same requester and same request to continue
    if (active_.requesterId != requesterId ||
        active_.type != RequestType::STORE ||
        active_.address != normalized) {
        return {true};
    }

    // Advance one cycle
    if (active_.cyclesLeft > 0) {
        --active_.cyclesLeft;

        if (active_.cyclesLeft == 0) {
            memory_[active_.lineIndex] = active_.pendingStoreLine;
            reset();
            return {false};
        }

        return {true};
    }

    // Safety fallback
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

void DRAM::setLineDirect(int address, const Line& line) {
    if (static_cast<int>(line.size()) != lineSize_) {
        throw std::invalid_argument("line size does not match DRAM line size");
    }

    int normalized = normalizeAddress(address);
    int lineIndex = getLineIndex(normalized);
    memory_[lineIndex] = line;
}

DRAM::Line DRAM::peekLine(int address) const {
    int normalized = normalizeAddress(address);
    int lineIndex = getLineIndex(normalized);
    return memory_[lineIndex];
}

void DRAM::dump() const {
    std::cout << "DRAM contents:\n";
    for (int i = 0; i < numLines_; ++i) {
        std::cout << "Line " << std::setw(2) << i << " | ";
        for (int j = 0; j < lineSize_; ++j) {
            std::cout << "0x"
                      << std::hex
                      << std::setw(8)
                      << std::setfill('0')
                      << memory_[i][j]
                      << std::dec
                      << " ";
        }
        std::cout << "\n";
    }
}

void DRAM::dump(int startLine, int endLine) const {
    if (startLine < 0 || endLine < 0 || startLine >= numLines_ || endLine >= numLines_ || endLine < startLine) {
        std::cout << "Invalid start line index or end line index\n"
                  << "DRAM contains " << numLines_ << " lines\n";
        std::cout << "\n";
        return;
    }

    std::cout << "DRAM contents:\n";
    for (int i = startLine; i <= endLine; ++i) {
        std::cout << "Line " << std::setw(2) << i << " | ";
        for (int j = 0; j < lineSize_; ++j) {
            std::cout << "0x"
                      << std::hex
                      << std::setw(8)
                      << std::setfill('0')
                      << memory_[i][j]
                      << std::dec
                      << " ";
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