#pragma once
#include <string>

// ---------------------------------------------------------------------------
// The four simulation modes
// ---------------------------------------------------------------------------
enum class SimMode {
    NO_PIPE_NO_CACHE = 0,  // Sequential execution, DRAM direct
    PIPE_ONLY        = 1,  // Pipeline + DRAM direct (no cache)
    CACHE_ONLY       = 2,  // Sequential execution + cache
    BOTH             = 3,  // Pipeline + cache (full simulation)
};

inline std::string simModeName(SimMode m) {
    switch (m) {
        case SimMode::NO_PIPE_NO_CACHE: return "No Pipe / No Cache";
        case SimMode::PIPE_ONLY:        return "Pipeline Only";
        case SimMode::CACHE_ONLY:       return "Cache Only";
        case SimMode::BOTH:             return "Pipeline + Cache";
    }
    return "";
}
