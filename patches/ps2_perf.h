#pragma once

#include <atomic>
#include <cstdint>

// PS2X_PERF=1 prints, every 2 s, how fast each part runs: window frames, game frames,
// vsyncs, how busy the GS was and how long the game waited for it. Counters are
// cheap atomics; the timed ones are only measured when the meter is on.
namespace ps2_perf
{
    struct Counters
    {
        std::atomic<uint64_t> windowFrames{0};
        std::atomic<uint64_t> newImages{0}; // window frames that show a newly completed game frame
        std::atomic<uint64_t> gameFrames{0};
        std::atomic<uint64_t> vsyncs{0};
        std::atomic<uint64_t> gsBusyNs{0};
        std::atomic<uint64_t> eeWaitingGsNs{0};
    };

    Counters &counters();
    bool enabled();
    uint64_t nowNs();
    // Starts the reporter thread once, if PS2X_PERF is set.
    void startReporter();
}
