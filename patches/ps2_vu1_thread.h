#pragma once
// VU1 on its own thread (like PCSX2's MTVU). The EE thread hands each VIF1 DMA transfer
// (copied, since the game may reuse the buffer once the channel reports done) to the VU1
// thread, which runs the whole VIF1 stream there: unpacks into VU1 memory, MPG, MSCAL
// (the VU1 program to its end) and DIRECT. GIF packets of PATH1 and PATH2 therefore leave
// in VIF1 stream order. The EE thread waits for the queue to drain (sync) before it looks
// at anything that thread owns: VU1 memory, VIF1 registers and FIFO, GS state, its own GIF
// submissions. It always waits for everything, so the game always sees the work done, as
// when VU1 ran inline: the result does not depend on host timing (online lockstep).
// PS2X_VU1_THREAD=0 runs VU1 inline as before.
#include <cstdint>
#include <functional>

namespace ps2_vu1_thread
{
    struct Hooks
    {
        std::function<void(const uint8_t *data, uint32_t size)> processVif1; // runs on the VU1 thread
        std::function<uint32_t()> readFbrst;                                 // EE thread: VU0 FBRST now
        std::function<void(uint32_t stopBits)> applyStopBits;                // EE thread: VPU_STAT D/T bits
    };

    bool enabled();
    void start(Hooks hooks);
    void stop();
    bool onWorker();
    // EE thread: queue one VIF1 transfer (copied).
    void submit(const uint8_t *data, uint32_t size);
    // EE thread: wait until every queued transfer ran. No-op on the VU1 thread or when off.
    void sync();
    // VU1 thread: FBRST as the EE had it when the running transfer was queued.
    uint32_t jobFbrst();
    // VU1 thread: D/T stop bits of the last program; the EE gets them at its next sync.
    void noteStopBits(uint32_t bits);
}
