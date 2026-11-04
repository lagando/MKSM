// x86-64 SSE recompiler for VU1 microcode.
//
// Runs of simple instruction pairs become native blocks working on the same
// VU1State as the interpreter, so run() can alternate between the two on every
// pair. Everything with timing that a program can observe (branches, CLIP, Q and
// P producers, XGKICK, flag operations) stays in the interpreter.
//
// A block's cycle count comes from the interpreter's own interlock rules, applied
// at compile time. Values read from outside the block are checked at entry.
//
// PS2X_VU_JIT=0 disables it. PS2X_VU_JIT_CHECK=N also runs one block in N through the
// interpreter and reports any difference in registers, memory or cycles.
#include "runtime/ps2_vu1.h"
#include "ps2_runtime.h"
#include "ps2_vu1_detail.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#if defined(__x86_64__) && !defined(_WIN32)
#include <sys/mman.h>
#define PS2_VU_JIT_SUPPORTED 1
#else
#define PS2_VU_JIT_SUPPORTED 0
#endif

namespace
{
    constexpr uint32_t kCodeArenaSize = 16u * 1024u * 1024u;
    constexpr uint32_t kMinBlockPairs = 2u;
    constexpr uint32_t kMaxBlockPairs = 256u;
    // shortcut: MAC/STATUS are only produced by interpreted pairs, and pairs this close
    // before a flag read stay interpreted. A reader reached by a branch from further
    // away would see stale flags; MKSM's programs read them right after computing.
    constexpr uint32_t kFlagReaderWindow = 16u;

    class Assembler
    {
    public:
        std::vector<uint8_t> bytes;

        void u8(uint8_t value) { bytes.push_back(value); }
        void u32(uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
                u8(static_cast<uint8_t>(value >> shift));
        }
        void modrmDisp(uint8_t reg, uint32_t disp)
        {
            u8(static_cast<uint8_t>(0x80u | (reg << 3) | 7u)); // [rdi+disp32]
            u32(disp);
        }
        void op0F(uint8_t opcode, uint8_t dst, uint8_t src)
        {
            u8(0x0F);
            u8(opcode);
            u8(static_cast<uint8_t>(0xC0u | (dst << 3) | src));
        }

        void loadState(uint8_t xmm, uint32_t disp) { u8(0x0F); u8(0x10); modrmDisp(xmm, disp); }
        void storeState(uint32_t disp, uint8_t xmm) { u8(0x0F); u8(0x11); modrmDisp(xmm, disp); }
        void loadScalarState(uint8_t xmm, uint32_t disp)
        {
            u8(0xF3); u8(0x0F); u8(0x10); modrmDisp(xmm, disp);
            shufps(xmm, xmm, 0x00);
        }
        void loadConstant(uint8_t xmm, const void *address)
        {
            u8(0x48); u8(0xB8); // mov rax, imm64
            const uint64_t value = reinterpret_cast<uint64_t>(address);
            for (int shift = 0; shift < 64; shift += 8)
                u8(static_cast<uint8_t>(value >> shift));
            u8(0x0F); u8(0x10); u8(static_cast<uint8_t>((xmm << 3) | 0u)); // movups xmm, [rax]
        }
        void movaps(uint8_t dst, uint8_t src) { op0F(0x28, dst, src); }
        void addps(uint8_t dst, uint8_t src) { op0F(0x58, dst, src); }
        void mulps(uint8_t dst, uint8_t src) { op0F(0x59, dst, src); }
        void subps(uint8_t dst, uint8_t src) { op0F(0x5C, dst, src); }
        void minps(uint8_t dst, uint8_t src) { op0F(0x5D, dst, src); }
        void maxps(uint8_t dst, uint8_t src) { op0F(0x5F, dst, src); }
        void andps(uint8_t dst, uint8_t src) { op0F(0x54, dst, src); }
        void xorps(uint8_t dst, uint8_t src) { op0F(0x57, dst, src); }
        void cvtdq2ps(uint8_t dst, uint8_t src) { op0F(0x5B, dst, src); }
        void cvttps2dq(uint8_t dst, uint8_t src) { u8(0xF3); op0F(0x5B, dst, src); }
        void shufps(uint8_t dst, uint8_t src, uint8_t imm) { op0F(0xC6, dst, src); u8(imm); }
        void cmpnltps(uint8_t dst, uint8_t src) { op0F(0xC2, dst, src); u8(5); }
        void andnps(uint8_t dst, uint8_t src) { op0F(0x55, dst, src); }
        void orps(uint8_t dst, uint8_t src) { op0F(0x56, dst, src); }
        void pcmpeqd(uint8_t dst, uint8_t src) { u8(0x66); op0F(0x76, dst, src); }
        void movmskpsEax(uint8_t src) { op0F(0x50, 0, src); }
        void testEaxEax() { u8(0x85); u8(0xC0); }
        void jz(uint8_t displacement) { u8(0x74); u8(displacement); }
        void pcmpgtd(uint8_t dst, uint8_t src) { u8(0x66); op0F(0x66, dst, src); }
        void pxor(uint8_t dst, uint8_t src) { u8(0x66); op0F(0xEF, dst, src); }
        size_t jzFar()
        {
            u8(0x0F); u8(0x84); u32(0u);
            return bytes.size();
        }
        void patchJump(size_t afterJump)
        {
            const uint32_t distance = static_cast<uint32_t>(bytes.size() - afterJump);
            std::memcpy(bytes.data() + afterJump - 4u, &distance, 4u);
        }
        void callAbsolute(const void *function)
        {
            u8(0x48); u8(0xB8);
            const uint64_t value = reinterpret_cast<uint64_t>(function);
            for (int shift = 0; shift < 64; shift += 8)
                u8(static_cast<uint8_t>(value >> shift));
            u8(0xFF); u8(0xD0); // call rax
        }

        // Inf and NaN operands become signed MAX, like normalizeOperand. Denormals are
        // handled by DAZ. Expects kVecMax, kVecExponent and kVecSign in xmm4 to xmm6.
        void clampOperand(uint8_t xmm)
        {
            movaps(3, xmm);
            andps(3, 5);
            pcmpeqd(3, 5);
            movmskpsEax(3);
            testEaxEax();
            constexpr uint8_t kCorrectionBytes = 7u * 3u;
            jz(kCorrectionBytes);
            movaps(7, xmm);
            andps(7, 6);
            orps(7, 4);
            andps(7, 3);
            andnps(3, xmm);
            orps(3, 7);
            movaps(xmm, 3);
        }
        void blendps(uint8_t dst, uint8_t src, uint8_t imm)
        {
            u8(0x66); u8(0x0F); u8(0x3A); u8(0x0C);
            u8(static_cast<uint8_t>(0xC0u | (dst << 3) | src));
            u8(imm);
        }

