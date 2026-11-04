// Plays what the IOP sends to the SPU2 by AutoDMA (CRI ADX music and voices) on the host.
// The SPU2 itself is not emulated (voices, SPU RAM, reverb): see notas/audio.md.
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

namespace
{
    constexpr uint32_t kRingFrames = 24000u; // 0.5 s per core

    struct Ring
    {
        std::array<int16_t, kRingFrames * 2u> samples{};
        uint32_t read = 0u, size = 0u;
    };

    std::mutex g_mutex;
    Ring g_rings[2];
    AudioStream g_stream{};
    std::FILE *g_dump = nullptr;

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

    // Audio thread: mixes both cores; a core with nothing queued adds silence.
    void fillStream(void *buffer, unsigned int frames)
    {
        auto *out = static_cast<int16_t *>(buffer);
        std::lock_guard<std::mutex> lock(g_mutex);
        for (unsigned int frame = 0u; frame < frames; ++frame)
        {
            int32_t left = 0, right = 0;
            for (Ring &ring : g_rings)
            {
                if (ring.size == 0u)
                    continue;
                left += ring.samples[ring.read * 2u];
                right += ring.samples[ring.read * 2u + 1u];
                ring.read = (ring.read + 1u) % kRingFrames;
                --ring.size;
            }
            out[frame * 2u] = static_cast<int16_t>(std::clamp(left, -32768, 32767));
            out[frame * 2u + 1u] = static_cast<int16_t>(std::clamp(right, -32768, 32767));
        }
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
