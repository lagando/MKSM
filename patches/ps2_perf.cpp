#include "runtime/ps2_perf.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

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

    void pinHeavyThread(HeavyThread role)
    {
#if defined(__linux__)
        static const std::vector<int> cores = []
        {
            std::vector<int> firstCpus; // first logical CPU of each physical core, in order
            const char *setting = std::getenv("PS2X_PIN");
            if (setting && setting[0] == '0')
                return firstCpus;
            std::set<std::string> seen;
            cpu_set_t allowed;
            if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0)
                return firstCpus;
            for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            {
                if (!CPU_ISSET(cpu, &allowed))
                    continue;
                std::ifstream file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list");
                std::string siblings;
                if (!(file >> siblings))
                    return std::vector<int>{};
                if (seen.insert(siblings).second)
                    firstCpus.push_back(cpu);
            }
            if (firstCpus.size() < 4u)
                firstCpus.clear();
            return firstCpus;
        }();
        if (cores.empty())
            return;
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cores[static_cast<unsigned>(role) % cores.size()], &set);
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
        (void)role;
#endif
    }
}
