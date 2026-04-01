#include "cache.hpp"

#include <iostream>
#include <iomanip>
#include <bitset>

Cache::Cache(int numLines,
             int lineSize,
             int delay,
             DRAM* lowerLevel,
             WritePolicy writePolicy,
             AllocatePolicy allocatePolicy)
    : numLines_(numLines),
      lineSize_(lineSize),
      delay_(delay),
      lowerLevel_(lowerLevel),
      writePolicy_(writePolicy),
      allocatePolicy_(allocatePolicy),
      lines_(numLines) {
    if (numLines_ <= 0) {
        throw std::invalid_argument("numLines must be > 0");
    }
    if (lineSize_ <= 0) {
        throw std::invalid_argument("lineSize must be > 0");
    }
    if (delay_ < 0) {
        throw std::invalid_argument("delay must be >= 0");
    }
    if (lowerLevel_ == nullptr) {
        throw std::invalid_argument("lowerLevel DRAM pointer cannot be null");
    }

    for (auto& line : lines_) {
        line.data.assign(lineSize_, 0);
    }
}

// Converts active_.requesterId back to a StageId
Cache::StageId Cache::getActiveStage() const {
    return static_cast<StageId>(active_.requesterId);
}

Cache::LoadResult Cache::load(int address, StageId stage) {
    // Convert StageId to int for internal tracking and DRAM calls
    int requesterId = static_cast<int>(stage);

    int normalized = normalizeAddress(address);
    int index = getIndex(normalized);
    int tag = getTag(normalized);
    int offset = getOffset(normalized);

    // Start request if idle
    if (!busy()) {
        active_.type = RequestType::LOAD;
        active_.requesterId = requesterId;
        active_.address = normalized;
        active_.index = index;
        active_.tag = tag;
        active_.offset = offset;
        active_.storeValue = 0;
        active_.miss = !isHit(index, tag);
        active_.waitingOnLower = false;
        active_.hasFetchedLine = false;
        active_.fetchedLine.clear();

        // Hit: start cache countdown immediately
        // Miss: wait for lower level first, then start cache countdown
        active_.cyclesLeft = active_.miss ? 0 : delay_;
    }

    // Must be same stage and same request to continue.
    // A different stage gets wait=true and learns which stage owns it.
    if (active_.type != RequestType::LOAD ||
        active_.requesterId != requesterId ||
        active_.address != normalized) {
        return {true, 0, getActiveStage()};
    }

    // ---------------- HIT ----------------
    if (!active_.miss) {
        if (active_.cyclesLeft > 0) {
            --active_.cyclesLeft;

            if (active_.cyclesLeft == 0) {
                int value = lines_[active_.index].data[active_.offset];
                reset();
                return {false, value, StageId::NONE};
            }

            return {true, 0, getActiveStage()};
        }

        int value = lines_[active_.index].data[active_.offset];
        reset();
        return {false, value, StageId::NONE};
    }

    // ---------------- MISS ----------------
    // First get the whole line from DRAM
    if (!active_.hasFetchedLine) {
        int lineBaseAddress = getLineBaseAddress(active_.address);
        auto lower = lowerLevel_->load(lineBaseAddress, requesterId);

        if (lower.wait) {
            return {true, 0, getActiveStage()};
        }

        active_.fetchedLine = lower.line;
        active_.hasFetchedLine = true;

        CacheLine& victim = lines_[active_.index];
        if (victim.valid && victim.dirty && writePolicy_ == WritePolicy::WRITE_BACK) {
            throw std::runtime_error("Dirty eviction during load miss not yet implemented");
        }

        victim.valid = true;
        victim.dirty = false;
        victim.tag = active_.tag;
        victim.data = active_.fetchedLine;

        // Now begin cache delay
        active_.cyclesLeft = delay_;
    }

    // Pay cache delay after fill
    if (active_.cyclesLeft > 0) {
        --active_.cyclesLeft;

        if (active_.cyclesLeft == 0) {
            int value = lines_[active_.index].data[active_.offset];
            reset();
            return {false, value, StageId::NONE};
        }

        return {true, 0, getActiveStage()};
    }

    int value = lines_[active_.index].data[active_.offset];
    reset();
    return {false, value, StageId::NONE};
}

