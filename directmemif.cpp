#include "directmemif.hpp"

DirectMemIF::DirectMemIF(DRAM* dram) : dram_(dram) {}

int DirectMemIF::normalizeAddress(int address) const {
    int words = dram_->getNumLines() * dram_->getLineSize();
    int n = address % words;
    return n < 0 ? n + words : n;
}

DirectMemIF::StageId DirectMemIF::getActiveStage() const {
    return static_cast<StageId>(active_.requesterId);
}

bool DirectMemIF::busy() const {
    return active_.type != ReqType::NONE;
}

void DirectMemIF::reset() {
    active_ = Active{};
}

void DirectMemIF::cancelRequestFrom(StageId stage) {
    if (busy() && getActiveStage() == stage) {
        // Cancel our request and any in-progress DRAM request
        dram_->reset();
        reset();
    }
}

// ---------------------------------------------------------------------------
// load — call dram_->load() each cycle until it completes, then return word
// ---------------------------------------------------------------------------
DirectMemIF::LoadResult DirectMemIF::load(int address, StageId stage) {
    int requesterId = static_cast<int>(stage);
    int normalized  = normalizeAddress(address);
    int lineSize    = dram_->getLineSize();
    int lineBase    = normalized - (normalized % lineSize);
    int offset      = normalized % lineSize;

    if (!busy()) {
        active_             = {};
        active_.type        = ReqType::LOAD;
        active_.requesterId = requesterId;
        active_.address     = normalized;
        active_.lineBase    = lineBase;
        active_.offset      = offset;
    }

    if (active_.requesterId != requesterId ||
        active_.type        != ReqType::LOAD ||
        active_.address     != normalized) {
        return {true, 0, getActiveStage()};
    }

    // Pump the DRAM one cycle
    auto r = dram_->load(active_.lineBase, requesterId);
    if (r.wait) return {true, 0, getActiveStage()};

    // Done — extract and return the word
    int value = r.line[active_.offset];
    reset();
    return {false, value, StageId::NONE};
}

// ---------------------------------------------------------------------------
// store — two phases: read line, then write modified line back
// ---------------------------------------------------------------------------
DirectMemIF::StoreResult DirectMemIF::store(int address, StageId stage, int value) {
    int requesterId = static_cast<int>(stage);
    int normalized  = normalizeAddress(address);
    int lineSize    = dram_->getLineSize();
    int lineBase    = normalized - (normalized % lineSize);
    int offset      = normalized % lineSize;

    if (!busy()) {
        active_             = {};
        active_.type        = ReqType::STORE;
        active_.requesterId = requesterId;
        active_.address     = normalized;
        active_.lineBase    = lineBase;
        active_.offset      = offset;
        active_.storeValue  = value;
        active_.storePhase  = StorePhase::READ;
    }

    if (active_.requesterId != requesterId ||
        active_.type        != ReqType::STORE ||
        active_.address     != normalized) {
        return {true, getActiveStage()};
    }

    // Phase 1: read the existing line so we can do a word-level update
    if (active_.storePhase == StorePhase::READ) {
        auto r = dram_->load(active_.lineBase, requesterId);
        if (r.wait) return {true, getActiveStage()};

        // Got the line — modify the target word and move to write phase
        active_.pendingLine             = r.line;
        active_.pendingLine[active_.offset] = active_.storeValue;
        active_.storePhase              = StorePhase::WRITE;
    }

    // Phase 2: write the modified line back
    auto r = dram_->store(active_.lineBase, requesterId, active_.pendingLine);
    if (r.wait) return {true, getActiveStage()};

    reset();
    return {false, StageId::NONE};
}