        void loadViEax(uint32_t disp) { u8(0x8B); modrmDisp(0, disp); }
        void loadViEcx(uint32_t disp) { u8(0x8B); modrmDisp(1, disp); }
        void storeViEax(uint32_t disp) { u8(0x89); modrmDisp(0, disp); }
        void storeImm32(uint32_t disp, uint32_t value)
        {
            u8(0xC7); modrmDisp(0, disp); u32(value);
        }
        void addEaxImm(uint32_t value) { u8(0x05); u32(value); }
        void andEaxImm(uint32_t value) { u8(0x25); u32(value); }
        void addEaxEcx() { u8(0x01); u8(0xC8); }
        void subEaxEcx() { u8(0x29); u8(0xC8); }
        void andEaxEcx() { u8(0x21); u8(0xC8); }
        void orEaxEcx() { u8(0x09); u8(0xC8); }
        void shlEax4() { u8(0xC1); u8(0xE0); u8(0x04); }
        void movsxEaxAx() { u8(0x0F); u8(0xBF); u8(0xC0); }
        void movzxEaxAx() { u8(0x0F); u8(0xB7); u8(0xC0); }
        void movdXmmEax(uint8_t xmm) { u8(0x66); u8(0x0F); u8(0x6E); u8(static_cast<uint8_t>(0xC0u | (xmm << 3))); }
        void loadData(uint8_t xmm) { u8(0x0F); u8(0x10); u8(static_cast<uint8_t>((xmm << 3) | 4u)); u8(0x06); }  // [rsi+rax]
        void storeData(uint8_t xmm) { u8(0x0F); u8(0x11); u8(static_cast<uint8_t>((xmm << 3) | 4u)); u8(0x06); } // [rsi+rax]
        void ret() { u8(0xC3); }
    };

    struct alignas(16) Vec4
    {
        float value[4];
    };
    struct alignas(16) Vec4i
    {
        uint32_t value[4];
    };

    const Vec4 kItofScale[4] = {{{1.0f, 1.0f, 1.0f, 1.0f}},
                                {{1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f}},
                                {{1.0f / 4096.0f, 1.0f / 4096.0f, 1.0f / 4096.0f, 1.0f / 4096.0f}},
                                {{1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f / 32768.0f}}};
    const Vec4 kFtoiScale[4] = {{{1.0f, 1.0f, 1.0f, 1.0f}},
                                {{16.0f, 16.0f, 16.0f, 16.0f}},
                                {{4096.0f, 4096.0f, 4096.0f, 4096.0f}},
                                {{32768.0f, 32768.0f, 32768.0f, 32768.0f}}};
    const Vec4 kInt32Limit = {{2147483648.0f, 2147483648.0f, 2147483648.0f, 2147483648.0f}};
    const Vec4i kAbsMask = {{0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu}};
    const Vec4i kVecMax = {{0x7F7FFFFFu, 0x7F7FFFFFu, 0x7F7FFFFFu, 0x7F7FFFFFu}};
    const Vec4i kVecExponent = {{0x7F800000u, 0x7F800000u, 0x7F800000u, 0x7F800000u}};
    const Vec4i kVecSign = {{0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u}};
    const Vec4i kExponentBelowFe = {{0x7EFFFFFFu, 0x7EFFFFFFu, 0x7EFFFFFFu, 0x7EFFFFFFu}};

    uint8_t laneForComponent(uint32_t component) { return static_cast<uint8_t>(8u >> component); }

    uint8_t blendMask(uint8_t dest)
    {
        return static_cast<uint8_t>(((dest >> 3) & 1u) | (((dest >> 2) & 1u) << 1) |
                                    (((dest >> 1) & 1u) << 2) | ((dest & 1u) << 3));
    }

    uint32_t vfOffset(uint32_t reg) { return static_cast<uint32_t>(offsetof(VU1State, vf)) + reg * 16u; }
    uint32_t viOffset(uint32_t reg) { return static_cast<uint32_t>(offsetof(VU1State, vi)) + reg * 4u; }
    uint32_t accOffset() { return static_cast<uint32_t>(offsetof(VU1State, acc)); }
    uint32_t qOffset() { return static_cast<uint32_t>(offsetof(VU1State, q)); }
    uint32_t iOffset() { return static_cast<uint32_t>(offsetof(VU1State, i)); }

    enum class UpperKind : uint8_t
    {
        Unsupported,
        Nop,
        Add,
        Sub,
        Mul,
        Madd,
        Msub,
        Max,
        Min,
        Itof,
        Ftoi,
        Abs
    };
    enum class UpperSource : uint8_t
    {
        Broadcast,
        Q,
        I,
        Vector
    };

    struct UpperOp
    {
        UpperKind kind = UpperKind::Unsupported;
        UpperSource source = UpperSource::Vector;
        uint8_t broadcast = 0;
        uint8_t scale = 0;
        bool toAcc = false;
    };

    UpperOp decodeUpper(uint32_t instr)
    {
        UpperOp op;
        const uint8_t code = instr & 0x3Fu;
        const auto set = [&op](UpperKind kind, UpperSource source, uint8_t broadcast = 0u, bool toAcc = false)
        {
            op.kind = kind;
            op.source = source;
            op.broadcast = broadcast;
            op.toAcc = toAcc;
        };
        if (code < 0x3Cu)
        {
            const uint8_t bc = code & 3u;
            if (code <= 0x03u) set(UpperKind::Add, UpperSource::Broadcast, bc);
            else if (code <= 0x07u) set(UpperKind::Sub, UpperSource::Broadcast, bc);
            else if (code <= 0x0Bu) set(UpperKind::Madd, UpperSource::Broadcast, bc);
            else if (code <= 0x0Fu) set(UpperKind::Msub, UpperSource::Broadcast, bc);
            else if (code <= 0x13u) set(UpperKind::Max, UpperSource::Broadcast, bc);
            else if (code <= 0x17u) set(UpperKind::Min, UpperSource::Broadcast, bc);
            else if (code <= 0x1Bu) set(UpperKind::Mul, UpperSource::Broadcast, bc);
            else switch (code)
            {
            case 0x1C: set(UpperKind::Mul, UpperSource::Q); break;
            case 0x1D: set(UpperKind::Max, UpperSource::I); break;
            case 0x1E: set(UpperKind::Mul, UpperSource::I); break;
            case 0x1F: set(UpperKind::Min, UpperSource::I); break;
            case 0x20: set(UpperKind::Add, UpperSource::Q); break;
            case 0x21: set(UpperKind::Madd, UpperSource::Q); break;
            case 0x22: set(UpperKind::Add, UpperSource::I); break;
            case 0x23: set(UpperKind::Madd, UpperSource::I); break;
            case 0x24: set(UpperKind::Sub, UpperSource::Q); break;
            case 0x25: set(UpperKind::Msub, UpperSource::Q); break;
            case 0x26: set(UpperKind::Sub, UpperSource::I); break;
            case 0x27: set(UpperKind::Msub, UpperSource::I); break;
            case 0x28: set(UpperKind::Add, UpperSource::Vector); break;
            case 0x29: set(UpperKind::Madd, UpperSource::Vector); break;
            case 0x2A: set(UpperKind::Mul, UpperSource::Vector); break;
            case 0x2B: set(UpperKind::Max, UpperSource::Vector); break;
            case 0x2C: set(UpperKind::Sub, UpperSource::Vector); break;
            case 0x2D: set(UpperKind::Msub, UpperSource::Vector); break;
            case 0x2F: set(UpperKind::Min, UpperSource::Vector); break;
            default: break;
            }
            return op;
        }

        const uint8_t special = static_cast<uint8_t>((instr & 3u) | ((instr >> 4) & 0x7Cu));
        const uint8_t bc = special & 3u;
        if (special <= 0x03u) set(UpperKind::Add, UpperSource::Broadcast, bc, true);
        else if (special <= 0x07u) set(UpperKind::Sub, UpperSource::Broadcast, bc, true);
        else if (special <= 0x0Bu) set(UpperKind::Madd, UpperSource::Broadcast, bc, true);
        else if (special <= 0x0Fu) set(UpperKind::Msub, UpperSource::Broadcast, bc, true);
        else if (special <= 0x13u) { op.kind = UpperKind::Itof; op.scale = special & 3u; }
        else if (special <= 0x17u) { op.kind = UpperKind::Ftoi; op.scale = special & 3u; }
        else if (special <= 0x1Bu) set(UpperKind::Mul, UpperSource::Broadcast, bc, true);
        else switch (special)
        {
        case 0x1C: set(UpperKind::Mul, UpperSource::Q, 0u, true); break;
        case 0x1D: op.kind = UpperKind::Abs; break;
        case 0x1E: set(UpperKind::Mul, UpperSource::I, 0u, true); break;
        case 0x20: set(UpperKind::Add, UpperSource::Q, 0u, true); break;
        case 0x21: set(UpperKind::Madd, UpperSource::Q, 0u, true); break;
        case 0x22: set(UpperKind::Add, UpperSource::I, 0u, true); break;
        case 0x23: set(UpperKind::Madd, UpperSource::I, 0u, true); break;
        case 0x24: set(UpperKind::Sub, UpperSource::Q, 0u, true); break;
        case 0x25: set(UpperKind::Msub, UpperSource::Q, 0u, true); break;
        case 0x26: set(UpperKind::Sub, UpperSource::I, 0u, true); break;
        case 0x27: set(UpperKind::Msub, UpperSource::I, 0u, true); break;
        case 0x28: set(UpperKind::Add, UpperSource::Vector, 0u, true); break;
        case 0x29: set(UpperKind::Madd, UpperSource::Vector, 0u, true); break;
        case 0x2A: set(UpperKind::Mul, UpperSource::Vector, 0u, true); break;
        case 0x2C: set(UpperKind::Sub, UpperSource::Vector, 0u, true); break;
        case 0x2D: set(UpperKind::Msub, UpperSource::Vector, 0u, true); break;
        case 0x2F: op.kind = UpperKind::Nop; break;
        default: break;
        }
        return op;
    }

