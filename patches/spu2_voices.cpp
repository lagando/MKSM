// SPU2 voices: sound RAM, the 24 ADPCM voices of each core and their mix. Sound effects
// (SDRDRV) play through these; music and speech come by AutoDMA (spu2_stream.h).
// Pitch uses the hardware's 4-point gaussian interpolation.
// Not emulated: reverb, noise, pitch modulation, SPU IRQs, volume sweeps.
#include "spu2_voices.h"
#include "ps2x/iop/spu2_stream.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kRamWords = 1024u * 1024u; // 2 MB of 16-bit words
        constexpr uint32_t kRamMask = kRamWords - 1u;
        constexpr uint32_t kVoices = 24u;

        // 4-point gaussian interpolation table of the PS1/PS2 SPU (psx-spx, "SPU Interpolation").
        constexpr int16_t kGauss[512] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3,
            3, 4, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 21, 22, 24, 25, 27, 28, 30, 32, 33, 35, 37,
            39, 41, 44, 46, 48, 51, 53, 56, 58, 61, 64, 67, 70, 73, 77, 80,
            84, 87, 91, 95, 99, 103, 107, 111, 116, 120, 125, 130, 135, 140, 145, 150,
            156, 161, 167, 173, 179, 186, 192, 199, 205, 212, 219, 227, 234, 242, 250, 257,
            266, 274, 283, 291, 300, 309, 319, 328, 338, 348, 358, 369, 379, 390, 401, 412,
            424, 436, 448, 460, 473, 485, 498, 512, 525, 539, 553, 567, 582, 597, 612, 627,
            643, 659, 675, 692, 708, 726, 743, 761, 779, 797, 816, 835, 854, 874, 894, 914,
            935, 956, 977, 999, 1020, 1043, 1066, 1089, 1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
            1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508, 1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
            1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031, 2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
            2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668, 2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
            3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427, 3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
            3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315, 4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
            4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332, 5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
            5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475, 6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
            7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735, 7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
            8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097, 9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
            9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542, 10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
            11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045, 12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
            12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577, 13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
            14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104, 15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
            15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592, 16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
            17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001, 18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
            18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295, 19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
            19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439, 20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
            21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399, 21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
            21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148, 22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
            22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662, 22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
            22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927, 22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
        };

        enum class Phase : uint8_t
        {
            Off,
            Attack,
            Decay,
            Sustain,
            Release
        };

        struct Voice
        {
            uint16_t volL = 0, volR = 0, pitch = 0, adsr1 = 0, adsr2 = 0;
            uint32_t ssa = 0, lsa = 0, nax = 0; // word addresses
            Phase phase = Phase::Off;
            int32_t envelope = 0;
            uint32_t envelopeWait = 0;
            uint32_t counter = 0; // 12-bit fraction of the decoded-sample position
            std::array<int16_t, 28> decoded{};
            uint32_t decodedIndex = 28; // 28: decode the block at nax next
            uint32_t blockAddress = 0;
            uint8_t blockFlags = 0;
            int32_t history[2]{};
            int16_t recent[4]{}; // the last four decoded samples, oldest first
        };

        struct Core
        {
            std::array<Voice, kVoices> voices{};
            uint32_t tsa = 0;
            uint32_t endx = 0;
            uint32_t vmixl = 0, vmixr = 0;
            uint16_t mvolL = 0x3FFF, mvolR = 0x3FFF;
        };

        std::mutex g_mutex;
        std::vector<uint16_t> g_ram(kRamWords);
        Core g_cores[2];

        int32_t fixedVolume(uint16_t value)
        {
            // Bit 15 selects a sweep; sweeps are not emulated, so they play at half scale.
            if ((value & 0x8000u) != 0u)
                return 0x3FFF;
            return static_cast<int16_t>(value << 1);
        }

        // One envelope step, as on the PS1/PS2 SPU (rate = shift << 2 | step).
        void stepEnvelope(Voice &voice, uint32_t rate, bool exponential, bool decrease)
        {
            if (voice.envelopeWait > 0u)
            {
                --voice.envelopeWait;
                return;
            }
            const int32_t shift = static_cast<int32_t>(rate >> 2);
            const int32_t stepBase = decrease ? -8 + static_cast<int32_t>(rate & 3u) : 7 - static_cast<int32_t>(rate & 3u);
            int32_t step = stepBase << std::max(0, 11 - shift);
            uint32_t cycles = 1u << std::max(0, shift - 11);
            if (exponential && !decrease && voice.envelope > 0x6000)
                cycles *= 4u;
            if (exponential && decrease)
                step = step * voice.envelope / 0x8000;
            voice.envelopeWait = cycles - 1u;
            voice.envelope = std::clamp(voice.envelope + step, 0, 0x7FFF);
        }

        void advanceEnvelope(Voice &voice)
        {
            switch (voice.phase)
            {
            case Phase::Attack:
                stepEnvelope(voice, (voice.adsr1 >> 8) & 0x7Fu, (voice.adsr1 & 0x8000u) != 0u, false);
                if (voice.envelope >= 0x7FFF)
                {
                    voice.phase = Phase::Decay;
                    voice.envelopeWait = 0u;
                }
                break;
            case Phase::Decay:
                stepEnvelope(voice, ((voice.adsr1 >> 4) & 0xFu) << 2, true, true);
                if (voice.envelope <= static_cast<int32_t>(((voice.adsr1 & 0xFu) + 1u) * 0x800u))
                {
                    voice.phase = Phase::Sustain;
                    voice.envelopeWait = 0u;
                }
                break;
            case Phase::Sustain:
                stepEnvelope(voice, (voice.adsr2 >> 6) & 0x7Fu, (voice.adsr2 & 0x8000u) != 0u, (voice.adsr2 & 0x4000u) != 0u);
                break;
            case Phase::Release:
                stepEnvelope(voice, (voice.adsr2 & 0x1Fu) << 2, (voice.adsr2 & 0x20u) != 0u, true);
                if (voice.envelope == 0)
                    voice.phase = Phase::Off;
                break;
            case Phase::Off:
                break;
            }
        }

        // Decodes the 16-byte ADPCM block at nax and moves nax past it.
        void decodeBlock(Voice &voice)
        {
            static constexpr int32_t kFilters[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
            voice.blockAddress = voice.nax & kRamMask;
            const uint16_t header = g_ram[voice.blockAddress];
            const uint32_t shift = header & 0xFu;
            const uint32_t filter = std::min<uint32_t>((header >> 4) & 7u, 4u);
            voice.blockFlags = static_cast<uint8_t>(header >> 8);
            if ((voice.blockFlags & 4u) != 0u) // loop start
                voice.lsa = voice.blockAddress;
            for (uint32_t i = 0u; i < 28u; ++i)
            {
                const uint16_t word = g_ram[(voice.blockAddress + 1u + i / 4u) & kRamMask];
                const int32_t nibble = static_cast<int16_t>(static_cast<uint16_t>(((word >> ((i & 3u) * 4u)) & 0xFu) << 12)) >> shift;
                const int32_t sample = nibble + (voice.history[0] * kFilters[filter][0] + voice.history[1] * kFilters[filter][1]) / 64;
                const int32_t clamped = std::clamp(sample, -32768, 32767);
                voice.history[1] = voice.history[0];
                voice.history[0] = clamped;
                voice.decoded[i] = static_cast<int16_t>(clamped);
            }
            voice.decodedIndex = 0u;
            voice.nax = (voice.blockAddress + 8u) & kRamMask;
        }

        // Next decoded sample; handles the block flags when a block runs out.
        int16_t nextSample(Core &core, uint32_t index, Voice &voice)
        {
            if (voice.decodedIndex >= 28u)
            {
                if ((voice.blockFlags & 1u) != 0u) // the finished block ends a loop
                {
                    core.endx |= 1u << index;
                    if ((voice.blockFlags & 2u) != 0u)
                        voice.nax = voice.lsa;
                    else
                    {
                        voice.phase = Phase::Off;
                        voice.envelope = 0;
                    }
                }
                decodeBlock(voice);
            }
            return voice.decoded[voice.decodedIndex++];
        }

        void keyOn(Core &core, uint32_t index)
        {
            Voice &voice = core.voices[index];
            voice.nax = voice.ssa;
            voice.lsa = voice.ssa;
            voice.phase = Phase::Attack;
            voice.envelope = 0;
            voice.envelopeWait = 0u;
            voice.counter = 0u;
            voice.decodedIndex = 28u;
            voice.blockFlags = 0u;
            voice.history[0] = voice.history[1] = 0;
            std::fill(std::begin(voice.recent), std::end(voice.recent), int16_t{0});
            core.endx &= ~(1u << index);
        }

        // Decodes a register offset into (core, register within the core).
        bool decode(uint32_t phys, uint32_t &coreIndex, uint32_t &reg)
        {
            const uint32_t offset = phys - 0x1F900000u;
            if (offset < 0x760u)
            {
                coreIndex = offset >= 0x400u ? 1u : 0u;
                reg = offset - coreIndex * 0x400u;
                return true;
            }
            if (offset < 0x760u + 2u * 0x28u)
            {
                coreIndex = (offset - 0x760u) / 0x28u;
                reg = 0x760u + (offset - 0x760u) % 0x28u;
                return true;
            }
            return false;
        }

        void setHigh(uint32_t &address, uint16_t value) { address = ((value & 0xFu) << 16) | (address & 0xFFFFu); }
        void setLow(uint32_t &address, uint16_t value) { address = (address & 0xF0000u) | value; }
    }

    void spu2Write16(uint32_t phys, uint16_t value)
    {
        uint32_t coreIndex = 0u, reg = 0u;
        if (!decode(phys, coreIndex, reg))
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        Core &core = g_cores[coreIndex];
        if (reg < kVoices * 0x10u)
        {
            Voice &voice = core.voices[reg >> 4];
            switch (reg & 0xFu)
            {
            case 0x0: voice.volL = value; break;
            case 0x2: voice.volR = value; break;
            case 0x4: voice.pitch = value; break;
            case 0x6: voice.adsr1 = value; break;
            case 0x8: voice.adsr2 = value; break;
            case 0xA: voice.envelope = value & 0x7FFF; break;
            default: break;
            }
            return;
        }
        if (reg >= 0x1C0u && reg < 0x1C0u + kVoices * 0xCu)
        {
            Voice &voice = core.voices[(reg - 0x1C0u) / 0xCu];
            switch ((reg - 0x1C0u) % 0xCu)
            {
            case 0x0: setHigh(voice.ssa, value); break;
            case 0x2: setLow(voice.ssa, value); break;
            case 0x4: setHigh(voice.lsa, value); break;
            case 0x6: setLow(voice.lsa, value); break;
            case 0x8: setHigh(voice.nax, value); break;
            case 0xA: setLow(voice.nax, value); break;
            default: break;
            }
            return;
        }
        switch (reg)
        {
        case 0x188: core.vmixl = (core.vmixl & 0xFF0000u) | value; break;
        case 0x18A: core.vmixl = (core.vmixl & 0xFFFFu) | ((value & 0xFFu) << 16); break;
        case 0x190: core.vmixr = (core.vmixr & 0xFF0000u) | value; break;
        case 0x192: core.vmixr = (core.vmixr & 0xFFFFu) | ((value & 0xFFu) << 16); break;
        case 0x1A0:
        case 0x1A2:
        case 0x1A4:
        case 0x1A6:
        {
            const uint32_t bits = reg & 2u ? (value & 0xFFu) << 16 : value;
            for (uint32_t index = 0u; index < kVoices; ++index)
            {
                if ((bits & (1u << index)) == 0u)
                    continue;
                if (reg < 0x1A4u)
                    keyOn(core, index);
                else if (core.voices[index].phase != Phase::Off)
                {
                    core.voices[index].phase = Phase::Release;
                    core.voices[index].envelopeWait = 0u;
                }
            }
            break;
        }
        case 0x1A8: setHigh(core.tsa, value); break;
        case 0x1AA: setLow(core.tsa, value); break;
        case 0x1AC: // STD: manual transfer port
            g_ram[core.tsa & kRamMask] = value;
            core.tsa = (core.tsa + 1u) & kRamMask;
            break;
        case 0x340: core.endx &= 0xFF0000u; break; // writing ENDX clears it
        case 0x342: core.endx &= 0xFFFFu; break;
        case 0x760: core.mvolL = value; break;
        case 0x762: core.mvolR = value; break;
        default: break;
        }
    }

    bool spu2Read16(uint32_t phys, uint16_t &value)
    {
        uint32_t coreIndex = 0u, reg = 0u;
        if (!decode(phys, coreIndex, reg))
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        const Core &core = g_cores[coreIndex];
        if (reg < kVoices * 0x10u && (reg & 0xFu) == 0xAu)
        {
            value = static_cast<uint16_t>(core.voices[reg >> 4].envelope);
            return true;
        }
        if (reg >= 0x1C0u && reg < 0x1C0u + kVoices * 0xCu && ((reg - 0x1C0u) % 0xCu) >= 0x8u)
        {
            const uint32_t nax = core.voices[(reg - 0x1C0u) / 0xCu].nax;
            value = static_cast<uint16_t>(((reg - 0x1C0u) % 0xCu) == 0x8u ? nax >> 16 : nax & 0xFFFFu);
            return true;
        }
        if (reg == 0x340u || reg == 0x342u)
        {
            value = static_cast<uint16_t>(reg == 0x340u ? core.endx & 0xFFFFu : core.endx >> 16);
            return true;
        }
        return false;
    }

    void spu2DmaWrite(int coreIndex, const uint8_t *data, size_t bytes)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Core &core = g_cores[coreIndex & 1];
        for (size_t offset = 0u; offset + 1u < bytes; offset += 2u)
        {
            uint16_t word = 0u;
            std::memcpy(&word, data + offset, 2u);
            g_ram[core.tsa & kRamMask] = word;
            core.tsa = (core.tsa + 1u) & kRamMask;
        }
    }

    void spu2RenderVoices(int32_t *stereo, unsigned frames)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (uint32_t coreIndex = 0u; coreIndex < 2u; ++coreIndex)
        {
            Core &core = g_cores[coreIndex];
            const int32_t mvolL = fixedVolume(core.mvolL), mvolR = fixedVolume(core.mvolR);
            for (uint32_t index = 0u; index < kVoices; ++index)
            {
                Voice &voice = core.voices[index];
                if (voice.phase == Phase::Off)
                    continue;
                const bool left = (core.vmixl & (1u << index)) != 0u, right = (core.vmixr & (1u << index)) != 0u;
                const int32_t volL = left ? fixedVolume(voice.volL) : 0, volR = right ? fixedVolume(voice.volR) : 0;
                const uint32_t step = std::min<uint32_t>(voice.pitch, 0x3FFFu);
                for (unsigned frame = 0u; frame < frames && voice.phase != Phase::Off; ++frame)
                {
                    voice.counter += step;
                    while (voice.counter >= 0x1000u && voice.phase != Phase::Off)
                    {
                        voice.counter -= 0x1000u;
                        std::copy(voice.recent + 1, voice.recent + 4, voice.recent);
                        voice.recent[3] = nextSample(core, index, voice);
                    }
                    const uint32_t i = (voice.counter >> 4) & 0xFFu;
                    const int32_t sample = ((kGauss[0xFFu - i] * voice.recent[0]) >> 15) + ((kGauss[0x1FFu - i] * voice.recent[1]) >> 15) +
                                           ((kGauss[0x100u + i] * voice.recent[2]) >> 15) + ((kGauss[i] * voice.recent[3]) >> 15);
                    const int32_t shaped = sample * voice.envelope >> 15;
                    stereo[frame * 2u] += (shaped * volL >> 15) * mvolL >> 15;
                    stereo[frame * 2u + 1u] += (shaped * volR >> 15) * mvolR >> 15;
                    advanceEnvelope(voice);
                }
            }
        }
    }
}

namespace ps2x::iop
{
    void renderSpu2Voices(int32_t *stereo, unsigned frames)
    {
        detail::spu2RenderVoices(stereo, frames);
    }
}
