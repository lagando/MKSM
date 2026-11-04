// Plays what the IOP sends to the SPU2 by AutoDMA (CRI ADX music and voices) on the host,
// mixed with the SPU2 voices (sound effects, ps2xIOP spu2_voices.cpp). See notas/audio.md.
// PS2X_AUDIO=0 turns it off; PS2X_SPU2_DUMP=<file> also writes the mixed stream as raw
// 16-bit stereo 48 kHz PCM.
#include "ps2x/iop/spu2_stream.h"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
    constexpr uint32_t kRingFrames = 24000u; // 0.5 s per core

    constexpr uint32_t kTargetFrames = 4800u; // 100 ms queued: play at full speed from here

    struct Ring
    {
        std::array<int16_t, kRingFrames * 2u> samples{};
        uint32_t read = 0u, size = 0u;
        double phase = 0.0; // fraction between samples[read] and the next frame
        double step = 1.0;  // frames consumed per output frame
    };

    std::mutex g_mutex;
    Ring g_rings[2];
    AudioStream g_stream{};
    std::FILE *g_dump = nullptr;
    std::vector<int32_t> g_mix; // audio thread only

    void onAutoDmaBlock(int core, const uint8_t *block, size_t size)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Ring &ring = g_rings[core & 1];
        // Each KB holds 256 left samples, then 256 right.
        for (size_t kb = 0u; kb + 1024u <= size; kb += 1024u)
        {
            for (uint32_t i = 0u; i < 256u; ++i)
            {
                if (ring.size == kRingFrames) // the host fell behind: drop the oldest frame
                {
                    ring.read = (ring.read + 1u) % kRingFrames;
                    --ring.size;
                }
                const uint32_t write = (ring.read + ring.size) % kRingFrames;
                std::memcpy(&ring.samples[write * 2u], block + kb + i * 2u, 2u);
                std::memcpy(&ring.samples[write * 2u + 1u], block + kb + 512u + i * 2u, 2u);
                ++ring.size;
            }
        }
    }

    // Audio thread: mixes both cores. Below 60 fields/s the guest makes sound slower than
    // real time; instead of gaps, a core then plays slower (lower pitch) as its queue
    // drains, and catches up slightly when it runs ahead.
    void fillStream(void *buffer, unsigned int frames)
    {
        auto *out = static_cast<int16_t *>(buffer);
        g_mix.assign(frames * 2u, 0);
        std::lock_guard<std::mutex> lock(g_mutex);
        for (Ring &ring : g_rings)
        {
            const double fill = static_cast<double>(ring.size) / kTargetFrames;
            const double wanted = std::clamp(fill < 1.0 ? 0.3 + 0.7 * fill : 1.0 + 0.02 * (fill - 1.0), 0.3, 1.1);
            ring.step += (wanted - ring.step) * 0.2;
        }
        for (unsigned int frame = 0u; frame < frames; ++frame)
        {
            int32_t left = 0, right = 0;
            for (Ring &ring : g_rings)
            {
                if (ring.size < 2u)
                    continue;
                const uint32_t next = (ring.read + 1u) % kRingFrames;
                const double t = ring.phase;
                left += static_cast<int32_t>(ring.samples[ring.read * 2u] * (1.0 - t) + ring.samples[next * 2u] * t);
                right += static_cast<int32_t>(ring.samples[ring.read * 2u + 1u] * (1.0 - t) + ring.samples[next * 2u + 1u] * t);
                ring.phase += ring.step;
                while (ring.phase >= 1.0 && ring.size >= 2u)
                {
                    ring.phase -= 1.0;
                    ring.read = (ring.read + 1u) % kRingFrames;
                    --ring.size;
                }
            }
            g_mix[frame * 2u] += left;
            g_mix[frame * 2u + 1u] += right;
        }
        ps2x::iop::renderSpu2Voices(g_mix.data(), frames);
        for (unsigned i = 0u; i < frames * 2u; ++i)
            out[i] = static_cast<int16_t>(std::clamp(g_mix[i], -32768, 32767));
        if (g_dump != nullptr)
            std::fwrite(out, 4u, frames, g_dump);
    }
}

void startSpu2Stream()
{
    const char *setting = std::getenv("PS2X_AUDIO");
    if ((setting != nullptr && setting[0] == '0') || !IsAudioDeviceReady())
        return;
    if (const char *dump = std::getenv("PS2X_SPU2_DUMP"))
        g_dump = std::fopen(dump, "wb");
    g_stream = LoadAudioStream(48000u, 16u, 2u);
    SetAudioStreamCallback(g_stream, fillStream);
    PlayAudioStream(g_stream);
    ps2x::iop::setSpu2StreamSink(onAutoDmaBlock);
}
