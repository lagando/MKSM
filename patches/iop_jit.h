#pragma once

#include "iop_cpu.h"
#include "iop_memory.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ps2x::iop::detail
{
    // x86-64 recompiler for straight runs of simple R3000 instructions (ALU, HI/LO reads,
    // loads and stores that hit RAM). Branches, jumps, COP0, MULT/DIV, overflow traps,
    // LWL/LWR/SWL/SWR and anything that touches hardware stay in the interpreter: a block
    // stops before them, and a load or store outside RAM or misaligned leaves the block
    // at that instruction with the state the interpreter would have there. The R3000 load
    // delay is resolved at compile time with the interpreter's own rule.
    //
    // The caller enters only where nothing can interrupt mid-block: no pending load, no
    // branch delay slot, no deliverable interrupt, no due DMA or guest callback. Then the
    // only things an instruction here changes are RAM and registers.
    //
    // PS2X_IOP_JIT=0 disables it.
    class IopJit
    {
    public:
        explicit IopJit(IopMemory &memory);
        ~IopJit();
        IopJit(const IopJit &) = delete;
        IopJit &operator=(const IopJit &) = delete;

        [[nodiscard]] bool enabled() const noexcept { return m_enabled; }
        // Runs one block at cpu.pc of at most maxInstructions; returns how many ran (0: none).
        uint32_t run(IopCpuState &cpu, uint32_t maxInstructions);

    private:
        using BlockFn = uint32_t (*)(IopCpuState *cpu, uint8_t *ram, uint8_t *owned);
        struct Block
        {
            BlockFn fn = nullptr; // null: no block starts here
            uint32_t length = 0;
            std::vector<uint32_t> words; // the code it was compiled from
        };

        Block compile(uint32_t phys);
        void clear();

        IopMemory &m_memory;
        bool m_enabled = false;
        uint8_t *m_arena = nullptr;
        size_t m_arenaUsed = 0;
        std::unordered_map<uint32_t, Block> m_blocks;
    };
}
