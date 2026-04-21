#include "cache.hpp"
#include <iostream>
#include <iomanip>

Cache::Cache(int numLines, int lineSize, int delay,
             DRAM* lowerLevel, WritePolicy writePolicy, AllocatePolicy allocatePolicy)
    : numLines_(numLines), lineSize_(lineSize), delay_(delay),
      lowerLevel_(lowerLevel), writePolicy_(writePolicy), allocatePolicy_(allocatePolicy),
      lines_(numLines) {
    if (numLines_ <= 0) throw std::invalid_argument("numLines must be > 0");
    if (lineSize_ <= 0) throw std::invalid_argument("lineSize must be > 0");
    if (delay_    <  0) throw std::invalid_argument("delay must be >= 0");
    if (!lowerLevel_)   throw std::invalid_argument("lowerLevel cannot be null");
    for (auto& line : lines_) line.data.assign(lineSize_, 0);
}

Cache::StageId Cache::getActiveStage() const {
    return static_cast<StageId>(active_.requesterId);
}

void Cache::cancelRequestFrom(StageId stage) {
    if (busy() && getActiveStage() == stage) reset();
}

// Returns the DRAM base address of whichever line is currently sitting at
// cache index `idx`.  Used when we need to write a dirty victim back.
// Formula: (tag * numLines + index) * lineSize
int Cache::victimBaseAddress(int idx) const {
    return (lines_[idx].tag * numLines_ + idx) * lineSize_;
}

