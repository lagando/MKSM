#include "../ps2_vu1_thread.h"
#include "runtime/ps2_perf.h"
#include "gs_async.h"
#include "runtime/gs/gs_frontend.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <vector>

namespace
{
    // Packets go through a byte ring with one writer at a time and one reader, so a submit is
    // a copy and one atomic store. (A mutex, a deque and a notify per packet cost ~7% of the
    // game thread in gameplay: the frontend slept between packets and every submit woke it.)
    // A full ring makes the EE wait, so a slow GS cannot grow the queue forever.
    constexpr uint64_t kRingBytes = 32u << 20; // a multiple of 16
    constexpr uint64_t kSkipToStart = ~0ull;   // header size: the next packet is at the ring start
    constexpr auto kSpinTime = std::chrono::microseconds(100);

    struct Header
    {
        uint64_t size;
        GS *gs;
    };

    struct Frontend
    {
        std::mutex submitMutex; // one writer at a time
        std::vector<uint8_t> ring;
        alignas(64) std::atomic<uint64_t> head{0u}; // bytes decoded (never wraps; position = % kRingBytes)
        alignas(64) std::atomic<uint64_t> tail{0u}; // bytes submitted
        std::mutex sleepMutex;
        std::condition_variable work, progress;
        std::atomic<bool> sleeping{false};     // the frontend waits on `work`
        std::atomic<uint32_t> drainers{0u};    // threads waiting on `progress`
        std::atomic<uint64_t> vblankMark{0u};  // tail at the last vblank
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

    // Spins until ready() or for kSpinTime; the caller sleeps on a condition variable after that.
    template <typename Ready>
    bool spinUntil(Ready ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + kSpinTime;
        for (;;)
        {
            for (int i = 0; i < 64; ++i)
            {
                if (ready())
                    return true;
                _mm_pause();
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return ready();
        }
    }

    void waitForHead(uint64_t wanted) // until head >= wanted
    {
        const auto ready = [wanted] { return g_frontend.head.load() >= wanted; };
        if (spinUntil(ready))
            return;
        std::unique_lock<std::mutex> lock(g_frontend.sleepMutex);
        g_frontend.drainers.fetch_add(1u);
        g_frontend.progress.wait(lock, ready);
        g_frontend.drainers.fetch_sub(1u);
    }

    void runFrontend()
    {
        pthread_setname_np(pthread_self(), "GSFrontend");
        ps2_perf::pinHeavyThread(ps2_perf::HeavyThread::GsFrontend);
        uint64_t head = 0u;
        const auto hasWork = [&head] { return g_frontend.tail.load() != head; };
        for (;;)
        {
            if (!hasWork() && !spinUntil(hasWork))
            {
                std::unique_lock<std::mutex> lock(g_frontend.sleepMutex);
                g_frontend.sleeping.store(true);
                g_frontend.work.wait(lock, hasWork);
                g_frontend.sleeping.store(false);
            }
            Header header;
            std::memcpy(&header, &g_frontend.ring[head % kRingBytes], sizeof(header));
            if (header.size == kSkipToStart)
                head += kRingBytes - head % kRingBytes;
            else
            {
                header.gs->processGIFPacket(&g_frontend.ring[head % kRingBytes + sizeof(header)], static_cast<uint32_t>(header.size));
                head += sizeof(header) + ((header.size + 15u) & ~15ull);
            }
            g_frontend.head.store(head);
            if (g_frontend.drainers.load() != 0u)
            {
                std::lock_guard<std::mutex> lock(g_frontend.sleepMutex);
                g_frontend.progress.notify_all();
            }
        }
    }
}

void gsAsyncSubmit(GS &gs, const uint8_t *data, uint32_t sizeBytes)
{
    const uint64_t needed = sizeof(Header) + ((static_cast<uint64_t>(sizeBytes) + 15u) & ~15ull);
    if (!asyncEnabled() || needed > kRingBytes / 4u)
    {
        gsAsyncDrain(); // a packet too big for the ring is decoded here, in order
        gs.processGIFPacket(data, sizeBytes);
        return;
    }
    std::lock_guard<std::mutex> submit(g_frontend.submitMutex);
    if (g_frontend.ring.empty())
    {
        g_frontend.ring.resize(kRingBytes);
        std::thread(runFrontend).detach();
    }
    uint64_t tail = g_frontend.tail.load(std::memory_order_relaxed);
    const uint64_t toEnd = kRingBytes - tail % kRingBytes;
    const uint64_t skipped = toEnd < needed ? toEnd : 0u; // packets stay in one piece: skip the rest of the ring
    if (tail + skipped + needed - g_frontend.head.load() > kRingBytes)
        waitForHead(tail + skipped + needed - kRingBytes);
    if (skipped != 0u)
    {
        const Header skip{kSkipToStart, nullptr};
        std::memcpy(&g_frontend.ring[tail % kRingBytes], &skip, sizeof(skip));
        tail += skipped;
    }
    const Header header{sizeBytes, &gs};
    std::memcpy(&g_frontend.ring[tail % kRingBytes], &header, sizeof(header));
    std::memcpy(&g_frontend.ring[tail % kRingBytes + sizeof(header)], data, sizeBytes);
    g_frontend.tail.store(tail + needed);
    if (g_frontend.sleeping.load())
    {
        std::lock_guard<std::mutex> lock(g_frontend.sleepMutex);
        g_frontend.work.notify_one();
    }
}

void gsAsyncDrain()
{
    // Readers of GS state on the EE thread also wait for PATH1/PATH2 the VU1 thread still has.
    if (!ps2_vu1_thread::onWorker())
        ps2_vu1_thread::sync();
    // Polling loops read CSR all the time: nothing queued costs two atomic loads.
    const uint64_t tail = g_frontend.tail.load();
    if (g_frontend.head.load() != tail)
        waitForHead(tail);
}

bool gsAsyncCsrLag()
{
    static const bool on = []
    {
        const char *setting = std::getenv("PS2X_GS_CSR_LAG");
        return !(setting && setting[0] == '0');
    }();
    return on && asyncEnabled();
}

void gsAsyncNoteVblank()
{
    g_frontend.vblankMark.store(g_frontend.tail.load());
}

void gsAsyncWaitLastVblank()
{
    const uint64_t mark = g_frontend.vblankMark.load();
    if (g_frontend.head.load() < mark)
        waitForHead(mark);
}