    bool lowerIsSupported(uint32_t instr)
    {
        if (instr == 0u || instr == 0x8000033Cu)
            return true;
        const uint8_t opHi = (instr >> 25) & 0x7Fu;
        switch (opHi)
        {
        case 0x00: case 0x01: case 0x08: case 0x09:
            return true;
        case 0x40:
            break;
        default:
            return false;
        }
        const uint8_t funct = instr & 0x3Fu;
        if (funct == 0x30u || funct == 0x31u || funct == 0x32u || funct == 0x34u || funct == 0x35u)
            return true;
        if (funct < 0x3Cu)
            return false;
        const uint8_t special = static_cast<uint8_t>((instr & 3u) | ((instr >> 4) & 0x7Cu));
        return special == 0x30u || special == 0x31u || (special >= 0x34u && special <= 0x37u) ||
               special == 0x3Cu || special == 0x3Du;
    }

    // Lower instructions a block runs through the interpreter's own code at their cycle
    // (emitCallout): FDIV/EFU/clip-flag pipelines and integer memory ops.
    void loadJitConstants(Assembler &a);

    bool lowerIsCallout(uint32_t instr)
    {
        switch ((instr >> 25) & 0x7Fu)
        {
        case 0x04: case 0x05:                                  // ILW, ISW
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x1C: // FCEQ, FCSET, FCAND, FCOR, FCGET
            return true;
        case 0x40:
            break;
        default:
            return false;
        }
        if ((instr & 0x3Fu) < 0x3Cu)
            return false;
        const uint8_t special = static_cast<uint8_t>((instr & 3u) | ((instr >> 4) & 0x7Cu));
        return (special >= 0x38u && special <= 0x3Au) ||          // DIV, SQRT, RSQRT
               special == 0x3Eu || special == 0x3Fu ||            // ILWR, ISWR
               special == 0x64u ||                                // MFP
               (special >= 0x70u && special <= 0x7Fu && special != 0x7Bu); // EFU ops but WAITP
    }

    bool upperIsClip(uint32_t instr)
    {
        return (instr & 0x3Cu) == 0x3Cu && (((instr >> 4) & 0x7Cu) | (instr & 3u)) == 0x1Fu;
    }

    // Calls function(interpreter, instr, cycle offset in the block), keeping xmm0: the
    // pair's upper result is still unstored there.
    void emitCallout(Assembler &a, const void *function, uint32_t instr, uint32_t offset)
    {
        a.u8(0x57); a.u8(0x56); a.u8(0x52);             // push rdi, rsi, rdx
        a.u8(0x48); a.u8(0x83); a.u8(0xEC); a.u8(0x10); // sub rsp, 16
        a.u8(0x0F); a.u8(0x11); a.u8(0x04); a.u8(0x24); // movups [rsp], xmm0
        a.u8(0x48); a.u8(0x89); a.u8(0xD7);             // mov rdi, rdx (interpreter)
        a.u8(0xBE); a.u32(instr);                       // mov esi, instr
        a.u8(0xBA); a.u32(offset);                      // mov edx, offset
        a.callAbsolute(function);
        a.u8(0x0F); a.u8(0x10); a.u8(0x04); a.u8(0x24); // movups xmm0, [rsp]
        a.u8(0x48); a.u8(0x83); a.u8(0xC4); a.u8(0x10); // add rsp, 16
        a.u8(0x5A); a.u8(0x5E); a.u8(0x5F);             // pop rdx, rsi, rdi
        loadJitConstants(a);
    }

    bool upperReadsQ(const UpperOp &op) { return op.kind != UpperKind::Unsupported && op.kind != UpperKind::Nop &&
                                                 op.kind != UpperKind::Itof && op.kind != UpperKind::Ftoi &&
                                                 op.kind != UpperKind::Abs && op.source == UpperSource::Q; }

    void loadJitConstants(Assembler &a)
    {
        a.loadConstant(4, &kVecMax);
        a.loadConstant(5, &kVecExponent);
        a.loadConstant(6, &kVecSign);
    }