Cache::CacheLineInfo Cache::getCacheLine(int index) const {
    if (index < 0 || index >= numLines_)
        throw std::out_of_range("Cache line index out of range");
    const CacheLine& l = lines_[index];
    return { l.valid, l.dirty, l.tag, l.data };
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------
Cache::LoadResult Cache::load(int address, StageId stage) {
    int requesterId = static_cast<int>(stage);
    int normalized  = normalizeAddress(address);
    int index       = getIndex (normalized);
    int tag         = getTag   (normalized);
    int offset      = getOffset(normalized);

    if (!busy()) {
        active_ = {};
        active_.type       = RequestType::LOAD;
        active_.requesterId = requesterId;
        active_.address    = normalized;
        active_.index      = index;
        active_.tag        = tag;
        active_.offset     = offset;
        active_.miss       = !isHit(index, tag);
        active_.cyclesLeft = active_.miss ? 0 : delay_;

        // Pre-compute dirty victim info now, while the victim is still intact
        if (active_.miss && lines_[index].valid && lines_[index].dirty
                         && writePolicy_ == WritePolicy::WRITE_BACK) {
            active_.dirtyFlushAddress = victimBaseAddress(index);
        }
    }

    if (active_.type        != RequestType::LOAD ||
        active_.requesterId != requesterId        ||
        active_.address     != normalized) {
        return {true, 0, getActiveStage()};
    }

    // ---- HIT ----
    if (!active_.miss) {
        if (active_.cyclesLeft > 0) {
            --active_.cyclesLeft;
            if (active_.cyclesLeft == 0) {
                int v = lines_[active_.index].data[active_.offset];
                reset(); return {false, v, StageId::NONE};
            }
            return {true, 0, getActiveStage()};
        }
        int v = lines_[active_.index].data[active_.offset];
        reset(); return {false, v, StageId::NONE};
    }

    // ---- MISS ----

    // Step 1: If there is a dirty victim, flush it to DRAM first.
    if (!active_.hasFlushedDirty) {
        CacheLine& victim = lines_[active_.index];
        if (victim.valid && victim.dirty && writePolicy_ == WritePolicy::WRITE_BACK) {
            // Write the dirty line back to DRAM — may take multiple cycles.
            auto flush = lowerLevel_->store(active_.dirtyFlushAddress,
                                            requesterId,
                                            victim.data);
            if (flush.wait) return {true, 0, getActiveStage()};

            // Writeback complete — mark the line clean so it can be safely replaced.
            victim.dirty = false;
        }
        active_.hasFlushedDirty = true;
    }

    // Step 2: Fetch the new line from DRAM.
    if (!active_.hasFetchedLine) {
        auto lower = lowerLevel_->load(getLineBaseAddress(active_.address), requesterId);
        if (lower.wait) return {true, 0, getActiveStage()};

        active_.fetchedLine    = lower.line;
        active_.hasFetchedLine = true;

        // Install the new line in the cache.
        CacheLine& victim = lines_[active_.index];
        victim = { true, false, active_.tag, active_.fetchedLine };

        active_.cyclesLeft = delay_;
    }

    // Step 3: Pay the cache access delay.
    if (active_.cyclesLeft > 0) {
        --active_.cyclesLeft;
        if (active_.cyclesLeft == 0) {
            int v = lines_[active_.index].data[active_.offset];
            reset(); return {false, v, StageId::NONE};
        }
        return {true, 0, getActiveStage()};
    }

    int v = lines_[active_.index].data[active_.offset];
    reset(); return {false, v, StageId::NONE};
}

// ---------------------------------------------------------------------------
// store
// ---------------------------------------------------------------------------
Cache::StoreResult Cache::store(int address, StageId stage, int value) {
    int requesterId = static_cast<int>(stage);
    int normalized  = normalizeAddress(address);
    int index       = getIndex (normalized);
    int tag         = getTag   (normalized);
    int offset      = getOffset(normalized);

    if (!busy()) {
        active_ = {};
        active_.type        = RequestType::STORE;
        active_.requesterId = requesterId;
        active_.address     = normalized;
        active_.index       = index;
        active_.tag         = tag;
        active_.offset      = offset;
        active_.cyclesLeft  = delay_;
        active_.storeValue  = value;
        active_.miss        = !isHit(index, tag);

        // Pre-compute dirty victim info
        if (active_.miss && lines_[index].valid && lines_[index].dirty
                         && writePolicy_ == WritePolicy::WRITE_BACK) {
            active_.dirtyFlushAddress = victimBaseAddress(index);
        }
    }

    if (active_.type        != RequestType::STORE ||
        active_.requesterId != requesterId         ||
        active_.address     != normalized) {
        return {true, getActiveStage()};
    }

    // ---- HIT ----
    if (!active_.miss) {
        if (active_.cyclesLeft > 0) { --active_.cyclesLeft; return {true, getActiveStage()}; }

        CacheLine& line = lines_[active_.index];
        line.data[active_.offset] = active_.storeValue;

        if (writePolicy_ == WritePolicy::WRITE_THROUGH) {
            auto lower = lowerLevel_->store(getLineBaseAddress(active_.address),
                                            requesterId, line.data);
            if (lower.wait) return {true, getActiveStage()};
            line.dirty = false;
        } else {
            line.dirty = true;
        }
        reset(); return {false, StageId::NONE};
    }

    // ---- MISS ----

    // Write-through + no-write-allocate: bypass cache, write directly to DRAM
    if (writePolicy_    == WritePolicy::WRITE_THROUGH &&
        allocatePolicy_ == AllocatePolicy::NO_WRITE_ALLOCATE) {
        int base        = getLineBaseAddress(active_.address);
        auto lowerLoad  = lowerLevel_->load(base, requesterId);
        if (lowerLoad.wait) return {true, getActiveStage()};
        Line updated    = lowerLoad.line;
        updated[active_.offset] = active_.storeValue;
        auto lowerStore = lowerLevel_->store(base, requesterId, updated);
        if (lowerStore.wait) return {true, getActiveStage()};
        reset(); return {false, StageId::NONE};
    }

    // Write-back + write-allocate: fetch line into cache, update, mark dirty
    if (writePolicy_    == WritePolicy::WRITE_BACK &&
        allocatePolicy_ == AllocatePolicy::WRITE_ALLOCATE) {

        // Step 1: Flush dirty victim if needed
        if (!active_.hasFlushedDirty) {
            CacheLine& victim = lines_[active_.index];
            if (victim.valid && victim.dirty) {
                auto flush = lowerLevel_->store(active_.dirtyFlushAddress,
                                                requesterId,
                                                victim.data);
                if (flush.wait) return {true, getActiveStage()};
                victim.dirty = false;
            }
            active_.hasFlushedDirty = true;
        }

        // Step 2: Fetch the line from DRAM
        if (!active_.hasFetchedLine) {
            auto lower = lowerLevel_->load(getLineBaseAddress(active_.address), requesterId);
            if (lower.wait) return {true, getActiveStage()};

            active_.fetchedLine    = lower.line;
            active_.hasFetchedLine = true;

            CacheLine& victim = lines_[active_.index];
            victim = { true, true, active_.tag, active_.fetchedLine };
            victim.data[active_.offset] = active_.storeValue;
        }

        // Step 3: Cache delay
        if (active_.cyclesLeft > 0) { --active_.cyclesLeft; return {true, getActiveStage()}; }

        reset(); return {false, StageId::NONE};
    }

    throw std::runtime_error("Unsupported cache write policy combination");
}

bool Cache::busy()  const { return active_.type != RequestType::NONE; }
void Cache::reset()       { active_ = ActiveRequest{}; }
void Cache::cancelCurrentRequest() { reset(); }
int  Cache::getNumLines() const { return numLines_; }
int  Cache::getLineSize() const { return lineSize_; }
int  Cache::getDelay()    const { return delay_; }

void Cache::dump() const {
    std::cout << "Cache contents:\n";
    for (int i = 0; i < numLines_; ++i) {
        const auto& l = lines_[i];
        std::cout << "Index " << std::setw(2) << i
                  << " | V=" << l.valid << " D=" << l.dirty << " T=" << l.tag << " | ";
        for (int x : l.data)
            std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << x << std::dec << " ";
        std::cout << "\n";
    }
}

void Cache::dump(int s, int e) const {
    if (s < 0 || e >= numLines_ || e < s) {
        std::cout << "Invalid range.\n"; return;
    }
    std::cout << "Cache contents:\n";
    for (int i = s; i <= e; ++i) {
        const auto& l = lines_[i];
        std::cout << "Index " << std::setw(2) << i
                  << " | V=" << l.valid << " D=" << l.dirty << " T=" << l.tag << " | ";
        for (int x : l.data)
            std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << x << std::dec << " ";
        std::cout << "\n";
    }
}

int  Cache::normalizeAddress  (int a) const {
    int w = lowerLevel_->getNumLines() * lowerLevel_->getLineSize();
    int n = a % w;
    return n < 0 ? n + w : n;
}
int  Cache::getIndex          (int a) const { return (a / lineSize_) % numLines_; }
int  Cache::getOffset         (int a) const { return a % lineSize_; }
int  Cache::getTag            (int a) const { return a / lineSize_ / numLines_; }
int  Cache::getLineBaseAddress(int a) const { return a - getOffset(a); }
int  Cache::totalWords        ()      const { return numLines_ * lineSize_; }
bool Cache::isHit(int idx, int tag)   const { return lines_[idx].valid && lines_[idx].tag == tag; }
