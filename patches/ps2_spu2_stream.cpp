// Plays what the IOP sends to the SPU2 by AutoDMA (CRI ADX music and voices) on the host,
// mixed with the SPU2 voices (sound effects, ps2xIOP spu2_voices.cpp). See notas/audio.md.
// PS2X_AUDIO=0 turns it off; PS2X_SPU2_DUMP=<file> also writes the mixed stream as raw
// 16-bit stereo 48 kHz PCM.
#include "ps2x/iop/spu2_stream.h"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
    constexpr uint32_t kRingFrames = 96000u;  // 2 s per core
    constexpr uint32_t kTargetFrames = 19200u; // 400 ms queued: absorbs the emulator's stalls
    // Time stretch (WSOLA): grains of kGrain frames, 50% overlap, each placed within
    // +-kSearch of its nominal position where it best continues the previous grain.
    constexpr uint32_t kGrain = 1024u, kHop = kGrain / 2u, kSearch = 128u;

    struct Ring
    {
        std::array<int16_t, kRingFrames * 2u> samples{};
        uint64_t start = 0u; // absolute index of the oldest frame kept
        uint64_t size = 0u;
        double nominal = kSearch; // absolute input position of the next grain
        uint64_t natural = 0u;    // where the previous grain would continue; 0 = none
        double tempo = 1.0;       // input frames per output frame
        double rate = 1.0;        // frames arriving per output frame, averaged over ~1 s
        uint64_t arrived = 0u;
        bool primed = false;      // false until refilled after running dry
        std::array<float, kHop * 2u> hop{}, tail{};
        uint32_t hopLeft = 0u;

        int16_t at(uint64_t frame, int channel) const { return samples[(frame % kRingFrames) * 2u + channel]; }
    };

    std::mutex g_mutex;
    Ring g_rings[2];
    AudioStream g_stream{};
    std::FILE *g_dump = nullptr;
    std::vector<int32_t> g_mix; // audio thread only
    std::array<float, kGrain> g_window = []
    {
        std::array<float, kGrain> w{};
        for (uint32_t i = 0u; i < kGrain; ++i) // periodic Hann: overlapping halves sum to 1
            w[i] = 0.5f - 0.5f * std::cos(6.283185307f * static_cast<float>(i) / kGrain);
        return w;
    }();

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
                    ++ring.start;
                    --ring.size;
                }
                const uint64_t write = (ring.start + ring.size) % kRingFrames;
                std::memcpy(&ring.samples[write * 2u], block + kb + i * 2u, 2u);
                std::memcpy(&ring.samples[write * 2u + 1u], block + kb + 512u + i * 2u, 2u);
                ++ring.size;
                ++ring.arrived;
            }
        }
    }

    // Fills ring.hop with the next kHop output frames.
    void nextHop(Ring &ring)
    {
        ring.hopLeft = kHop;
        const uint64_t end = ring.start + ring.size;
        if (ring.nominal < ring.start + kSearch)
            ring.nominal = static_cast<double>(ring.start + kSearch);
        const uint64_t nominal = static_cast<uint64_t>(ring.nominal);
        if (!ring.primed && ring.size >= kTargetFrames / 2u)
            ring.primed = true;
        if (!ring.primed || nominal + kSearch + kGrain > end)
        {
            // Ran dry: fade out what was playing and wait for a refill.
            ring.primed = false;
            ring.hop = ring.tail;
            ring.tail.fill(0.0f);
            ring.natural = 0u;
            return;
        }

        uint64_t pos = nominal;
        if (ring.natural != 0u && ring.natural >= ring.start)
        {
            float best = -1e30f;
            for (uint64_t c = nominal - kSearch; c <= nominal + kSearch; c += 4u)
            {
                float dot = 0.0f, energy = 1.0f;
                for (uint32_t k = 0u; k < kHop; k += 2u)
                {
                    const float a = static_cast<float>(ring.at(ring.natural + k, 0) + ring.at(ring.natural + k, 1));
                    const float b = static_cast<float>(ring.at(c + k, 0) + ring.at(c + k, 1));
                    dot += a * b;
                    energy += b * b;
                }
                const float score = dot / std::sqrt(energy);
                if (score > best)
                {
                    best = score;
                    pos = c;
                }
            }
        }
        for (uint32_t k = 0u; k < kHop; ++k)
            for (int ch = 0; ch < 2; ++ch)
            {
                ring.hop[k * 2u + ch] = ring.tail[k * 2u + ch] + ring.at(pos + k, ch) * g_window[k];
                ring.tail[k * 2u + ch] = ring.at(pos + kHop + k, ch) * g_window[kHop + k];
            }
        ring.natural = pos + kHop;
        ring.nominal += kHop * ring.tempo;
        const uint64_t keep = std::min<uint64_t>(ring.natural, static_cast<uint64_t>(ring.nominal) - kSearch);
        if (keep > ring.start)
        {
            ring.size -= keep - ring.start;
            ring.start = keep;
        }
    }

    // Audio thread: mixes both cores. Below 60 fields/s the guest makes sound slower than
    // real time, and it stalls for a few hundred ms at a time. The queue absorbs the stalls;
    // a longer slowdown plays the music at a slower tempo (same pitch) instead of gaps, and
    // a backlog plays slightly faster.
    void fillStream(void *buffer, unsigned int frames)
    {
        auto *out = static_cast<int16_t *>(buffer);
        g_mix.assign(frames * 2u, 0);
        std::lock_guard<std::mutex> lock(g_mutex);
        for (Ring &ring : g_rings)
        {
            const double fill = static_cast<double>(ring.size) / kTargetFrames;
            ring.rate += (std::min(static_cast<double>(ring.arrived) / frames, 4.0) - ring.rate) * 0.01;
            ring.arrived = 0u;
            // Follow the arrival rate, and steer the queue back toward the target.
            const double wanted = std::clamp(ring.rate * (0.5 + 0.5 * std::min(fill, 3.0)), 0.3, 1.25);
            ring.tempo += (wanted - ring.tempo) * 0.05;
            for (unsigned int frame = 0u; frame < frames; ++frame)
            {
                if (ring.hopLeft == 0u)
                    nextHop(ring);
                const uint32_t k = kHop - ring.hopLeft--;
                g_mix[frame * 2u] += static_cast<int32_t>(ring.hop[k * 2u]);
                g_mix[frame * 2u + 1u] += static_cast<int32_t>(ring.hop[k * 2u + 1u]);
            }
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