    // Lanes where truncated single precision and the interpreter's exact form part ways:
    // a result or product at exponent 0xFE/0xFF and, for MADD/MSUB, a zero or subnormal
    // sum whose exact sign can differ. They are rare: hand them to the interpreter's own
    // code while the VF and ACC still hold the pre-pair values it reads.
    void emitExactFmacFixup(Assembler &a, uint32_t instr, bool checksProduct, const void *fixup)
    {
        a.movaps(3, 0);
        a.andps(3, 5);
        if (checksProduct)
            a.movaps(2, 3);
        a.loadConstant(7, &kExponentBelowFe);
        a.pcmpgtd(3, 7);
        if (checksProduct)
        {
            a.pxor(7, 7);
            a.pcmpeqd(2, 7);
            a.orps(3, 2);
            a.movaps(2, 1);
            a.andps(2, 5);
            a.loadConstant(7, &kExponentBelowFe);
            a.pcmpgtd(2, 7);
            a.orps(3, 2);
        }
        a.movmskpsEax(3);
        a.andEaxImm(blendMask(DEST(instr)));
        const size_t skip = a.jzFar();
        a.u8(0x57); a.u8(0x56); a.u8(0x52);             // push rdi, rsi, rdx
        a.u8(0x48); a.u8(0x83); a.u8(0xEC); a.u8(0x10); // sub rsp, 16
        a.u8(0x0F); a.u8(0x11); a.u8(0x04); a.u8(0x24); // movups [rsp], xmm0
        a.u8(0x48); a.u8(0x89); a.u8(0xD7);             // mov rdi, rdx (interpreter)
        a.u8(0xBE); a.u32(instr);                       // mov esi, instr
        a.u8(0x48); a.u8(0x89); a.u8(0xE2);             // mov rdx, rsp
        a.callAbsolute(fixup);
        a.u8(0x0F); a.u8(0x10); a.u8(0x04); a.u8(0x24); // movups xmm0, [rsp]
        a.u8(0x48); a.u8(0x83); a.u8(0xC4); a.u8(0x10); // add rsp, 16
        a.u8(0x5A); a.u8(0x5E); a.u8(0x5F);             // pop rdx, rsi, rdi
        loadJitConstants(a);
        a.patchJump(skip);
    }

    // Leaves the masked result in xmm0, unstored: the lower instruction of the pair must
    // still see the old register value.
    void emitUpper(Assembler &a, uint32_t instr, const UpperOp &op, const void *fixup)
    {
        const uint8_t fs = FS(instr), ft = FT(instr);
        const auto loadSource = [&](uint8_t xmm)
        {
            switch (op.source)
            {
            case UpperSource::Broadcast:
                a.loadState(xmm, vfOffset(ft));
                a.shufps(xmm, xmm, static_cast<uint8_t>(op.broadcast * 0x55u));
                break;
            case UpperSource::Q: a.loadScalarState(xmm, qOffset()); break;
            case UpperSource::I: a.loadScalarState(xmm, iOffset()); break;
            case UpperSource::Vector: a.loadState(xmm, vfOffset(ft)); break;
            }
            a.clampOperand(xmm);
        };
        const auto loadFs = [&](uint8_t xmm)
        {
            a.loadState(xmm, vfOffset(fs));
            a.clampOperand(xmm);
        };

        switch (op.kind)
        {
        case UpperKind::Add:
            loadFs(0); loadSource(1); a.addps(0, 1);
            emitExactFmacFixup(a, instr, false, fixup);
            break;
        case UpperKind::Sub:
            loadFs(0); loadSource(1); a.subps(0, 1);
            emitExactFmacFixup(a, instr, false, fixup);
            break;
        case UpperKind::Mul:
            loadFs(0); loadSource(1); a.mulps(0, 1);
            emitExactFmacFixup(a, instr, false, fixup);
            break;
        case UpperKind::Max:
            loadFs(0); loadSource(1); a.maxps(0, 1);
            break;
        case UpperKind::Min:
            loadFs(0); loadSource(1); a.minps(0, 1);
            break;
        case UpperKind::Madd:
            loadFs(1); loadSource(2); a.mulps(1, 2);
            a.loadState(0, accOffset()); a.addps(0, 1);
            emitExactFmacFixup(a, instr, true, fixup);
            break;
        case UpperKind::Msub:
            loadFs(1); loadSource(2); a.mulps(1, 2);
            a.loadState(0, accOffset()); a.subps(0, 1);
            emitExactFmacFixup(a, instr, true, fixup);
            break;
        case UpperKind::Itof:
            a.loadState(0, vfOffset(fs));
            a.cvtdq2ps(0, 0);
            if (op.scale != 0u)
            {
                a.loadConstant(1, &kItofScale[op.scale]);
                a.mulps(0, 1);
            }
            break;
        case UpperKind::Ftoi:
            loadFs(1);
            if (op.scale != 0u)
            {
                a.loadConstant(2, &kFtoiScale[op.scale]);
                a.mulps(1, 2);
            }
            a.cvttps2dq(0, 1);
            // cvttps2dq returns INT_MIN on overflow; positive overflow must saturate high.
            a.loadConstant(2, &kInt32Limit);
            a.cmpnltps(1, 2);
            a.xorps(0, 1);
            break;
        case UpperKind::Abs:
            loadFs(0);
            a.loadConstant(1, &kAbsMask);
            a.andps(0, 1);
            break;
        default:
            break;
        }
    }

    void emitStoreVf(Assembler &a, uint32_t reg, uint8_t dest, uint8_t xmm)
    {
        if (reg == 0u || dest == 0u)
            return;
        if (dest == 0xFu)
        {
            a.storeState(vfOffset(reg), xmm);
            return;
        }
        a.loadState(7, vfOffset(reg));
        a.blendps(7, xmm, blendMask(dest));
        a.storeState(vfOffset(reg), 7);
    }

    void emitStoreAcc(Assembler &a, uint8_t dest, uint8_t xmm)
    {
        if (dest == 0u)
            return;
        a.loadState(7, accOffset());
        a.blendps(7, xmm, blendMask(dest));
        a.storeState(accOffset(), 7);
    }

    void emitPostIncrement(Assembler &a, uint32_t reg, int32_t delta)
    {
        if (reg == 0u)
            return;
        a.loadViEax(viOffset(reg));
        a.addEaxImm(static_cast<uint32_t>(delta));
        a.movsxEaxAx();
        a.storeViEax(viOffset(reg));
    }

    void emitAddressFromVi(Assembler &a, uint32_t reg, uint32_t dataMask)
    {
        a.loadViEax(viOffset(reg));
        a.movzxEaxAx();
        a.shlEax4();
        a.andEaxImm(dataMask);
    }

    void emitLoadVf(Assembler &a, uint32_t reg, uint8_t dest, uint8_t suppressedReg)
    {
        if (reg == 0u || reg == suppressedReg)
            return;
        a.loadData(1);
        emitStoreVf(a, reg, dest, 1);
    }

    void emitStoreData(Assembler &a, uint32_t reg, uint8_t dest)
    {
        if (dest == 0u)
            return;
        a.loadState(1, vfOffset(reg));
        if (dest != 0xFu)
        {
            a.loadData(2);
            a.blendps(2, 1, blendMask(dest));
            a.storeData(2);
            return;
        }
        a.storeData(1);
    }

