// SPU2 voices: sound RAM, the 24 ADPCM voices of each core and their mix. Sound effects
// (SDRDRV) play through these; music and speech come by AutoDMA (spu2_stream.h).
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
            int16_t previous = 0, current = 0; // samples around the play position
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
            voice.previous = voice.current = 0;
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
                        voice.previous = voice.current;
                        voice.current = nextSample(core, index, voice);
                    }
                    const int32_t t = static_cast<int32_t>(voice.counter);
                    const int32_t sample = (voice.previous * (0x1000 - t) + voice.current * t) >> 12;
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
