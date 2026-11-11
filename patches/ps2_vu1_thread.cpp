#include "ps2_vu1_thread.h"
#include "runtime/ps2_perf.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <vector>

namespace ps2_vu1_thread
{
    namespace
    {
        struct Job
        {
            std::vector<uint8_t> data;
            uint32_t fbrst = 0u;
        };

        Hooks g_hooks;
        std::thread g_thread;
        std::mutex g_mutex;
        std::condition_variable g_work, g_idle;
        std::deque<Job> g_jobs;
        std::vector<std::vector<uint8_t>> g_spare; // reused job buffers
        bool g_busy = false, g_stop = false, g_running = false;
        std::atomic<bool> g_pending{false}; // queue not empty or a job running: sync's fast path
        std::atomic<uint32_t> g_stopBits{0u};
        std::atomic<bool> g_stopBitsNew{false};
        thread_local bool t_worker = false;
        uint32_t g_jobFbrst = 0u; // VU1 thread only

        void loop()
        {
            pthread_setname_np(pthread_self(), "VU1Thread"); // PS2X_PROFILE_THREAD picks threads by name
            ps2_perf::pinHeavyThread(ps2_perf::HeavyThread::Vu1);
            t_worker = true;
            std::unique_lock<std::mutex> lock(g_mutex);
            while (true)
            {
                g_work.wait(lock, [] { return g_stop || !g_jobs.empty(); });
                if (g_jobs.empty())
                    break;
                Job job = std::move(g_jobs.front());
                g_jobs.pop_front();
                g_busy = true;
                lock.unlock();
                g_jobFbrst = job.fbrst;
                g_hooks.processVif1(job.data.data(), static_cast<uint32_t>(job.data.size()));
                lock.lock();
                g_busy = false;
                job.data.clear();
                if (g_spare.size() < 8u)
                    g_spare.push_back(std::move(job.data));
                if (g_jobs.empty())
                {
                    g_pending.store(false, std::memory_order_release);
                    g_idle.notify_all();
                }
            }
        }
    }

    bool enabled()
    {
        static const bool on = []
        {
            const char *setting = std::getenv("PS2X_VU1_THREAD");
            return !(setting && setting[0] == '0');
        }();
        return on && g_running;
    }

    void start(Hooks hooks)
    {
        if (g_running)
            return;
        const char *setting = std::getenv("PS2X_VU1_THREAD");
        if (setting && setting[0] == '0')
            return;
        g_hooks = std::move(hooks);
        g_stop = false;
        g_running = true;
        g_thread = std::thread(loop);
    }

    void stop()
    {
        if (!g_running)
            return;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_stop = true;
        }
        g_work.notify_all();
        g_thread.join();
        g_running = false;
    }

    bool onWorker()
    {
        return t_worker;
    }

    void submit(const uint8_t *data, uint32_t size)
    {
        if (size == 0u)
            return;
        Job job;
        job.fbrst = g_hooks.readFbrst ? g_hooks.readFbrst() : 0u;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_spare.empty())
            {
                job.data = std::move(g_spare.back());
                g_spare.pop_back();
            }
        }
        job.data.assign(data, data + size);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_jobs.push_back(std::move(job));
            g_pending.store(true, std::memory_order_release);
        }
        g_work.notify_one();
    }

    void sync()
    {
        if (!g_running || t_worker)
            return;
        if (g_pending.load(std::memory_order_acquire))
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_idle.wait(lock, [] { return g_jobs.empty() && !g_busy; });
        }
        if (g_stopBitsNew.exchange(false, std::memory_order_acq_rel) && g_hooks.applyStopBits)
            g_hooks.applyStopBits(g_stopBits.load(std::memory_order_relaxed));
    }

    uint32_t jobFbrst()
    {
        return g_jobFbrst;
    }

    void noteStopBits(uint32_t bits)
    {
        g_stopBits.store(bits, std::memory_order_relaxed);
        g_stopBitsNew.store(true, std::memory_order_release);
    }
}
