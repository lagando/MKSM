// Self-time sampling profiler for hosts without perf. PS2X_PROFILE=<seconds>
// waits that long, samples every 1 ms of CPU time for 20 s and writes the raw
// instruction pointers to ps2_profile.txt (resolved with nm by a script).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>
#include <thread>
#include <ucontext.h>
#include <vector>

namespace
{
    constexpr size_t kMaxSamples = 200000u;
    uintptr_t g_samples[kMaxSamples];
    std::atomic<size_t> g_count{0u};

    void onProfileSignal(int, siginfo_t *, void *context)
    {
        const size_t index = g_count.fetch_add(1u, std::memory_order_relaxed);
        if (index < kMaxSamples)
        {
            g_samples[index] = static_cast<uintptr_t>(static_cast<ucontext_t *>(context)->uc_mcontext.gregs[REG_RIP]);
        }
    }

    struct ProfilerStarter
    {
        ProfilerStarter()
        {
            const char *delay = std::getenv("PS2X_PROFILE");
            if (delay == nullptr)
            {
                return;
            }
            const int seconds = std::atoi(delay);
            std::thread([seconds]
                        {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                struct sigaction action{};
                action.sa_sigaction = onProfileSignal;
                action.sa_flags = SA_SIGINFO | SA_RESTART;
                sigaction(SIGPROF, &action, nullptr);
                itimerval timer{{0, 1000}, {0, 1000}};
                setitimer(ITIMER_PROF, &timer, nullptr);
                std::this_thread::sleep_for(std::chrono::seconds(20));
                itimerval stop{};
                setitimer(ITIMER_PROF, &stop, nullptr);
                std::FILE *out = std::fopen("ps2_profile.txt", "w");
                if (out == nullptr)
                {
                    return;
                }
                std::FILE *maps = std::fopen("/proc/self/maps", "r");
                char line[512];
                while (maps != nullptr && std::fgets(line, sizeof(line), maps) != nullptr)
                {
                    std::fprintf(out, "base %s", line);

                }
                const size_t total = std::min(g_count.load(), kMaxSamples);
                for (size_t i = 0u; i < total; ++i)
                {
                    std::fprintf(out, "%zx\n", g_samples[i]);
                }
                std::fclose(out); })
                .detach();
        }
    } g_profilerStarter;
}
