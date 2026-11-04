#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2x::iop::detail
{
    // IOP side: 16-bit register accesses in 0x1F900000-0x1F9007FF. spu2Read16 answers only
    // the registers the voices own (ENVX, NAX, ENDX); the rest keep the stored value.
    void spu2Write16(uint32_t phys, uint16_t value);
    [[nodiscard]] bool spu2Read16(uint32_t phys, uint16_t &value);
    // A non-AutoDMA transfer to sound RAM at the core's TSA.
    void spu2DmaWrite(int core, const uint8_t *data, size_t bytes);
}
