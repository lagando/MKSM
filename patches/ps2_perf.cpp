#include "runtime/ps2_perf.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace ps2_perf
{
    Counters &counters()
    {
        static Counters instance;
        return instance;
    }

    bool enabled()
    {
        static const bool on = std::getenv("PS2X_PERF") != nullptr;
        return on;
    }

    uint64_t nowNs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
    }

    void startReporter()
    {
        static bool started = false;
        if (!enabled() || started)
            return;
        started = true;
        std::thread([]
                    {
            Counters &c = counters();
            uint64_t last = nowNs();
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                const uint64_t now = nowNs();
                const double seconds = static_cast<double>(now - last) / 1e9;
                last = now;
                std::fprintf(stderr,
                             "[perf] janela %.1f fps (imagens novas %.1f/s) | jogo %.1f quadros/s | vsync %.1f/s | GS ocupado %.0f%% | jogo esperando o GS %.0f%%\n",
                             c.windowFrames.exchange(0) / seconds, c.newImages.exchange(0) / seconds, c.gameFrames.exchange(0) / seconds,
                             c.vsyncs.exchange(0) / seconds, c.gsBusyNs.exchange(0) / (seconds * 1e7),
                             c.eeWaitingGsNs.exchange(0) / (seconds * 1e7));
            } })
            .detach();
    }
}
