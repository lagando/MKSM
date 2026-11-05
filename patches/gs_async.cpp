#include "gs_async.h"
#include "runtime/gs/gs_frontend.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <vector>

namespace
{
    // Past this much queued data the EE waits, so a slow GS cannot grow the queue forever.
    constexpr size_t kMaxQueuedBytes = 32u << 20;

    struct Frontend
    {
        std::mutex mutex;
        std::condition_variable work, progress;
        std::deque<std::vector<uint8_t>> pending;
        std::vector<std::vector<uint8_t>> spare; // decoded packets, reused for new ones
        size_t queuedBytes = 0u;
        std::atomic<uint64_t> outstanding{0u}; // submitted and not yet decoded
        GS *gs = nullptr;
        bool started = false;
    };

    Frontend g_frontend;

    bool asyncEnabled()
    {
        static const bool enabled = []
        {
            const char *setting = std::getenv("PS2X_GS_ASYNC");
            return !(setting && setting[0] == '0');
        }();
        return enabled;
    }

    void runFrontend()
    {
        pthread_setname_np(pthread_self(), "GSFrontend");
        std::unique_lock<std::mutex> lock(g_frontend.mutex);
        for (;;)
        {
            g_frontend.work.wait(lock, [] { return !g_frontend.pending.empty(); });
            std::vector<uint8_t> packet = std::move(g_frontend.pending.front());
            g_frontend.pending.pop_front();
            lock.unlock();

            g_frontend.gs->processGIFPacket(packet.data(), static_cast<uint32_t>(packet.size()));

            lock.lock();
            g_frontend.queuedBytes -= packet.size();
            g_frontend.spare.push_back(std::move(packet));
            g_frontend.outstanding.fetch_sub(1u, std::memory_order_release);
            g_frontend.progress.notify_all();
        }
    }
}

void gsAsyncSubmit(GS &gs, const uint8_t *data, uint32_t sizeBytes)
{
    if (!asyncEnabled())
    {
        gs.processGIFPacket(data, sizeBytes);
        return;
    }
    std::unique_lock<std::mutex> lock(g_frontend.mutex);
    if (!g_frontend.started)
    {
        g_frontend.gs = &gs;
        g_frontend.started = true;
        std::thread(runFrontend).detach();
    }
    if (g_frontend.gs != &gs) // another runtime (tests): finish the old GS's packets first
    {
        g_frontend.progress.wait(lock, [] { return g_frontend.outstanding.load(std::memory_order_acquire) == 0u; });
        g_frontend.gs = &gs;
    }
    g_frontend.progress.wait(lock, [] { return g_frontend.queuedBytes < kMaxQueuedBytes; });
    std::vector<uint8_t> packet;
    if (!g_frontend.spare.empty())
    {
        packet = std::move(g_frontend.spare.back());
        g_frontend.spare.pop_back();
    }
    lock.unlock(); // copy without holding up the GS thread
    packet.assign(data, data + sizeBytes);
    lock.lock();
    g_frontend.queuedBytes += packet.size();
    g_frontend.outstanding.fetch_add(1u, std::memory_order_relaxed);
    g_frontend.pending.push_back(std::move(packet));
    g_frontend.work.notify_one();
}

void gsAsyncDrain()
{
    // Polling loops read CSR all the time: nothing queued costs one atomic load.
    if (g_frontend.outstanding.load(std::memory_order_acquire) == 0u)
        return;
    std::unique_lock<std::mutex> lock(g_frontend.mutex);
    g_frontend.progress.wait(lock, [] { return g_frontend.outstanding.load(std::memory_order_acquire) == 0u; });
}
