#include "iop_jit.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#if defined(__x86_64__) && !defined(_WIN32)
#include <sys/mman.h>
#define PS2X_IOP_JIT_SUPPORTED 1
#else
#define PS2X_IOP_JIT_SUPPORTED 0
#endif

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr size_t kArenaSize = 8u * 1024u * 1024u;
        constexpr uint32_t kMaxBlockInstructions = 64u;
        constexpr uint32_t kRamSize = IopMemory::RamSize;

        uint32_t gprOffset(uint32_t reg) { return static_cast<uint32_t>(offsetof(IopCpuState, gpr) + reg * 4u); }
        const uint32_t kHi = offsetof(IopCpuState, hi);
        const uint32_t kLo = offsetof(IopCpuState, lo);
        const uint32_t kPc = offsetof(IopCpuState, pc);
        const uint32_t kPendingReg = offsetof(IopCpuState, pendingLoadReg);
        const uint32_t kPendingValue = offsetof(IopCpuState, pendingLoadValue);
        const uint32_t kPendingLoad = offsetof(IopCpuState, pendingLoad);
        const uint32_t kBranchPending = offsetof(IopCpuState, branchPending);
        const uint32_t kBranchTarget = offsetof(IopCpuState, branchTarget);

        // Registers: rdi = IopCpuState*, rsi = RAM, rdx = written-byte map; eax, ecx, r8d, r9d scratch.
        struct Assembler
        {
            std::vector<uint8_t> b;
            void u8(uint8_t v) { b.push_back(v); }
            void u32(uint32_t v)
            {
                for (int s = 0; s < 32; s += 8)
                    u8(static_cast<uint8_t>(v >> s));
            }
            void bytes(std::initializer_list<uint8_t> list) { b.insert(b.end(), list); }
            // mov reg32, [rdi + off] / mov [rdi + off], reg32 (reg 0..15)
            void load(uint8_t reg, uint32_t off)
            {
                if (reg >= 8u)
                    u8(0x44);
                u8(0x8B);
                u8(static_cast<uint8_t>(0x80u | ((reg & 7u) << 3) | 7u));
                u32(off);
            }
            void store(uint32_t off, uint8_t reg)
            {
                if (reg >= 8u)
                    u8(0x44);
                u8(0x89);
                u8(static_cast<uint8_t>(0x80u | ((reg & 7u) << 3) | 7u));
                u32(off);
            }
            void storeImm(uint32_t off, uint32_t imm) { bytes({0xC7, 0x87}); u32(off); u32(imm); }
            void storeImm8(uint32_t off, uint8_t imm) { bytes({0xC6, 0x87}); u32(off); u8(imm); }
            void addMemImm(uint32_t off, uint32_t imm) { bytes({0x81, 0x87}); u32(off); u32(imm); }
            void movEaxImm(uint32_t imm) { u8(0xB8); u32(imm); }
            // Jcc rel32; returns the offset of the displacement to patch.
            size_t jcc(uint8_t cc)
            {
                bytes({0x0F, cc});
                const size_t at = b.size();
                u32(0);
                return at;
            }
            void patch(size_t at, size_t target)
            {
                const int32_t rel = static_cast<int32_t>(target - (at + 4u));
                std::memcpy(b.data() + at, &rel, 4);
            }
        };

        enum class Kind
        {
            None,
            Alu,
            Load,
            Store,
            Branch
        };

        Kind classify(uint32_t w)
        {
            const uint32_t op = w >> 26u, funct = w & 63u;
            if (op == 0x00)
            {
                switch (funct)
                {
                case 0x00: case 0x02: case 0x03: case 0x04: case 0x06: case 0x07:
                case 0x10: case 0x12: case 0x21: case 0x23: case 0x24: case 0x25:
                case 0x26: case 0x27: case 0x2A: case 0x2B:
                    return Kind::Alu;
                case 0x08: case 0x09:
                    return Kind::Branch;
                default:
                    return Kind::None;
                }
            }
            if (op == 0x01)
            {
                const uint32_t rt = (w >> 16u) & 31u;
                return rt == 0x00 || rt == 0x01 || rt == 0x10 || rt == 0x11 ? Kind::Branch : Kind::None;
            }
            switch (op)
            {
            case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
                return Kind::Branch;
            case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
                return Kind::Alu;
            case 0x20: case 0x21: case 0x23: case 0x24: case 0x25:
                return Kind::Load;
            case 0x28: case 0x29: case 0x2B:
                return Kind::Store;
            default:
                return Kind::None;
            }
        }
    }

    IopJit::IopJit(IopMemory &memory) : m_memory(memory)
    {
#if PS2X_IOP_JIT_SUPPORTED
        const char *setting = std::getenv("PS2X_IOP_JIT");
        if (setting != nullptr && setting[0] == '0')
            return;
        void *arena = mmap(nullptr, kArenaSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (arena == MAP_FAILED)
            return;
        m_arena = static_cast<uint8_t *>(arena);
        m_enabled = true;
#endif
    }

    IopJit::~IopJit()
    {
#if PS2X_IOP_JIT_SUPPORTED
        if (m_arena != nullptr)
            munmap(m_arena, kArenaSize);
#endif
    }

    void IopJit::clear()
    {
        m_blocks.clear();
        m_arenaUsed = 0;
    }

    IopJit::Block IopJit::compile(uint32_t phys)
    {
        Block block;
        const uint8_t *ram = m_memory.ramData();
        uint32_t count = 0;
        bool endsInBranch = false;
        while (count < kMaxBlockInstructions && phys + (count + 1u) * 4u <= kRamSize)
        {
            uint32_t w;
            std::memcpy(&w, ram + phys + count * 4u, 4);
            const Kind kind = classify(w);
            if (kind == Kind::None)
                break;
            if (kind == Kind::Branch)
            {
                // The branch and its delay slot end the block. An import stub (jr ra with
                // addiu zero in the slot) belongs to the interpreter's import dispatch.
                uint32_t slot = 0;
                if (count + 1u >= kMaxBlockInstructions || phys + (count + 2u) * 4u > kRamSize)
                    break;
                std::memcpy(&slot, ram + phys + (count + 1u) * 4u, 4);
                const Kind slotKind = classify(slot);
                if (slotKind == Kind::None || slotKind == Kind::Branch ||
                    (w == 0x03E00008u && (slot & 0xFFFF0000u) == 0x24000000u))
                    break;
                block.words.push_back(w);
                block.words.push_back(slot);
                count += 2u;
                endsInBranch = true;
                break;
            }
            block.words.push_back(w);
            ++count;
        }
        if (count < 2u) // one instruction: the interpreter is as fast. Keep the two words the
        {               // decision rests on, so reloaded code there gets looked at again.
            block.words.resize(2);
            std::memcpy(block.words.data(), ram + phys, 8);
            block.length = count;
            return block;
        }

        Assembler a;
        std::vector<uint32_t> pendingAtStart(count); // load still in its delay slot when instruction i starts
        std::vector<std::pair<size_t, uint32_t>> bails;   // jcc to patch, instruction index
        uint32_t pending = 0;
        auto commitPending = [&](uint32_t writtenReg)
        {
            if (pending != 0u && pending != writtenReg)
            {
                a.load(9, kPendingValue);
                a.store(gprOffset(pending), 9);
            }
        };
        for (uint32_t i = 0; i < count; ++i)
        {
            pendingAtStart[i] = pending;
            const uint32_t w = block.words[i];
            const uint32_t op = w >> 26u, rs = (w >> 21u) & 31u, rt = (w >> 16u) & 31u, rd = (w >> 11u) & 31u;
            const uint32_t sa = (w >> 6u) & 31u, funct = w & 63u, imm = w & 0xFFFFu;
            const uint32_t simm = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(imm)));
            const Kind kind = classify(w);
            if (kind == Kind::Branch)
            {
                // Link first: the interpreter writes ra (or rd) before it reads the condition
                // or the jump register. pc in memory is still the block's entry pc here.
                uint32_t link = 0;
                if (op == 0x03 || (op == 0x01 && (rt == 0x10 || rt == 0x11)))
                    link = 31u;
                else if (op == 0x00 && funct == 0x09)
                    link = rd != 0u ? rd : 31u;
                if (link != 0u)
                {
                    a.load(0, kPc);
                    a.u8(0x05);
                    a.u32(i * 4u + 8u);
                    a.store(gprOffset(link), 0);
                }
                if (op == 0x00) // jr / jalr: always taken, target from rs
                {
                    a.load(0, gprOffset(rs));
                    a.storeImm8(kBranchPending, 1u);
                    a.store(kBranchTarget, 0);
                }
                else if (op == 0x02 || op == 0x03) // j / jal
                {
                    a.load(0, kPc);
                    a.u8(0x05);
                    a.u32((i + 1u) * 4u);
                    a.u8(0x25);
                    a.u32(0xF0000000u);
                    a.u8(0x0D);
                    a.u32((w & 0x03FFFFFFu) << 2u);
                    a.storeImm8(kBranchPending, 1u);
                    a.store(kBranchTarget, 0);
                }
                else // conditional: branchPending = taken, branchTarget = taken ? target : 0
                {
                    a.load(0, kPc);
                    a.u8(0x05);
                    a.u32((i + 1u) * 4u + (simm << 2u));
                    a.load(8, gprOffset(rs));
                    uint8_t setcc;
                    if (op == 0x04 || op == 0x05)
                    {
                        a.load(9, gprOffset(rt));
                        a.bytes({0x45, 0x39, 0xC8}); // cmp r8d, r9d
                        setcc = op == 0x04 ? 0x94 : 0x95; // sete / setne
                    }
                    else
                    {
                        a.bytes({0x41, 0x83, 0xF8, 0x00}); // cmp r8d, 0
                        if (op == 0x06)
                            setcc = 0x9E; // setle
                        else if (op == 0x07)
                            setcc = 0x9F; // setg
                        else
                            setcc = (rt & 1u) == 0u ? 0x9C : 0x9D; // bltz(al): setl, bgez(al): setge
                    }
                    a.bytes({0x0F, setcc, 0xC1, 0x0F, 0xB6, 0xC9}); // setcc cl; movzx ecx, cl
                    a.bytes({0x88, 0x8F});
                    a.u32(kBranchPending); // mov [rdi+branchPending], cl
                    a.bytes({0xF7, 0xD9, 0x21, 0xC8}); // neg ecx; and eax, ecx
                    a.store(kBranchTarget, 0);
                }
                commitPending(link);
                pending = 0;
                continue;
            }
            if (kind == Kind::Alu)
            {
                uint32_t dest = op == 0x00 ? rd : rt;
                if (op == 0x00)
                {
                    switch (funct)
                    {
                    case 0x00: a.load(0, gprOffset(rt)); a.bytes({0xC1, 0xE0, static_cast<uint8_t>(sa)}); break; // shl
                    case 0x02: a.load(0, gprOffset(rt)); a.bytes({0xC1, 0xE8, static_cast<uint8_t>(sa)}); break; // shr
                    case 0x03: a.load(0, gprOffset(rt)); a.bytes({0xC1, 0xF8, static_cast<uint8_t>(sa)}); break; // sar
                    case 0x04: a.load(0, gprOffset(rt)); a.load(1, gprOffset(rs)); a.bytes({0xD3, 0xE0}); break; // shl eax, cl
                    case 0x06: a.load(0, gprOffset(rt)); a.load(1, gprOffset(rs)); a.bytes({0xD3, 0xE8}); break;
                    case 0x07: a.load(0, gprOffset(rt)); a.load(1, gprOffset(rs)); a.bytes({0xD3, 0xF8}); break;
                    case 0x10: a.load(0, kHi); break;
                    case 0x12: a.load(0, kLo); break;
                    case 0x21: a.load(0, gprOffset(rs)); a.load(1, gprOffset(rt)); a.bytes({0x01, 0xC8}); break; // add
                    case 0x23: a.load(0, gprOffset(rs)); a.load(1, gprOffset(rt)); a.bytes({0x29, 0xC8}); break; // sub
                    case 0x24: a.load(0, gprOffset(rs)); a.load(1, gprOffset(rt)); a.bytes({0x21, 0xC8}); break; // and
                    case 0x25: a.load(0, gprOffset(rs)); a.load(1, gprOffset(rt)); a.bytes({0x09, 0xC8}); break; // or
                    case 0x26: a.load(0, gprOffset(rs)); a.load(1, gprOffset(rt)); a.bytes({0x31, 0xC8}); break; // xor
                    case 0x27: a.load(0, gprOffset(rs)); a.load(1, gprOffset(rt)); a.bytes({0x09, 0xC8, 0xF7, 0xD0}); break; // or; not
                    case 0x2A: a.load(0, gprOffset(rs)); a.load(1, gprOffset(rt)); a.bytes({0x39, 0xC8, 0x0F, 0x9C, 0xC0, 0x0F, 0xB6, 0xC0}); break; // setl
                    case 0x2B: a.load(0, gprOffset(rs)); a.load(1, gprOffset(rt)); a.bytes({0x39, 0xC8, 0x0F, 0x92, 0xC0, 0x0F, 0xB6, 0xC0}); break; // setb
                    }
                }
                else
                {
                    switch (op)
                    {
                    case 0x09: a.load(0, gprOffset(rs)); a.u8(0x05); a.u32(simm); break; // addiu
                    case 0x0A: a.load(0, gprOffset(rs)); a.u8(0x3D); a.u32(simm); a.bytes({0x0F, 0x9C, 0xC0, 0x0F, 0xB6, 0xC0}); break; // slti
                    case 0x0B: a.load(0, gprOffset(rs)); a.u8(0x3D); a.u32(simm); a.bytes({0x0F, 0x92, 0xC0, 0x0F, 0xB6, 0xC0}); break; // sltiu
                    case 0x0C: a.load(0, gprOffset(rs)); a.u8(0x25); a.u32(imm); break;
                    case 0x0D: a.load(0, gprOffset(rs)); a.u8(0x0D); a.u32(imm); break;
                    case 0x0E: a.load(0, gprOffset(rs)); a.u8(0x35); a.u32(imm); break;
                    case 0x0F: a.movEaxImm(imm << 16u); break;
                    }
                }
                if (dest != 0u)
                    a.store(gprOffset(dest), 0);
                commitPending(dest);
                pending = 0;
                continue;
            }

            // Loads and stores: address, alignment and RAM checks first; failing one leaves the
            // block before this instruction (the interpreter raises the exception or goes to
            // hardware).
            const uint32_t size = (op == 0x20 || op == 0x24 || op == 0x28) ? 1u : (op == 0x21 || op == 0x25 || op == 0x29) ? 2u : 4u;
            a.load(0, gprOffset(rs));
            a.u8(0x05);
            a.u32(simm); // add eax, simm
            if (size > 1u)
            {
                a.bytes({0xA8, static_cast<uint8_t>(size - 1u)}); // test al, size-1
                bails.emplace_back(a.jcc(0x85), i);                // jnz
            }
            a.u8(0x25);
            a.u32(0x1FFFFFFFu); // and eax, physical mask
            a.u8(0x3D);
            a.u32(kRamSize - size); // cmp eax, last valid address
            bails.emplace_back(a.jcc(0x87), i); // ja
            if (kind == Kind::Load)
            {
                switch (op)
                {
                case 0x20: a.bytes({0x44, 0x0F, 0xBE, 0x04, 0x06}); break; // movsx r8d, byte [rsi+rax]
                case 0x24: a.bytes({0x44, 0x0F, 0xB6, 0x04, 0x06}); break; // movzx r8d, byte
                case 0x21: a.bytes({0x44, 0x0F, 0xBF, 0x04, 0x06}); break; // movsx r8d, word
                case 0x25: a.bytes({0x44, 0x0F, 0xB7, 0x04, 0x06}); break; // movzx r8d, word
                case 0x23: a.bytes({0x44, 0x8B, 0x04, 0x06}); break;       // mov r8d, dword
                }
                commitPending(0u); // a load writes no register now
                if (rt != 0u)
                    a.store(kPendingValue, 8);
                pending = rt;
                continue;
            }
            a.load(1, gprOffset(rt));
            switch (size)
            {
            case 1: a.bytes({0x88, 0x0C, 0x06, 0xC6, 0x04, 0x02, 0x01}); break;                         // mov [rsi+rax], cl; mov byte [rdx+rax], 1
            case 2: a.bytes({0x66, 0x89, 0x0C, 0x06, 0x66, 0xC7, 0x04, 0x02, 0x01, 0x01}); break;       // word
            default: a.bytes({0x89, 0x0C, 0x06, 0xC7, 0x04, 0x02, 0x01, 0x01, 0x01, 0x01}); break;     // dword
            }
            commitPending(0u);
            pending = 0;
        }
        auto leave = [&](uint32_t executed, uint32_t loadInDelaySlot)
        {
            a.addMemImm(kPc, executed * 4u);
            if (loadInDelaySlot != 0u)
            {
                a.storeImm(kPendingReg, loadInDelaySlot);
                a.storeImm8(kPendingLoad, 1u);
            }
            a.movEaxImm(executed);
            a.u8(0xC3);
        };
        if (endsInBranch)
        {
            // After the delay slot: pc = taken ? branchTarget : next; the branch is resolved.
            a.load(0, kPc);
            a.u8(0x05);
            a.u32(count * 4u);
            a.load(1, kBranchTarget);
            a.bytes({0x80, 0xBF});
            a.u32(kBranchPending);
            a.u8(0x00);                      // cmp byte [rdi+branchPending], 0
            a.bytes({0x0F, 0x45, 0xC1});     // cmovne eax, ecx
            a.store(kPc, 0);
            a.storeImm8(kBranchPending, 0u);
            if (pending != 0u)
            {
                a.storeImm(kPendingReg, pending);
                a.storeImm8(kPendingLoad, 1u);
            }
            a.movEaxImm(count);
            a.u8(0xC3);
        }
        else
            leave(count, pending);
        for (const auto &[at, index] : bails)
        {
            a.patch(at, a.b.size());
            leave(index, pendingAtStart[index]);
        }

        if (m_arenaUsed + a.b.size() > kArenaSize)
            clear();
        std::memcpy(m_arena + m_arenaUsed, a.b.data(), a.b.size());
        block.fn = reinterpret_cast<BlockFn>(m_arena + m_arenaUsed);
        m_arenaUsed += (a.b.size() + 15u) & ~size_t{15};
        block.length = count;
        return block;
    }

    uint32_t IopJit::run(IopCpuState &cpu, uint32_t maxInstructions)
    {
        if (!m_enabled || (cpu.pc & 3u) != 0u)
            return 0;
        const uint32_t phys = cpu.pc & 0x1FFFFFFFu;
        if (phys + 8u > kRamSize)
            return 0;
        uint8_t *ram = m_memory.ramData();
        auto found = m_blocks.find(phys);
        // Code can be reloaded (modules): a block runs only on the words it was built from.
        if (found == m_blocks.end() || std::memcmp(found->second.words.data(), ram + phys, found->second.words.size() * 4u) != 0)
        {
            const size_t arenaBefore = m_arenaUsed;
            Block block = compile(phys);
            if (m_arenaUsed < arenaBefore) // the arena was cleared: every older block is gone
                m_blocks.clear();
            found = m_blocks.insert_or_assign(phys, std::move(block)).first;
        }
        const Block &block = found->second;
        if (block.fn == nullptr || block.length > maxInstructions)
            return 0;
        return block.fn(&cpu, ram, m_memory.ownedData());
    }
}