    void emitLower(Assembler &a, uint32_t instr, uint32_t dataMask, uint8_t suppressedReg)
    {
        if (instr == 0u || instr == 0x8000033Cu)
            return;
        const uint8_t opHi = (instr >> 25) & 0x7Fu;
        const uint8_t dest = (instr >> 21) & 0xFu;
        const uint8_t vit = VIT(instr), vis = VIS(instr), vid = VID(instr);
        const uint8_t vfT = FT(instr), vfS = FS(instr);
        switch (opHi)
        {
        case 0x00: // LQ
            a.loadViEax(viOffset(vis));
            a.addEaxImm(static_cast<uint32_t>(static_cast<int32_t>(IMM11(instr))));
            a.shlEax4();
            a.andEaxImm(dataMask);
            emitLoadVf(a, vfT, dest, suppressedReg);
            return;
        case 0x01: // SQ
            a.loadViEax(viOffset(vit));
            a.addEaxImm(static_cast<uint32_t>(static_cast<int32_t>(IMM11(instr))));
            a.shlEax4();
            a.andEaxImm(dataMask);
            emitStoreData(a, vfS, dest);
            return;
        case 0x08: // IADDIU
        case 0x09: // ISUBIU
        {
            if (vit == 0u)
                return;
            const int32_t imm = static_cast<int16_t>((instr & 0x7FFu) | ((instr >> 10) & 0x7800u));
            a.loadViEax(viOffset(vis));
            a.addEaxImm(static_cast<uint32_t>(opHi == 0x08u ? imm : -imm));
            a.movsxEaxAx();
            a.storeViEax(viOffset(vit));
            return;
        }
        default:
            break;
        }

        const uint8_t funct = instr & 0x3Fu;
        if (funct < 0x3Cu)
        {
            if (funct == 0x32u) // IADDI
            {
                if (vit == 0u)
                    return;
                const int32_t imm5 = static_cast<int32_t>(((instr >> 6) & 0x1Fu) << 27) >> 27;
                a.loadViEax(viOffset(vis));
                a.addEaxImm(static_cast<uint32_t>(imm5));
                a.movsxEaxAx();
                a.storeViEax(viOffset(vit));
                return;
            }
            if (vid == 0u)
                return;
            a.loadViEax(viOffset(vis));
            a.loadViEcx(viOffset(vit));
            switch (funct)
            {
            case 0x30: a.addEaxEcx(); a.movsxEaxAx(); break;
            case 0x31: a.subEaxEcx(); a.movsxEaxAx(); break;
            case 0x34: a.andEaxEcx(); break;
            default: a.orEaxEcx(); break;
            }
            a.storeViEax(viOffset(vid));
            return;
        }

        const uint8_t special = static_cast<uint8_t>((instr & 3u) | ((instr >> 4) & 0x7Cu));
        switch (special)
        {
        case 0x30: // MOVE
            if (vfT != 0u && vfT != suppressedReg)
            {
                a.loadState(1, vfOffset(vfS));
                emitStoreVf(a, vfT, dest, 1);
            }
            return;
        case 0x31: // MR32
            if (vfT != 0u && vfT != suppressedReg)
            {
                a.loadState(1, vfOffset(vfS));
                a.shufps(1, 1, 0x39);
                emitStoreVf(a, vfT, dest, 1);
            }
            return;
        case 0x34: // LQI
            emitAddressFromVi(a, vis, dataMask);
            emitLoadVf(a, vfT, dest, suppressedReg);
            emitPostIncrement(a, vis, 1);
            return;
        case 0x35: // SQI
            emitAddressFromVi(a, vit, dataMask);
            emitStoreData(a, vfS, dest);
            emitPostIncrement(a, vit, 1);
            return;
        case 0x36: // LQD
            emitPostIncrement(a, vis, -1);
            emitAddressFromVi(a, vis, dataMask);
            emitLoadVf(a, vfT, dest, suppressedReg);
            return;
        case 0x37: // SQD
            emitPostIncrement(a, vit, -1);
            emitAddressFromVi(a, vit, dataMask);
            emitStoreData(a, vfS, dest);
            return;
        case 0x3C: // MTIR
            if (vit != 0u)
            {
                a.loadViEax(vfOffset(vfS) + ((instr >> 21) & 3u) * 4u);
                a.movsxEaxAx();
                a.storeViEax(viOffset(vit));
            }
            return;
        default: // MFIR
            if (vfT != 0u && vfT != suppressedReg)
            {
                a.loadViEax(viOffset(vis));
                a.movsxEaxAx();
                a.movdXmmEax(1);
                a.shufps(1, 1, 0x00);
                emitStoreVf(a, vfT, dest, 1);
            }
            return;
        }
    }

    struct CodeArena
    {
        uint8_t *base = nullptr;
        uint32_t used = 0;

        uint8_t *place(const std::vector<uint8_t> &code)
        {
#if PS2_VU_JIT_SUPPORTED
            if (!base)
            {
                void *memory = mmap(nullptr, kCodeArenaSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (memory == MAP_FAILED)
                    return nullptr;
                base = static_cast<uint8_t *>(memory);
            }
            if (used + code.size() > kCodeArenaSize)
                return nullptr;
            uint8_t *destination = base + used;
            std::memcpy(destination, code.data(), code.size());
            used += static_cast<uint32_t>(code.size());
            return destination;
#else
            (void)code;
            return nullptr;
#endif
        }
    };
    CodeArena g_codeArena;
}

struct VU1Interpreter::JitBlock
{
    using Function = void (*)(VU1State *, uint8_t *, VU1Interpreter *);
    struct Timing
    {
        uint8_t kind; // 0 VF lane, 1 VI, 2 ACC lane
        uint8_t reg;
        uint8_t lane;
        uint16_t offset;
    };

