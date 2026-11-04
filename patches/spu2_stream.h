#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2x::iop
{
    // Receives each block the IOP sends to an SPU2 core by AutoDMA (ADMAS set): 16-bit
    // PCM at 48 kHz, per KB 256 left samples then 256 right. Called from the IOP's thread
    // when the DMA starts; the block plays for size / 4 frames.
    using Spu2StreamSink = void (*)(int core, const uint8_t *block, size_t size);
    void setSpu2StreamSink(Spu2StreamSink sink);
}