Cache::StoreResult Cache::store(int address, StageId stage, int value) {
    // Convert StageId to int for internal tracking and DRAM calls
    int requesterId = static_cast<int>(stage);

    int normalized = normalizeAddress(address);
    int index = getIndex(normalized);
    int tag = getTag(normalized);
    int offset = getOffset(normalized);

    // Start request if idle
    if (!busy()) {
        active_.type = RequestType::STORE;
        active_.requesterId = requesterId;
        active_.address = normalized;
        active_.index = index;
        active_.tag = tag;
        active_.offset = offset;
        active_.cyclesLeft = delay_;
        active_.storeValue = value;
        active_.miss = !isHit(index, tag);
        active_.waitingOnLower = false;
        active_.hasFetchedLine = false;
        active_.fetchedLine.clear();
    }

    // Must be same stage and same request to continue.
    // A different stage gets wait=true and learns which stage owns it.
    if (active_.type != RequestType::STORE ||
        active_.requesterId != requesterId ||
        active_.address != normalized) {
        return {true, getActiveStage()};
    }

    // HIT CASE
    if (!active_.miss) {
        if (active_.cyclesLeft > 0) {
            --active_.cyclesLeft;
            return {true, getActiveStage()};
        }

        CacheLine& line = lines_[active_.index];
        line.data[active_.offset] = active_.storeValue;

        if (writePolicy_ == WritePolicy::WRITE_THROUGH) {
            // Forward entire line to DRAM
            auto lower = lowerLevel_->store(getLineBaseAddress(active_.address),
                                            requesterId,
                                            line.data);
            if (lower.wait) {
                return {true, getActiveStage()};
            }
            line.dirty = false;
        } else {
            line.dirty = true;
        }

        reset();
        return {false, StageId::NONE};
    }

    // MISS CASE

    // Write-through + no-write-allocate:
    // do not fill cache, just forward store to DRAM
    if (writePolicy_ == WritePolicy::WRITE_THROUGH &&
        allocatePolicy_ == AllocatePolicy::NO_WRITE_ALLOCATE) {
        int lineBaseAddress = getLineBaseAddress(active_.address);

        auto lowerLoad = lowerLevel_->load(lineBaseAddress, requesterId);
        if (lowerLoad.wait) {
            return {true, getActiveStage()};
        }

        Line updated = lowerLoad.line;
        updated[active_.offset] = active_.storeValue;

        auto lowerStore = lowerLevel_->store(lineBaseAddress, requesterId, updated);
        if (lowerStore.wait) {
            return {true, getActiveStage()};
        }

        reset();
        return {false, StageId::NONE};
    }

    // Write-back + write-allocate:
    // fetch line, install in cache, update word, mark dirty
    if (writePolicy_ == WritePolicy::WRITE_BACK &&
        allocatePolicy_ == AllocatePolicy::WRITE_ALLOCATE) {
        if (!active_.hasFetchedLine) {
            int lineBaseAddress = getLineBaseAddress(active_.address);
            auto lower = lowerLevel_->load(lineBaseAddress, requesterId);

            if (lower.wait) {
                return {true, getActiveStage()};
            }

            active_.fetchedLine = lower.line;
            active_.hasFetchedLine = true;

            CacheLine& victim = lines_[active_.index];
            if (victim.valid && victim.dirty) {
                throw std::runtime_error("Dirty eviction during store miss not yet implemented");
            }

            victim.valid = true;
            victim.tag = active_.tag;
            victim.data = active_.fetchedLine;
            victim.data[active_.offset] = active_.storeValue;
            victim.dirty = true;
        }

        if (active_.cyclesLeft > 0) {
            --active_.cyclesLeft;
            return {true, getActiveStage()};
        }

        reset();
        return {false, StageId::NONE};
    }

    throw std::runtime_error("Unsupported cache write policy combination");
}

bool Cache::busy() const {
    return active_.type != RequestType::NONE;
}

void Cache::reset() {
    active_ = ActiveRequest{};
}

void Cache::cancelCurrentRequest() {
    reset();
}

int Cache::getNumLines() const {
    return numLines_;
}

int Cache::getLineSize() const {
    return lineSize_;
}

int Cache::getDelay() const {
    return delay_;
}

void Cache::dump() const {
    std::cout << "Cache contents:\n";
    for (int i = 0; i < numLines_; ++i) {
        const auto& line = lines_[i];
        std::cout << "Index " << std::setw(2) << i
                  << " | V=" << line.valid
                  << " D=" << line.dirty
                  << " T=" << line.tag
                  << " | ";
        for (int x : line.data) {
            std::cout << "0x"
                      << std::hex
                      << std::setw(8)
                      << std::setfill('0')
                      << x
                      << std::dec
                      << " ";
        }
        std::cout << "\n";
    }
}

void Cache::dump(int startLine, int endLine) const {
    if (startLine < 0 || endLine < 0 || startLine >= numLines_ || endLine >= numLines_ || endLine < startLine) {
        std::cout << "Invalid start line index or end line index\n"
                  << "Cache contains " << numLines_ << " lines\n";
        std::cout << "\n";
        return;
    }

    std::cout << "Cache contents:\n";
    for (int i = startLine; i <= endLine; ++i) {
        const auto& line = lines_[i];
        std::cout << "Index " << std::setw(2) << i
                  << " | V=" << line.valid
                  << " D=" << line.dirty
                  << " T=" << line.tag
                  << " | ";
        for (int x : line.data) {
            std::cout << "0x"
                      << std::hex
                      << std::setw(8)
                      << std::setfill('0')
                      << x
                      << std::dec
                      << " ";
        }
        std::cout << "\n";
    }
}

int Cache::normalizeAddress(int address) const {
    int words = lowerLevel_->getNumLines() * lowerLevel_->getLineSize();
    int normalized = address % words;
    if (normalized < 0) {
        normalized += words;
    }
    return normalized;
}

int Cache::getIndex(int address) const {
    return (address / lineSize_) % numLines_;
}

int Cache::getOffset(int address) const {
    return address % lineSize_;
}

int Cache::getTag(int address) const {
    return address / lineSize_ / numLines_;
}

int Cache::getLineBaseAddress(int address) const {
    return address - getOffset(address);
}

int Cache::totalWords() const {
    return numLines_ * lineSize_;
}

bool Cache::isHit(int index, int tag) const {
    return lines_[index].valid && lines_[index].tag == tag;
}