    static constexpr uint16_t kNone = 0xFFFFu;
    Function function = nullptr;
    uint32_t pairs = 0;
    uint32_t cycles = 0;
    uint16_t fdivOffset = kNone, efuOffset = kNone; // issue cycle of the block's DIV-class / EFU op
    std::vector<Timing> reads;
    std::vector<Timing> writes;
};

struct VU1Interpreter::JitState
{
    std::array<JitBlock *, kMaxDecodedPairs> table{};
    std::unordered_map<uint64_t, JitBlock *> known;
    std::array<bool, kMaxDecodedPairs> nearFlagReader{};
    JitBlock noBlock;
    uint64_t generation = UINT64_MAX;
    uint32_t checkInterval = 0; // Check one block in this many; 0 disables.
    uint64_t seenBlocks = 0;
    uint64_t checkedBlocks = 0;
    uint64_t mismatches = 0;
    uint64_t cycleMismatches = 0;
    uint32_t pairsLeft = 0;
};

void VU1Interpreter::setJitEnabled(bool enabled)
{
#if PS2_VU_JIT_SUPPORTED
    const char *setting = std::getenv("PS2X_VU_JIT");
    if (setting != nullptr && setting[0] == '0')
        enabled = false;
    if (enabled && m_jit == nullptr)
    {
        m_jit = new JitState();
        const char *check = std::getenv("PS2X_VU_JIT_CHECK");
        m_jit->checkInterval = check != nullptr ? static_cast<uint32_t>(std::strtoul(check, nullptr, 10)) : 0u;
    }
    m_jitEnabled = enabled;
#else
    (void)enabled;
#endif
}

VU1Interpreter::JitBlock *VU1Interpreter::compileJitBlock(const uint8_t *vuCode, uint32_t codeSize,
                                                          PS2Memory *memory, uint32_t startPc, uint32_t dataSize)
{
    JitState &jit = *m_jit;
    const uint32_t dataMask = dataSize - 1u;

    const auto pairIsCompilable = [&](uint32_t pc, bool isFirst, const DecodedInstructionPair &pair)
    {
        if (jit.nearFlagReader[pc / 8u])
            return false;
        if (pair.eBit || pair.dBit || pair.tBit || pair.mBit ||
            pair.upperUsage.reserved || pair.lowerUsage.reserved)
            return false;
        const bool clipUpper = upperIsClip(pair.upper);
        const bool calloutLower = !pair.iBit && lowerIsCallout(pair.lower);
        if ((!clipUpper && (pair.upperUsage.readsClip || pair.upperUsage.writesClip)) ||
            (!calloutLower && (pair.lowerUsage.readsClip || pair.lowerUsage.writesClip)))
            return false;
        const UpperOp upper = decodeUpper(pair.upper);
        if (upper.kind == UpperKind::Unsupported && !clipUpper)
            return false;
        // Q changes on the interpreter's schedule, so a reader must start a block: run()
        // brings the pipelines up to date right before entering one.
        if (!isFirst && upperReadsQ(upper))
            return false;
        if (pair.iBit)
            return true;
        if ((!lowerIsSupported(pair.lower) && !calloutLower) || pair.lowerUsage.waitQ || pair.lowerUsage.waitP)
            return false;
        if (calloutLower && pair.lowerUsage.vfWrite.reg != 0u && pair.suppressedLowerVf == pair.lowerUsage.vfWrite.reg)
            return false;
        // The next branch would need the interpreter's record of the old VI value.
        if (pair.lowerUsage.delaysNextBranchRead && pair.lowerUsage.viWrite != 0u)
        {
            const uint32_t nextPc = pc + 8u;
            if (nextPc + 8u > codeSize)
                return false;
            if (getDecodedInstructionPairForPc(vuCode, codeSize, memory, nextPc).lowerUsage.pipeline == PipelineBranch)
                return false;
        }
        return true;
    };

    // Pair lookups below can reuse the shared uncached slot: copy what must survive.
    std::vector<DecodedInstructionPair> pairs;
    bool hasFdiv = false, hasEfu = false; // one of each per block: run() checks the unit is free
    for (uint32_t pc = startPc; pc + 8u <= codeSize && pairs.size() < kMaxBlockPairs; pc += 8u)
    {
        const DecodedInstructionPair pair = getDecodedInstructionPairForPc(vuCode, codeSize, memory, pc);
        if (!pairIsCompilable(pc, pairs.empty(), pair))
            break;
        const uint8_t pipeline = pair.iBit ? static_cast<uint8_t>(PipelineNone) : pair.lowerUsage.pipeline;
        if ((pipeline == PipelineFdiv && hasFdiv) || (pipeline == PipelineEfu && hasEfu))
            break;
        hasFdiv = hasFdiv || pipeline == PipelineFdiv;
        hasEfu = hasEfu || pipeline == PipelineEfu;
        pairs.push_back(pair);
    }

    // Callouts run with m_cycle at the block entry; the interpreter sees its own cycle.
    const auto upperCall = +[](VU1Interpreter *self, uint32_t instr, uint32_t offset)
    {
        const uint64_t entry = self->m_cycle;
        self->m_cycle = entry + offset;
        self->commitReadyPipelines();
        self->execUpper(instr);
        self->m_cycle = entry;
    };
    const auto lowerCall = +[](VU1Interpreter *self, uint32_t instr, uint32_t offset)
    {
        const uint64_t entry = self->m_cycle;
        self->m_cycle = entry + offset;
        self->commitReadyPipelines();
        self->execLower(instr, self->m_activeVuData, self->m_activeVuDataSize, *self->m_activeGs, self->m_activeMemory, 0u);
        // Stores land at the next cycle boundary; later pairs of the block read memory directly.
        self->m_cycle = entry + offset + 1u;
        self->commitReadyPipelines();
        self->m_cycle = entry;
    };
    if (pairs.size() < kMinBlockPairs)
        return &jit.noBlock;

    uint64_t hash = 0xCBF29CE484222325ull ^ (static_cast<uint64_t>(startPc) << 32) ^ dataSize;
    for (const DecodedInstructionPair &pair : pairs)
    {
        hash = (hash ^ pair.lower) * 0x100000001B3ull;
        hash = (hash ^ pair.upper) * 0x100000001B3ull;
    }
    hash ^= static_cast<uint64_t>(pairs.size()) << 48;
    if (const auto found = jit.known.find(hash); found != jit.known.end())
        return found->second;

    auto block = std::make_unique<JitBlock>();
    uint32_t vfReady[32][4]{};
    bool vfWritten[32][4]{};
    uint32_t viReady[16]{};
    bool viWritten[16]{};
    uint32_t accReady[4]{};
    bool accWritten[4]{};
    uint32_t cycle = 0u;

    // Stall from writes inside the block, as calculatePairReadyCycle would see them.
    const auto innerReady = [&](const InstructionUsage &usage, uint32_t ready)
    {
        for (uint32_t index = 0u; index < usage.vfReadCount; ++index)
        {
            const VfAccess &access = usage.vfRead[index];
            for (uint32_t lane = 0u; lane < 4u; ++lane)
                if ((access.lanes & laneForComponent(lane)) != 0u && vfWritten[access.reg][lane])
                    ready = std::max(ready, vfReady[access.reg][lane]);
        }
        for (uint32_t reg = 1u; reg < 16u; ++reg)
            if ((usage.viRead & (1u << reg)) != 0u && viWritten[reg])
                ready = std::max(ready, viReady[reg]);
        for (uint32_t lane = 0u; lane < 4u; ++lane)
            if ((usage.accRead & laneForComponent(lane)) != 0u && accWritten[lane])
                ready = std::max(ready, accReady[lane]);
        return ready;
    };
    // Values from outside the block are checked at entry, at this pair's final offset.
    const auto recordOuterReads = [&](const InstructionUsage &usage)
    {
        const uint16_t offset = static_cast<uint16_t>(cycle);
        for (uint32_t index = 0u; index < usage.vfReadCount; ++index)
        {
            const VfAccess &access = usage.vfRead[index];
            for (uint32_t lane = 0u; lane < 4u; ++lane)
                if ((access.lanes & laneForComponent(lane)) != 0u && !vfWritten[access.reg][lane])
                    block->reads.push_back({0u, access.reg, static_cast<uint8_t>(lane), offset});
        }
        for (uint32_t reg = 1u; reg < 16u; ++reg)
            if ((usage.viRead & (1u << reg)) != 0u && !viWritten[reg])
                block->reads.push_back({1u, static_cast<uint8_t>(reg), 0u, offset});
        for (uint32_t lane = 0u; lane < 4u; ++lane)
            if ((usage.accRead & laneForComponent(lane)) != 0u && !accWritten[lane])
                block->reads.push_back({2u, 0u, static_cast<uint8_t>(lane), offset});
    };
    const auto markVf = [&](const VfAccess &access, uint32_t latency)
    {
        for (uint32_t lane = 0u; lane < 4u; ++lane)
        {
            if ((access.lanes & laneForComponent(lane)) != 0u)
            {
                vfWritten[access.reg][lane] = true;
                vfReady[access.reg][lane] = cycle + latency;
            }
        }
    };

    Assembler a;
    loadJitConstants(a);
    for (const DecodedInstructionPair &pair : pairs)
    {
        cycle = innerReady(pair.lowerUsage, innerReady(pair.upperUsage, cycle));
        recordOuterReads(pair.upperUsage);
        recordOuterReads(pair.lowerUsage);

        const UpperOp upper = decodeUpper(pair.upper);
        const bool clipUpper = upperIsClip(pair.upper);
        if (clipUpper)
            emitCallout(a, reinterpret_cast<const void *>(upperCall), pair.upper, cycle);
        else
            emitUpper(a, pair.upper, upper, reinterpret_cast<const void *>(&VU1Interpreter::jitFmacFixup));
        if (pair.iBit)
        {
            uint32_t immediate = pair.lower;
            float value = 0.0f;
            std::memcpy(&value, &immediate, sizeof(value));
            value = normalizeOperand(value);
            std::memcpy(&immediate, &value, sizeof(immediate));
            a.storeImm32(iOffset(), immediate);
        }
        else if (lowerIsCallout(pair.lower))
        {
            emitCallout(a, reinterpret_cast<const void *>(lowerCall), pair.lower, cycle);
            if (pair.lowerUsage.pipeline == PipelineFdiv)
                block->fdivOffset = static_cast<uint16_t>(cycle);
            else if (pair.lowerUsage.pipeline == PipelineEfu)
                block->efuOffset = static_cast<uint16_t>(cycle);
        }
        else
        {
            emitLower(a, pair.lower, dataMask, pair.suppressedLowerVf);
        }
        if (!clipUpper && upper.kind != UpperKind::Nop)
        {
            if (upper.toAcc)
                emitStoreAcc(a, DEST(pair.upper), 0);
            else if (upper.kind == UpperKind::Itof || upper.kind == UpperKind::Ftoi || upper.kind == UpperKind::Abs)
                emitStoreVf(a, FT(pair.upper), DEST(pair.upper), 0);
            else
                emitStoreVf(a, FD(pair.upper), DEST(pair.upper), 0);
        }

        const VfAccess lowerWrite = pair.lowerUsage.vfWrite;
        if (!pair.iBit && lowerWrite.reg != 0u && pair.suppressedLowerVf != lowerWrite.reg)
            markVf(lowerWrite, pair.lowerUsage.vfLatency != 0u ? pair.lowerUsage.vfLatency : pair.lowerUsage.latency);
        if (pair.upperUsage.vfWrite.reg != 0u)
            markVf(pair.upperUsage.vfWrite, pair.upperUsage.vfLatency != 0u ? pair.upperUsage.vfLatency : pair.upperUsage.latency);
        if (!pair.iBit)
        {
            for (uint32_t reg = 1u; reg < 16u; ++reg)
            {
                if ((pair.lowerUsage.viWrite & (1u << reg)) != 0u)
                {
                    viWritten[reg] = true;
                    viReady[reg] = cycle + (pair.lowerUsage.viLatency != 0u ? pair.lowerUsage.viLatency : pair.lowerUsage.latency);
                }
            }
        }
        for (uint32_t lane = 0u; lane < 4u; ++lane)
        {
            if ((pair.upperUsage.accWrite & laneForComponent(lane)) != 0u)
            {
                accWritten[lane] = true;
                accReady[lane] = cycle + kAccForwardLatency;
            }
        }
        ++cycle;
    }
    a.ret();

    for (uint32_t reg = 1u; reg < 32u; ++reg)
        for (uint32_t lane = 0u; lane < 4u; ++lane)
            if (vfWritten[reg][lane])
                block->writes.push_back({0u, static_cast<uint8_t>(reg), static_cast<uint8_t>(lane), static_cast<uint16_t>(vfReady[reg][lane])});
    for (uint32_t reg = 1u; reg < 16u; ++reg)
        if (viWritten[reg])
            block->writes.push_back({1u, static_cast<uint8_t>(reg), 0u, static_cast<uint16_t>(viReady[reg])});
    for (uint32_t lane = 0u; lane < 4u; ++lane)
        if (accWritten[lane])
            block->writes.push_back({2u, 0u, static_cast<uint8_t>(lane), static_cast<uint16_t>(accReady[lane])});

    uint8_t *code = g_codeArena.place(a.bytes);
    if (code == nullptr)
        return &jit.noBlock;
    block->function = reinterpret_cast<JitBlock::Function>(code);
    block->pairs = static_cast<uint32_t>(pairs.size());
    block->cycles = cycle;
    JitBlock *result = block.release();
    jit.known.emplace(hash, result);
    return result;
}

void VU1Interpreter::prepareJitTable(const uint8_t *vuCode, uint32_t codeSize, PS2Memory *memory)
{
    JitState &jit = *m_jit;
    const uint64_t generation = memory != nullptr ? memory->getVU1CodeGeneration() : 0u;
    if (jit.generation == generation)
        return;
    jit.generation = generation;
    jit.table.fill(nullptr);
    jit.nearFlagReader.fill(false);
    const uint32_t count = std::min(codeSize / 8u, kMaxDecodedPairs);
    for (uint32_t index = 0u; index < count; ++index)
    {
        uint32_t lower = 0u, upper = 0u;
        std::memcpy(&lower, vuCode + index * 8u, 4u);
        std::memcpy(&upper, vuCode + index * 8u + 4u, 4u);
        const uint8_t opHi = (lower >> 25) & 0x7Fu;
        const bool readsArithmeticFlags = opHi == 0x14u || opHi == 0x16u || opHi == 0x17u ||
                                          opHi == 0x18u || opHi == 0x1Au || opHi == 0x1Bu;
        if (!readsArithmeticFlags || (upper & 0x80000000u) != 0u)
            continue;
        const uint32_t first = index > kFlagReaderWindow ? index - kFlagReaderWindow : 0u;
        for (uint32_t covered = first; covered <= index; ++covered)
            jit.nearFlagReader[covered] = true;
    }
}

bool VU1Interpreter::jitActiveFor(uint8_t *vuCode, PS2Memory *memory) const
{
    return m_jitEnabled && m_unit == Unit::VU1 && m_jit != nullptr && m_jit->pairsLeft == 0u &&
           memory != nullptr && vuCode == memory->getVU1Code();
}

bool VU1Interpreter::runJitBlock(uint8_t *vuCode, uint32_t codeSize, uint8_t *vuData, uint32_t dataSize,
                                 GS &gs, PS2Memory *memory, uint64_t budgetEnd)
{
    JitState &jit = *m_jit;
    const uint32_t pairIndex = m_state.pc / 8u;
    if ((m_state.pc & 7u) != 0u || pairIndex >= kMaxDecodedPairs)
        return false;
    prepareJitTable(vuCode, codeSize, memory);

    JitBlock *block = jit.table[pairIndex];
    if (block == nullptr)
    {
        block = compileJitBlock(vuCode, codeSize, memory, m_state.pc, dataSize);
        jit.table[pairIndex] = block;
    }
    if (block->function == nullptr)
    {
        return false;
    }

    uint64_t entry = m_cycle;
    for (const JitBlock::Timing &read : block->reads)
    {
        const uint64_t ready = read.kind == 0u   ? m_vfReady[read.reg][read.lane]
                               : read.kind == 1u ? m_viReady[read.reg]
                                                 : m_accReady[read.lane];
        if (ready > entry + read.offset)
            entry = ready - read.offset;
    }
    if (entry + block->cycles >= budgetEnd)
        return false;
    // A busy FDIV or EFU would stall the pair: leave that to the interpreter.
    if (block->fdivOffset != JitBlock::kNone && m_fdiv.valid && m_fdiv.readyCycle > entry + block->fdivOffset)
        return false;
    if (block->efuOffset != JitBlock::kNone && m_efuResourceReady > entry + block->efuOffset)
        return false;
    if (entry != m_cycle)
        advanceTo(entry);

    const auto applyBlockTiming = [&]
    {
        for (const JitBlock::Timing &write : block->writes)
        {
            const uint64_t ready = entry + write.offset;
            if (write.kind == 0u)
                m_vfReady[write.reg][write.lane] = ready;
            else if (write.kind == 1u)
                m_viReady[write.reg] = ready;
            else
                m_accReady[write.lane] = ready;
        }
    };
    const auto finishCycles = [&]
    {
        advanceTo(entry + block->cycles);
    };

    if (jit.checkInterval != 0u && jit.seenBlocks++ % jit.checkInterval == 0u)
    {
        const VU1State before = m_state;
        const uint64_t cycleBefore = m_cycle;
        const auto vfReadyBefore = m_vfReady;
        const auto viReadyBefore = m_viReady;
        const auto accReadyBefore = m_accReady;
        const auto flagsBefore = m_flagPipeline;
        const auto fdivBefore = m_fdiv;
        const auto efuBefore = m_efu;
        const auto storesBefore = m_storePipeline;
        const uint64_t efuReadyBefore = m_efuResourceReady;
        const uint64_t nextPipelineBefore = m_nextPipelineCycle;
        const uint32_t workingClipBefore = m_workingClip;
        std::vector<uint8_t> dataBefore(vuData, vuData + dataSize);

        block->function(&m_state, vuData, this);
        m_state.pc = before.pc + block->pairs * 8u;
        m_cycle = entry + block->cycles; // Q, P and CLIP as the interpreter will have them
        commitReadyPipelines();
        const VU1State jitState = m_state;
        std::vector<uint8_t> dataJit(vuData, vuData + dataSize);

        m_state = before;
        m_cycle = cycleBefore;
        m_vfReady = vfReadyBefore;
        m_viReady = viReadyBefore;
        m_accReady = accReadyBefore;
        m_flagPipeline = flagsBefore;
        m_fdiv = fdivBefore;
        m_efu = efuBefore;
        m_storePipeline = storesBefore;
        m_efuResourceReady = efuReadyBefore;
        m_nextPipelineCycle = nextPipelineBefore;
        m_workingClip = workingClipBefore;
        std::memcpy(vuData, dataBefore.data(), dataSize);

        jit.pairsLeft = block->pairs;
        run(vuCode, codeSize, vuData, dataSize, gs, memory, static_cast<uint32_t>(budgetEnd - m_cycle));
        jit.pairsLeft = 0u;

        ++jit.checkedBlocks;
        const bool sameVf = std::memcmp(jitState.vf, m_state.vf, sizeof(m_state.vf)) == 0;
        const bool sameVi = std::memcmp(jitState.vi, m_state.vi, sizeof(m_state.vi)) == 0;
        const bool sameAcc = std::memcmp(jitState.acc, m_state.acc, sizeof(m_state.acc)) == 0;
        const bool sameData = std::memcmp(dataJit.data(), vuData, dataSize) == 0;
        const bool sameQpc = std::memcmp(&jitState.q, &m_state.q, sizeof(float)) == 0 &&
                             std::memcmp(&jitState.p, &m_state.p, sizeof(float)) == 0 && jitState.clip == m_state.clip;
        const bool sameCycles = entry + block->cycles == m_cycle;
        jit.cycleMismatches += sameCycles ? 0u : 1u;
        if (!sameVf || !sameVi || !sameAcc || !sameQpc || !sameData || !sameCycles)
        {
            static std::vector<uint32_t> reportedPcs;
            ++jit.mismatches;
            if (reportedPcs.size() < 8u && std::find(reportedPcs.begin(), reportedPcs.end(), before.pc) == reportedPcs.end() &&
                (reportedPcs.push_back(before.pc), true))
            {
                std::fprintf(stderr, "[vu-jit] mismatch pc=0x%x pairs=%u vf=%d vi=%d acc=%d qpclip=%d data=%d cycles jit=%llu int=%llu\n",
                             before.pc, block->pairs, sameVf, sameVi, sameAcc, sameQpc, sameData,
                             static_cast<unsigned long long>(entry + block->cycles),
                             static_cast<unsigned long long>(m_cycle));
                for (uint32_t reg = 0u; reg < 32u; ++reg)
                {
                    if (std::memcmp(jitState.vf[reg], m_state.vf[reg], 16u) != 0)
                    {
                        uint32_t j[4], i[4];
                        std::memcpy(j, jitState.vf[reg], 16u);
                        std::memcpy(i, m_state.vf[reg], 16u);
                        std::fprintf(stderr, "[vu-jit]   vf%u jit=%08x,%08x,%08x,%08x int=%08x,%08x,%08x,%08x\n", reg,
                                     j[0], j[1], j[2], j[3], i[0], i[1], i[2], i[3]);
                    }
                }
                for (uint32_t pair = 0u; pair < block->pairs; ++pair)
                {
                    uint32_t lower = 0u, upper = 0u;
                    std::memcpy(&lower, vuCode + before.pc + pair * 8u, 4u);
                    std::memcpy(&upper, vuCode + before.pc + pair * 8u + 4u, 4u);
                    std::fprintf(stderr, "[vu-jit]   pair %u: upper=%08x lower=%08x\n", pair, upper, lower);
                }
                uint32_t accBits[4], qBits = 0u, iBits = 0u;
                std::memcpy(accBits, before.acc, 16u);
                std::memcpy(&qBits, &before.q, 4u);
                std::memcpy(&iBits, &before.i, 4u);
                std::fprintf(stderr, "[vu-jit]   in acc=%08x,%08x,%08x,%08x q=%08x i=%08x\n",
                             accBits[0], accBits[1], accBits[2], accBits[3], qBits, iBits);
                uint32_t beforeBits[32][4];
                std::memcpy(beforeBits, before.vf, sizeof(beforeBits));
                for (uint32_t reg = 1u; reg < 32u; ++reg)
                    std::fprintf(stderr, "[vu-jit]   in vf%u=%08x,%08x,%08x,%08x\n", reg,
                                 beforeBits[reg][0], beforeBits[reg][1], beforeBits[reg][2], beforeBits[reg][3]);
            }
        }
        if ((jit.checkedBlocks & 0xFFFFFu) == 0u)
            std::fprintf(stderr, "[vu-jit] checked=%llu mismatches=%llu cycle-only-or-more=%llu\n",
                         static_cast<unsigned long long>(jit.checkedBlocks),
                         static_cast<unsigned long long>(jit.mismatches),
                         static_cast<unsigned long long>(jit.cycleMismatches));
        return true;
    }

    block->function(&m_state, vuData, this);
    m_state.pc += block->pairs * 8u;
    applyBlockTiming();
    finishCycles();
    return true;
}

bool VU1Interpreter::jitCheckPairDone()
{
    return m_jit != nullptr && m_jit->pairsLeft != 0u && --m_jit->pairsLeft == 0u;
}

void VU1Interpreter::jitFmacFixup(VU1Interpreter *self, uint32_t instr, float *result)
{
    self->m_currentUpperInstruction = instr;
    uint8_t laneFlags[4]{};
    self->normalizeFmacResult(result, DEST(instr), laneFlags);
}
