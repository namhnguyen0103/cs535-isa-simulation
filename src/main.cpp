#include "pipeline/pipeline.hpp"
#include "cache/cache.hpp"
#include "dram/dram.hpp"
#include "instruction/instruction.hpp"

#include <iostream>
#include <string>
#include <limits>

// -----------------------------------------------------------------------------
// Memory configuration
// -----------------------------------------------------------------------------

constexpr int DRAM_NUM_LINES = 16;
constexpr int DRAM_LINE_SIZE = 4;     // 4 words per line
constexpr int DRAM_DELAY     = 3;     // cycles

constexpr int CACHE_NUM_LINES = 4;
constexpr int CACHE_LINE_SIZE = 4;    // must match DRAM
constexpr int CACHE_DELAY     = 1;    // cycles

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Print a divider line
static void printDivider() {
    std::cout << std::string(130, '-') << "\n";
}

// Print the full system state: pipeline registers, cache, DRAM
static void printFullState(const Pipeline& pipeline,
                           const Cache&    cache,
                           const DRAM&     dram) {
    printDivider();
    pipeline.dump();
    printDivider();
    cache.dump(0, CACHE_NUM_LINES - 1);
    dram.dump(0, 3);    // only show the array lines (addresses 0-3)
    printDivider();
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main() {
    // -------------------------------------------------------------------------
    // Build memory hierarchy
    // -------------------------------------------------------------------------
    DRAM dram(DRAM_NUM_LINES, DRAM_LINE_SIZE, DRAM_DELAY);

    // Pre-load array[0..3] = {10, 20, 30, 40} into DRAM directly.
    // Word-addressed: address 0 = array[0], address 1 = array[1], etc.
    // All four words fit in DRAM line 0 (addresses 0-3).
    dram.setLineDirect(0, { 10, 20, 30, 40 });

    Cache cache(CACHE_NUM_LINES,
                CACHE_LINE_SIZE,
                CACHE_DELAY,
                &dram,
                Cache::WritePolicy::WRITE_BACK,
                Cache::AllocatePolicy::WRITE_ALLOCATE);

    // -------------------------------------------------------------------------
    // Build pipeline with the demo program
    // -------------------------------------------------------------------------
    Pipeline pipeline(makeDemoProgram(), &cache);

    // -------------------------------------------------------------------------
    // Print initial memory state
    // -------------------------------------------------------------------------
    std::cout << "\n=== Simulator Start ===\n\n";
    std::cout << "Initial DRAM state (line 0 = array[0..3]):\n";
    dram.dump(0, 0);
    std::cout << "\n";

    // -------------------------------------------------------------------------
    // Run loop — supports two modes:
    //   step : advance one cycle at a time, press Enter to continue
    //   run  : run to completion with no pausing
    // -------------------------------------------------------------------------
    std::cout << "Commands:  [Enter] = step one cycle   |   r = run to completion   |   q = quit\n\n";

    bool running = true;
    while (running && !pipeline.isDone()) {
        std::cout << "Press Enter / r / q: ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "q" || input == "Q") {
            std::cout << "Quitting.\n";
            break;
        }

        if (input == "r" || input == "R") {
            // Run to completion
            std::cout << "\n=== Running to completion ===\n\n";
            printDivider();

            while (!pipeline.isDone()) {
                pipeline.tick();
                pipeline.dump();
            }

            printDivider();
            running = false;
        } else {
            // Step one cycle
            pipeline.tick();
            printFullState(pipeline, cache, dram);
        }
    }

    // -------------------------------------------------------------------------
    // Final state
    // -------------------------------------------------------------------------
    std::cout << "\n=== Simulation Complete ===\n";
    std::cout << "Total cycles: " << pipeline.getCycleCount() << "\n\n";

    std::cout << "Final DRAM state (array[0..3] should each be incremented by 1):\n";
    dram.dump(0, 0);

    std::cout << "\nFinal cache state:\n";
    cache.dump(0, CACHE_NUM_LINES - 1);

    return 0;
}