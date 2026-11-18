#include <atomic>
#include <chrono>
#include <condition_variable>
#include <tuple>
#include <pthread.h>
#include <thread>
#include "gs_hw_backend.h"
#include "runtime/ps2_perf.h"

#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_common.h"
#include "runtime/gs/ps2_gs_memory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>


static GSHwBackend *g_activeHw = nullptr; // the renderer drawing the game (Active())

static inline uint64_t mix64(uint64_t x)
{
    x ^= x >> 33, x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33, x *= 0xC4CEB9FE1A85EC53ull;
    return x ^ (x >> 33);
}

// A 64-bit fingerprint of n bytes (n a multiple of 32): four independent lanes, then mixed.
static uint64_t hashBytes(const void *data, size_t n)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint64_t lane[4] = {0x9E3779B97F4A7C15ull, 0xBF58476D1CE4E5B9ull, 0x94D049BB133111EBull, n};
    for (size_t i = 0; i + 32u <= n; i += 32u)
        for (int k = 0; k < 4; ++k)
        {
            uint64_t w;
            std::memcpy(&w, p + i + size_t(k) * 8u, 8u);
            lane[k] = (lane[k] ^ w) * 0x9E3779B97F4A7C15ull;
        }
    return mix64(lane[0] ^ mix64(lane[1] ^ mix64(lane[2] ^ mix64(lane[3]))));
}

#if defined(PS2X_HAVE_VULKAN)
#include <vulkan/vulkan.h>
#endif

namespace
{
    constexpr uint32_t kVramBlocks = 16384u; // 4 MB / 256-byte blocks

    bool isPaletted(uint32_t psm) { return psm == GS_PSM_T8 || psm == GS_PSM_T8H || psm == GS_PSM_T4 || psm == GS_PSM_T4HL || psm == GS_PSM_T4HH; }
    bool isFourBit(uint32_t psm) { return psm == GS_PSM_T4 || psm == GS_PSM_T4HL || psm == GS_PSM_T4HH; }
    bool isDepthPsm(uint32_t psm) { return psm == GS_PSM_Z32 || psm == GS_PSM_Z24 || psm == GS_PSM_Z16 || psm == GS_PSM_Z16S; }
    // Z formats too: MKSM copies the depth buffer by drawing sprites with FRAME in Z24.
    bool isColorTargetPsm(uint32_t psm) { return psm == GS_PSM_CT32 || psm == GS_PSM_CT24 || psm == GS_PSM_Z32 || psm == GS_PSM_Z24; }

    // Page geometry (pixels) of a pixel storage mode.
    void pageSize(uint32_t psm, uint32_t &w, uint32_t &h)
    {
        switch (psm)
        {
        case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S: w = 64u; h = 64u; return;
        case GS_PSM_T8: w = 128u; h = 64u; return;
        case GS_PSM_T4: w = 128u; h = 128u; return;
        default: w = 64u; h = 32u; return; // 32-bit formats and the H variants (stored in CT32 pages)
        }
    }

    // Blocks [begin, end) an area of w x h pixels at block bp with buffer width bw touches.
    // shortcut: whole pages per row, as if the area started at x = 0; close enough to decide overlaps.
    void blockRange(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t w, uint32_t h, uint32_t &begin, uint32_t &end)
    {
        uint32_t pw = 0u, ph = 0u;
        pageSize(psm, pw, ph);
        const uint32_t widthPx = std::max(bw * 64u, w);
        const uint32_t pagesPerRow = std::max(1u, (widthPx + pw - 1u) / pw);
        const uint32_t rows = std::max(1u, (h + ph - 1u) / ph);
        begin = bp & (kVramBlocks - 1u);
        end = std::min(kVramBlocks, begin + pagesPerRow * rows * 32u);
    }

    bool overlaps(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a0 < b1 && b0 < a1; }

    // Block order inside a 32-bit page (8 x 4 blocks of 8 x 8 pixels), GS manual.
    constexpr uint8_t kBlockTable32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
                                             {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
    constexpr uint8_t kBlockTableZ32[4][8] = {{24, 25, 28, 29, 8, 9, 12, 13}, {26, 27, 30, 31, 10, 11, 14, 15},
                                              {16, 17, 20, 21, 0, 1, 4, 5}, {18, 19, 22, 23, 2, 3, 6, 7}};
    // Pixel origin of a block inside a 32-bit surface, false if the block is outside it.
    bool blockOrigin(uint32_t surfaceBlock, uint32_t fbw, uint32_t psm, uint32_t block, uint32_t &x, uint32_t &y)
    {
        if (block < surfaceBlock)
            return false;
        const uint32_t page = (block - surfaceBlock) / 32u, inPage = (block - surfaceBlock) % 32u;
        const bool z = psm == GS_PSM_Z32 || psm == GS_PSM_Z24;
        for (uint32_t by = 0; by < 4u; ++by)
            for (uint32_t bx = 0; bx < 8u; ++bx)
                if ((z ? kBlockTableZ32 : kBlockTable32)[by][bx] == inPage)
                {
                    x = (page % fbw) * 64u + bx * 8u;
                    y = (page / fbw) * 32u + by * 8u;
                    return true;
                }
        return false;
    }

    // The 256-byte blocks an area really touches (page ranges are too coarse: CLUTs and small
    // textures share pages, and every CLUT upload threw the neighbouring textures away).
    template <typename F>
    void forEachBlock(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, F &&visit)
    {
        uint32_t bwPx = 8, bhPx = 8, bitsPerPixel = 32; // block geometry of the format
        switch (psm)
        {
        case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S: bwPx = 16, bhPx = 8, bitsPerPixel = 16; break;
        case GS_PSM_T8: bwPx = 16, bhPx = 16, bitsPerPixel = 8; break;
        case GS_PSM_T4: bwPx = 32, bhPx = 16, bitsPerPixel = 4; break;
        default: break; // 32-bit layouts, including the H variants stored in CT32 blocks
        }
        const uint32_t storagePsm = (psm == GS_PSM_T8H || psm == GS_PSM_T4HL || psm == GS_PSM_T4HH) ? GS_PSM_CT32 : psm;
        for (uint32_t y = y0 - (y0 % bhPx); y < y0 + h; y += bhPx)
            for (uint32_t x = x0 - (x0 % bwPx); x < x0 + w; x += bwPx)
            {
                const uint64_t bits = uint64_t(GSMem::PixelAddress(storagePsm, bp, std::max(bw, 1u), x, y)) * bitsPerPixel;
                visit(static_cast<uint32_t>((bits / 2048u) & (kVramBlocks - 1u)));
            }
    }

    uint32_t applyTexa(const GSTexaReg &texa, uint32_t psm, uint32_t raw)
    {
        if (psm == GS_PSM_CT24 || psm == GS_PSM_Z24)
        {
            const uint32_t rgb = raw & 0x00FFFFFFu;
            const uint32_t alpha = (texa.aem && rgb == 0u) ? 0u : texa.ta0;
            return rgb | (alpha << 24);
        }
        return raw;
    }

    uint32_t expand5551(uint32_t v, const GSTexaReg &texa)
    {
        const uint32_t r = (v & 0x1Fu) << 3, g = ((v >> 5) & 0x1Fu) << 3, b = ((v >> 10) & 0x1Fu) << 3;
        const uint32_t rgb = r | (g << 8) | (b << 16);
        const uint32_t alpha = (v & 0x8000u) ? texa.ta1 : ((texa.aem && (v & 0x7FFFu) == 0u) ? 0u : texa.ta0);
        return rgb | (alpha << 24);
    }

    uint16_t floatToHalf(float f)
    {
        uint32_t x;
        std::memcpy(&x, &f, 4);
        const uint32_t sign = (x >> 16) & 0x8000u;
        int32_t exponent = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
        uint32_t mantissa = x & 0x7FFFFFu;
        if (exponent <= 0)
            return static_cast<uint16_t>(sign);
        if (exponent >= 31)
            return static_cast<uint16_t>(sign | 0x7C00u);
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | ((mantissa + 0x1000u) >> 13));
    }

    float halfToFloat(uint16_t h)
    {
        const uint32_t sign = (h & 0x8000u) << 16;
        const uint32_t exponent = (h >> 10) & 0x1Fu;
        const uint32_t mantissa = h & 0x3FFu;
        uint32_t x;
        if (exponent == 0u)
            x = sign; // denormals as zero: values here are byte/255 steps
        else if (exponent == 31u)
            x = sign | 0x7F800000u | (mantissa << 13);
        else
            x = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        float f;
        std::memcpy(&f, &x, 4);
        return f;
    }

    double depthRange(uint32_t zpsm)
    {
        if (zpsm == GS_PSM_Z32)
            return 4294967296.0;
        if (zpsm == GS_PSM_Z16 || zpsm == GS_PSM_Z16S)
            return 65536.0;
        return 16777216.0;
    }
}

// ---------------------------------------------------------------- count mode (F1)

void GSHwBackend::countBatch(const GSPrimitiveBatch &batch)
{
    const GSDrawState &state = batch.state;
    const GSContext &ctx = state.context;
    const uint32_t frameBlock = GSInternal::framePageBaseToBlock(ctx.frame.fbp);
    const uint32_t depthBlock = GSInternal::framePageBaseToBlock(ctx.zbuf.zbp);
    Key key;
    key.target = (uint64_t(frameBlock) << 32) | (uint64_t(ctx.frame.fbw) << 24) | (uint64_t(ctx.frame.psm) << 16) |
                 (uint64_t(depthBlock & 0x3FFFu) << 2) | (ctx.zbuf.zmask ? 1u : 0u);
    key.texture = state.prim.tme ? (((uint64_t(ctx.tex0.tbp0) << 40) | (uint64_t(ctx.tex0.psm) << 32) | (uint64_t(ctx.tex0.cbp) << 16) |
                                     (uint64_t(ctx.tex0.tw) << 12) | (uint64_t(ctx.tex0.th) << 8) | (uint64_t(ctx.tex0.tfx) << 2) |
                                     ctx.tex0.tcc) ^
                                    (ctx.clamp * 0x9E3779B97F4A7C15ull) ^ (uint64_t(state.linearFilter) << 63))
                                 : 0u;
    key.blend = state.prim.abe ? ctx.alpha | (uint64_t(state.pabe) << 63) : 0u;
    key.test = ctx.test & 0x7FFFFull;
    key.other = (uint64_t(state.prim.type == GS_PRIM_SPRITE) << 0) | (uint64_t(state.prim.tme) << 1) | (uint64_t(state.prim.fge) << 2) |
                (uint64_t(ctx.fba & 1u) << 3) | (uint64_t(ctx.frame.fbmsk) << 8);
    ++m_stats.primitives;
    if (state.prim.tme && (ctx.tex0.tbp0 == frameBlock || ctx.tex0.tbp0 == depthBlock))
        ++m_stats.feedback;
    if (!m_open)
        ++m_stats.batches, m_open = true;
    else if (key.target != m_last.target)
        ++m_stats.batches, ++m_stats.breaksTarget;
    else if (key.texture != m_last.texture)
        ++m_stats.batches, ++m_stats.breaksTexture;
    else if (key.blend != m_last.blend)
        ++m_stats.batches, ++m_stats.breaksBlend;
    else if (key.test != m_last.test)
        ++m_stats.batches, ++m_stats.breaksTest;
    else if (key.other != m_last.other)
        ++m_stats.batches, ++m_stats.breaksOther;
    m_last = key;
}


// ---------------------------------------------------------------- render mode (F2-F6)
#if defined(PS2X_HAVE_VULKAN)
namespace
{
    const uint32_t kHwVert[] = {
#include "gs_hw_vert.inc"
    };
    const uint32_t kHwFrag[] = {
#include "gs_hw_frag.inc"
    };

    struct HwVertex
    {
        float pos[4];
        float color[4];
        float tex[4];
    };

    struct HwPush
    {
        float targetSize[2];
        float texSize[2];
        uint32_t flags, tfxTcc, alphaTest, fogColor, wrap, clampU, clampV;
        float scale;
        float texOffset[2];
        float depthScale;
        uint32_t texa;
        uint32_t mip; // flag 32768: mxl | mmin << 3 | mmag << 6 | L << 7 | LCM << 9 | K (12 bits, 1/16) << 12
        uint32_t pad[3];
    };
    static_assert(sizeof(HwPush) == 80, "push constant layout must match gs_hw.vert/frag");

    constexpr uint32_t kSlots = 3u;
    constexpr VkDeviceSize kVertexBytes = 32ull << 20;
    constexpr VkDeviceSize kStagingBytes = 48ull << 20;
    constexpr VkFormat kColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
}

struct GSHwBackend::Renderer
{
    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
        VkDeviceSize size = 0;
    };
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint32_t w = 0, h = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };
    struct Slot
    {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Buffer vertices, staging;
        VkDeviceSize vertexUsed = 0, stagingUsed = 0;
        bool recording = false, pending = false;
        uint64_t serial = 0; // bumped at each submit
        std::vector<Image> garbage;
        std::vector<VkFramebuffer> garbageFramebuffers;
    };
    struct Surface
    {
        uint32_t block = 0, fbw = 0, psm = 0;
        bool depth = false;
        uint32_t w = 0, h = 0; // GS pixels
        uint32_t b0 = 0, b1 = 0;
        Image image, shadow;
        bool gpuNewer = false, mirrorNewer = true;
        uint64_t writeSeq = 0; // order of the last GPU write, for downloads of aliased memory
        // Area (GS pixels) the GPU drew since the mirror last matched: blocks outside it are
        // still current in the mirror even though the surface as a whole is newer.
        int dirtyX0 = 1 << 30, dirtyY0 = 1 << 30, dirtyX1 = -1, dirtyY1 = -1;
        void markDrawn(int x0, int y0, int x1, int y1)
        {
            dirtyX0 = std::min(dirtyX0, x0), dirtyY0 = std::min(dirtyY0, y0);
            dirtyX1 = std::max(dirtyX1, x1), dirtyY1 = std::max(dirtyY1, y1);
        }
        void markClean() { dirtyX0 = dirtyY0 = 1 << 30, dirtyX1 = dirtyY1 = -1; }
        // Feedback reads (bloom) sample the shadow: a copy of the whole image plus the exact
        // pixel rectangles drawn since. A read that misses them all needs no new copy, so a pass
        // of disjoint in-place sprites shares one copy (one per primitive was ~25 000/s).
        bool shadowFull = false;
        std::vector<std::array<int, 4>> drawnSinceCopy; // inclusive x0, y0, x1, y1
        std::vector<uint32_t> pending; // blocks a host transfer rewrote, refreshed from the mirror on use
        void noteDrawn(int x0, int y0, int x1, int y1)
        {
            if (!shadowFull)
                return;
            if (drawnSinceCopy.size() >= 1024u)
                shadowFull = false; // shortcut: too many to check; the next read copies again
            else
                drawnSinceCopy.push_back({x0, y0, x1, y1});
        }
        bool shadowServes(int x0, int y0, int x1, int y1) const
        {
            if (!shadowFull)
                return false;
            for (const auto &r : drawnSinceCopy)
                if (x0 <= r[2] && r[0] <= x1 && y0 <= r[3] && r[1] <= y1)
                    return false;
            return true;
        }
    };
    struct TexEntry
    {
        Image image;
        uint32_t b0 = 0, b1 = 0;
        uint64_t lastUse = 0;
        std::vector<uint16_t> blocks; // exact blocks the texels come from
        uint64_t content = 0;         // key into contentCache, which owns the image
    };
    struct ContentEntry
    {
        Image image;
        uint64_t lastUse = 0;
    };
    // Decoded textures by content (texel bytes + palette + shape): MKSM re-uploads each texture
    // into the same VRAM slot right before drawing it, so address-keyed entries die every draw.
    std::unordered_map<uint64_t, ContentEntry> contentCache;
    // Bounded in bytes, not only entries: partial reads of 1024x1024 textures change content
    // every frame (4 MB each), and 3000 of them filled the 4 GB card (driver crash in 50 s).
    static constexpr uint64_t kContentBudget = 512ull << 20, kRecycledBudget = 128ull << 20;
    uint64_t contentBytes = 0, recycledBytes = 0;
    static uint64_t textureBytes(const Image &img) { return uint64_t(img.w) * img.h * 4u; } // RGBA8
    struct BatchState
    {
        Surface *color = nullptr, *depth = nullptr;
        uint64_t pipeline = 0, pipeline2 = 0; // pipeline2: AFAIL pass (0 = none)
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSet set1 = VK_NULL_HANDLE; // map + palette (mapped reads), default otherwise
        HwPush push{};
        VkRect2D scissor{};
        float blendConstant = 0.0f;
        bool operator==(const BatchState &o) const
        {
            return color == o.color && depth == o.depth && pipeline == o.pipeline && pipeline2 == o.pipeline2 && set == o.set && set1 == o.set1 &&
                   std::memcmp(&push, &o.push, sizeof(push)) == 0 && std::memcmp(&scissor, &o.scissor, sizeof(scissor)) == 0 &&
                   blendConstant == o.blendConstant;
        }
    };

    GSHwBackend &owner;
    GSCpuBackend &cpu;
    uint8_t *vram = nullptr;
    uint32_t scale = 1;
    bool ok = false;
    bool innerDirty = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkPhysicalDeviceMemoryProperties memProps{};
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkRenderPass passColor = VK_NULL_HANDLE, passColorDepth = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    // Set 1: the map (texel -> target pixel and byte) and the palette of a mapped read.
    VkDescriptorSetLayout mapSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet defaultMapSet = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkPipeline> pipelines;
    Slot slots[kSlots];
    uint32_t slot = 0;
    Buffer readback;
    Image dummy;
    Image dummyMap, dummyPalette; // R16G16_UINT and RGBA8, 1x1
    std::map<std::tuple<VkImageView, VkImageView, VkImageView>, VkDescriptorSet> mapSets;
    struct MapEntry
    {
        Image image;
        bool complete = false;          // every texel lies inside the target
        uint32_t w = 0, h = 0;
        std::vector<uint32_t> outside;  // 2D prefix sum of texels outside the target ((w+1)*(h+1)), if not complete
        uint32_t outsideIn(int u0, int v0, int u1, int v1) const // inclusive rectangle, clamped
        {
            u0 = std::clamp(u0, 0, int(w) - 1), u1 = std::clamp(u1, 0, int(w) - 1);
            v0 = std::clamp(v0, 0, int(h) - 1), v1 = std::clamp(v1, 0, int(h) - 1);
            auto at = [&](int u, int v) { return outside[size_t(v) * (w + 1u) + size_t(u)]; };
            return at(u1 + 1, v1 + 1) - at(u0, v1 + 1) - at(u1 + 1, v0) + at(u0, v0);
        }
    };
    std::unordered_map<uint64_t, MapEntry> maps;          // by texture shape + target footprint
    std::unordered_map<uint64_t, std::vector<uint32_t>> targetPixels; // CT32 word -> x | y << 16
    std::unordered_map<uint64_t, Image> palettes;          // by palette content

    std::vector<std::unique_ptr<Surface>> surfaces;
    std::map<std::pair<Surface *, Surface *>, VkFramebuffer> framebuffers;
    std::unordered_map<uint64_t, TexEntry> textures;
    uint64_t useCounter = 0;
    // Why surfaces went back to the mirror (PS2X_GS_HW_STATS): each one waits for the GPU.
    enum Why
    {
        WhyOther,
        WhyTextureRead, // texture read through another layout (paletted read of a target)
        WhyDecode,      // texture decoded from the mirror over a GPU-written target
        WhyClut,        // CLUT loaded out of a GPU-written target
        WhyPresent,     // no display target matched: the window shows the mirror
        WhyTransfer,    // host or local transfer over a target
        WhyEvict,       // a target with another footprint took its memory
        WhyCount
    };
    int why = WhyOther;
    bool inlineUpload = false; // the open host-to-local transfer goes straight into the mirror
    uint64_t downloadsBy[WhyCount]{};
    uint64_t uploadsToSurface = 0;
    // Blocks a host transfer rewrote inside GPU surfaces, refreshed from the mirror once the
    // CPU backend applied the transfer (instead of a full download and re-upload per CLUT).
    Image blockStage; // scratch for refreshPending at scale > 1
    void refreshPending(Surface *only = nullptr); // only: that surface's blocks
    // Texture images are recycled by size: allocating and freeing device memory for every
    // re-uploaded texture (MKSM's menus stream textures each frame) cost most of the GS thread.
    struct Recycled
    {
        Image image;
        uint32_t slot = 0;
        uint64_t serial = 0; // the slot's serial when retired; free once that slot's next submit ended
    };
    std::vector<Recycled> recycled;
    void recycle(Image &img)
    {
        if (!img.image)
            return;
        cmd();
        recycled.push_back({img, slot, slots[slot].serial});
        recycledBytes += textureBytes(img);
        img = Image{};
        while (recycled.size() > 512u || recycledBytes > kRecycledBudget) // keep the pool bounded
        {
            recycledBytes -= textureBytes(recycled.front().image);
            retire(recycled.front().image);
            recycled.erase(recycled.begin());
        }
    }
    bool gpuDone(const Recycled &r) const
    {
        const Slot &sl = slots[r.slot];
        if (sl.serial > r.serial + 1u)
            return true;
        if (sl.serial == r.serial + 1u)
            return vkGetFenceStatus(device, sl.fence) == VK_SUCCESS;
        return false; // not submitted yet
    }
    Image textureImage(uint32_t w, uint32_t h)
    {
        for (size_t i = 0; i < recycled.size(); ++i)
            if (recycled[i].image.w == w && recycled[i].image.h == h && gpuDone(recycled[i]))
            {
                Image img = recycled[i].image;
                recycled.erase(recycled.begin() + static_cast<std::ptrdiff_t>(i));
                recycledBytes -= textureBytes(img);
                return img;
            }
        return createImage(w, h, VK_FORMAT_R8G8B8A8_UNORM, 0);
    }
    uint64_t writeCounter = 0;
    // Blocks a host transfer queued on the CPU backend may still be writing: reading them from
    // the mirror needs syncInner, anything else does not (each sync is a thread round trip).
    std::vector<uint8_t> pendingWrite = std::vector<uint8_t>(kVramBlocks, 0u);
    std::vector<uint32_t> pendingWriteList;
    bool pendingUnknown = false; // a write not mapped to blocks: sync before any read
    void notePendingWrite(uint32_t blk)
    {
        if (!pendingWrite[blk])
            pendingWrite[blk] = 1u, pendingWriteList.push_back(blk);
    }
    // Whether the mirror holds the current data of a block inside a GPU surface.
    bool blockCurrent(const Surface &sf, uint32_t blk) const
    {
        if (!sf.gpuNewer || blk < sf.b0 || blk >= sf.b1)
            return true;
        uint32_t x = 0, y = 0;
        if (sf.dirtyX1 < 0)
            return true;
        if (blockOrigin(sf.block, sf.fbw, sf.psm, blk, x, y) &&
            (int(x) + 8 <= sf.dirtyX0 || int(x) > sf.dirtyX1 || int(y) + 8 <= sf.dirtyY0 || int(y) > sf.dirtyY1))
            return true; // outside what the GPU drew
        return blockTransferSeq[blk] > sf.writeSeq; // uploaded after the last GPU write
    }
    void syncIfPending(uint32_t b0, uint32_t b1)
    {
        if (!innerDirty)
            return;
        bool needed = pendingUnknown;
        for (uint32_t blk : pendingWriteList)
            if (blk >= b0 && blk < b1)
            {
                needed = true;
                break;
            }
        if (needed)
            syncInner();
    }
    // writeCounter value when a host transfer last wrote each block: newer than a surface's
    // writeSeq means the mirror holds that block's current data.
    std::vector<uint64_t> blockTransferSeq = std::vector<uint64_t>(kVramBlocks, 0u);

    // Open render pass and batch.
    Surface *passColorSurface = nullptr, *passDepthSurface = nullptr;
    bool inPass = false;
    BatchState batch{};
    struct BoundState // what flushBatch last set in the open command buffer
    {
        bool valid = false;
        uint32_t viewportW = 0, viewportH = 0;
        VkRect2D scissor{};
        float blendConstant = 0.0f;
        VkDescriptorSet sets[2]{};
        uint64_t pipeline = 0;
        HwPush push{};
    };
    BoundState bound{};
    VkDeviceSize batchFirst = 0;
    uint32_t batchCount = 0;

    // CLUT as the GS keeps it (512 halfwords), fed from the mirror.
    std::array<uint16_t, 512> clut{};
    uint64_t clutSerial = 1; // bumped when clut changes
    uint64_t cpuClutSerial = 0; // the clut the CPU backend last got (fallback)
    uint32_t cpuClutCbp[2]{~0u, ~0u};
    // clut converted for the last ways it was read (resolveTextureSlow; MKSM alternates CSA)
    uint64_t clutHash = 0;   // content of clut[]: keys the converted palettes
    // By content: MKSM switches CLUTs ~2500 times a frame, mostly among a few. Keyed by the
    // serial (which only grows), every switch back was a new conversion of 256 entries.
    struct Palette
    {
        std::array<uint32_t, 256> colors{};
        uint64_t clut = ~0ull, how = ~0ull, hash = 0; // clut: the clutHash it was converted from
    };
    std::array<Palette, 64> palettesRead{};
    // Recent CLUT loads by their source bytes: a repeat applies the stored entries instead of
    // reading and reordering the texels again.
    struct ClutLoad
    {
        uint64_t key = ~0ull, raw = 0;
        std::array<uint16_t, 256> lo{}, hi{}; // by entry, in CLUT order
    };
    std::vector<ClutLoad> clutLoads = std::vector<ClutLoad>(256);
    uint32_t clutCbp[2]{~0u, ~0u};
    // Last texture resolved, reused while nothing it depends on changed (most primitives).
    uint64_t generation = 1;   // bumped when a surface or texture changes (the CLUT has clutSerial)
    uint64_t lastTexGeneration = 0, lastTexClutSerial = 0;
    uint64_t lastTexTex1 = 0, lastTexMiptbp1 = 0, lastTexMiptbp2 = 0;
    GSTex0Reg lastTex0{};
    GSTexaReg lastTexa{};
    uint32_t lastTexW = 0, lastTexH = 0;
    BatchState lastTexState{};
    // Last draw state: identical state (most primitives) goes straight to the vertex copy.
    GSDrawState lastDrawState{};
    uint64_t lastDrawGeneration = 0, lastDrawClutSerial = 0;
    uint8_t lastDrawCount = 0;

    Renderer(GSHwBackend &o, GSCpuBackend &c) : owner(o), cpu(c) {}

    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags preferred = 0) const
    {
        int chosen = -1;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            const VkMemoryPropertyFlags flags = memProps.memoryTypes[i].propertyFlags;
            if (!(bits & (1u << i)) || (flags & wanted) != wanted)
                continue;
            if (chosen < 0 || (preferred && (flags & preferred) == preferred))
                chosen = static_cast<int>(i);
        }
        return chosen < 0 ? 0u : static_cast<uint32_t>(chosen);
    }

    bool createBuffer(Buffer &out, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags preferred)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        if (vkCreateBuffer(device, &info, nullptr, &out.buffer) != VK_SUCCESS)
            return false;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, out.buffer, &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, preferred);
        if (vkAllocateMemory(device, &alloc, nullptr, &out.memory) != VK_SUCCESS)
            return false;
        vkBindBufferMemory(device, out.buffer, out.memory, 0);
        out.size = size;
        return vkMapMemory(device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped) == VK_SUCCESS;
    }

    Image createImage(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage)
    {
        Image img;
        img.w = w, img.h = h, img.format = format;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {w, h, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(device, &info, nullptr, &img.image);
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, img.image, &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &alloc, nullptr, &img.memory);
        vkBindImageMemory(device, img.image, img.memory, 0);
        const bool depthFormat = format == kDepthFormat;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = img.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {static_cast<VkImageAspectFlags>(depthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
        vkCreateImageView(device, &view, nullptr, &img.view);
        VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = pool;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &setLayout;
        if (vkAllocateDescriptorSets(device, &setInfo, &img.set) == VK_SUCCESS)
        {
            VkDescriptorImageInfo imageInfo{sampler, img.view, VK_IMAGE_LAYOUT_GENERAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = img.set;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        // Everything lives in GENERAL: render target, sampled and copied without layout juggling.
        VkCommandBuffer c = cmd();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = img.image;
        barrier.subresourceRange = view.subresourceRange;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        endPass();
        gpuKind(GkOther);
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (!depthFormat)
        {
            VkClearColorValue zero{};
            vkCmdClearColorImage(c, img.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &view.subresourceRange);
        }
        else
        {
            VkClearDepthStencilValue zero{};
            vkCmdClearDepthStencilImage(c, img.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &view.subresourceRange);
        }
        fullBarrier();
        return img;
    }

    // Frees once the GPU is done: the slot must be recording, otherwise its begin would free
    // the image right away while the previous submission (that used it) still runs (Xid 31).
    void retire(Image &img)
    {
        if (img.image)
        {
            cmd();
            slots[slot].garbage.push_back(img);
        }
        img = Image{};
    }

    void freeImage(Image &img)
    {
        if (img.view)
            for (auto it = mapSets.begin(); it != mapSets.end();)
                if (std::get<0>(it->first) == img.view || std::get<1>(it->first) == img.view || std::get<2>(it->first) == img.view)
                {
                    vkFreeDescriptorSets(device, pool, 1, &it->second);
                    it = mapSets.erase(it);
                }
                else
                    ++it;
        if (img.set)
            vkFreeDescriptorSets(device, pool, 1, &img.set);
        if (img.view)
            vkDestroyImageView(device, img.view, nullptr);
        if (img.image)
            vkDestroyImage(device, img.image, nullptr);
        if (img.memory)
            vkFreeMemory(device, img.memory, nullptr);
        img = Image{};
    }

    // GPU time by kind of work (PS2X_PERF): a timestamp at each change of kind, read when the
    // slot is reused (its fence is done), summed and printed every 2 s.
    enum GpuKind : uint8_t
    {
        GkDraw,
        GkShadow,
        GkUpload,
        GkDownload,
        GkPresent,
        GkOther,
        GkStart
    };
    static constexpr uint32_t kStampsPerSlot = 1024u;
    VkQueryPool stampPool = VK_NULL_HANDLE;
    double stampNs = 1.0;
    uint32_t stampsUsed[kSlots]{};
    uint8_t stampKinds[kSlots][kStampsPerSlot]{};
    int gpuKindNow = -1;
    double gpuNs[GkStart]{}, gpuBusyNs = 0.0;
    uint64_t gpuStampsLost = 0;
    int64_t gpuReportMs = 0;
    void gpuStamp(int kind) // ends the current segment, of `kind`
    {
        uint32_t &n = stampsUsed[slot];
        if (n >= kStampsPerSlot)
        {
            ++gpuStampsLost;
            return;
        }
        vkCmdWriteTimestamp(slots[slot].cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, stampPool, slot * kStampsPerSlot + n);
        stampKinds[slot][n++] = static_cast<uint8_t>(kind);
    }
    void gpuKind(int kind)
    {
        if (!stampPool || kind == gpuKindNow)
            return;
        cmd();
        if (gpuKindNow >= 0)
            gpuStamp(gpuKindNow);
        gpuKindNow = kind;
    }
    void gpuCollect(uint32_t index);

    VkCommandBuffer cmd()
    {
        Slot &s = slots[slot];
        if (!s.recording)
        {
            if (s.pending)
            {
                vkWaitForFences(device, 1, &s.fence, VK_TRUE, UINT64_MAX);
                s.pending = false;
            }
            if (stampPool)
                gpuCollect(slot);
            vkResetFences(device, 1, &s.fence);
            for (Image &img : s.garbage)
                freeImage(img);
            s.garbage.clear();
            for (VkFramebuffer fb : s.garbageFramebuffers)
                vkDestroyFramebuffer(device, fb, nullptr);
            s.garbageFramebuffers.clear();
            s.vertexUsed = s.stagingUsed = 0;
            batchFirst = 0, batchCount = 0;
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(s.cmd, &begin);
            s.recording = true;
            bound = BoundState{};
            if (stampPool)
            {
                vkCmdResetQueryPool(s.cmd, stampPool, slot * kStampsPerSlot, kStampsPerSlot);
                gpuKindNow = -1;
                gpuStamp(GkStart);
            }
        }
        return s.cmd;
    }

    void fullBarrier()
    {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    void endPass()
    {
        if (!inPass)
            return;
        vkCmdEndRenderPass(slots[slot].cmd);
        inPass = false;
        passColorSurface = passDepthSurface = nullptr;
        fullBarrier();
    }

    // Ends the command buffer and hands it to the GPU; wait = block until it finished.
    void submit(bool wait)
    {
        flushBatch();
        Slot &s = slots[slot];
        if (s.recording)
        {
            endPass();
            if (gpuKindNow >= 0)
                gpuStamp(gpuKindNow), gpuKindNow = -1;
            vkEndCommandBuffer(s.cmd);
            VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            info.commandBufferCount = 1;
            info.pCommandBuffers = &s.cmd;
            vkQueueSubmit(queue, 1, &info, s.fence);
            ++s.serial;
            s.recording = false;
            s.pending = true;
            if (wait)
            {
                vkWaitForFences(device, 1, &s.fence, VK_TRUE, UINT64_MAX);
                s.pending = false;
            }
            slot = (slot + 1u) % kSlots;
        }
        else if (wait)
            vkDeviceWaitIdle(device);
    }

    // Space in the current slot's staging buffer (submits and starts another slot when full).
    uint8_t *stage(VkDeviceSize bytes, VkDeviceSize &offset)
    {
        if (bytes > kStagingBytes)
            return nullptr;
        cmd();
        if (slots[slot].stagingUsed + bytes > kStagingBytes)
        {
            submit(false);
            cmd();
        }
        Slot &s = slots[slot];
        offset = s.stagingUsed;
        s.stagingUsed = (s.stagingUsed + bytes + 255u) & ~VkDeviceSize(255u);
        return static_cast<uint8_t *>(s.staging.mapped) + offset;
    }

    // Copies of finished fields for the window. The GS side records one when MKSM switches frame
    // pages (the field is complete, like the CPU backend's capture) or, in scenes that do not
    // flip, when the window asks; the window takes the newest copy the GPU finished, without the
    // renderer lock and without waiting. (It took the lock and waited on the copy's fence, and
    // copied half-drawn fields: 12-48 fps and flicker at 2x.)
    struct PresentCopy
    {
        Image image;   // RGBA8 at the surface scale
        Buffer buffer; // host copy of it
        uint32_t w = 0, h = 0, slot = 0;
        uint64_t serial = 0, seq = 0;
        bool doubleRows = false;
        uint32_t fbp = 0;
        uint64_t recordedNs = 0; // PS2X_PERF: when the game finished this field
    };
    // Up to kSlots copies can be in flight (one submit each), one is read: two more keep a
    // finished one when the GPU lags (with three, a GPU two fields behind showed nothing new).
    static constexpr int kPresentCopies = int(kSlots) + 2;
    PresentCopy presentCopies[kPresentCopies];
    std::mutex presentMutex;      // guards the indices, seq, presentGeom and presentFailed
    int published = -1, reading = -1;
    int backlogTicks = 0; // window frames in a row with two finished copies waiting (takeNewest)
    uint64_t publishedSeq = 0;
    GSPresentationRequest presentGeom{}; // the window's last request (display geometry)
    bool hasPresentGeom = false, presentFailed = false;
    std::atomic<int64_t> lastFieldCaptureMs{0};
    uint32_t lastMainFbp = ~0u; // GS side
    std::atomic<std::thread::id> presentThread{};
    uint64_t shownSeq = 0; // window side
    PresentationFrame shown;
    bool record(const GSPresentationRequest &request, uint32_t sourceFbp);
    void noteDraw(uint32_t fbp);
    bool takeNewest(PresentationFrame &out);
    bool copyDone(const PresentCopy &copy) const;
    void convertPresent(const PresentCopy &ready, PresentationFrame &out) const;

    bool init();
    void destroy();
    VkPipeline pipeline(uint64_t key);
    VkFramebuffer framebuffer(Surface *color, Surface *depth);
    void beginPass(Surface *color, Surface *depth);
    void flushBatch();
    void syncInner();
    Surface *surface(uint32_t block, uint32_t fbw, uint32_t psm, bool depth, uint32_t height);
    void destroySurface(Surface *s);
    void upload(Surface &s);
    void download(const std::vector<Surface *> &list);
    void downloadRange(uint32_t b0, uint32_t b1);
    bool downloadBlocks(Surface &s, const std::vector<uint32_t> &blocks);
    void downloadAll();
    void mirrorWrote(uint32_t b0, uint32_t b1);
    void invalidateTextures(uint32_t b0, uint32_t b1);
    void invalidateTexturesExact(const std::vector<uint8_t> &written, uint32_t b0, uint32_t b1);
    void loadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut);
    uint32_t clutColor(const GSDrawState &state, uint32_t index) const;
    const Palette &paletteFor(const GSDrawState &state);
    bool resolveTexture(const GSPrimitiveBatch &b, Surface *target, BatchState &out);
    bool resolveTextureSlow(const GSPrimitiveBatch &b, Surface *target, BatchState &out);
    VkDescriptorSet mapSet(VkImageView map, VkImageView palette, VkImageView dst = VK_NULL_HANDLE);
    Surface *dateSurface = nullptr; // the open batch tests destination alpha against its shadow
    bool dateServed(const GSPrimitiveBatch &b) const;
    bool mappedRead(const GSPrimitiveBatch &b, Surface *s, Surface *target, BatchState &out);
    bool feedbackLast = false;
    bool feedbackRead = false;              // the resolved texture is a surface's shadow
    bool partialRead = false;               // decoded only the region this primitive samples
    Surface *feedbackSurface = nullptr;     // the open batch samples this surface's shadow
    float feedbackOffset[2] = {0.0f, 0.0f}; // texture origin inside it (GS pixels)
    bool feedbackServed(const GSPrimitiveBatch &b) const;
    void refreshShadow(Surface &s);
    bool draw(const GSPrimitiveBatch &b);
    bool drawState(const GSPrimitiveBatch &b);
    void appendVertices(const GSPrimitiveBatch &b);
    void fallback(const GSPrimitiveBatch &b);
};

bool ps2xVulkanLoaderUsable(); // gs_gpu_accelerator.cpp: no Windows, vulkan-1.dll presente e 1.1+

bool GSHwBackend::Renderer::init()
{
    if (!ps2xVulkanLoaderUsable())
        return false;
    if (const char *s = std::getenv("PS2X_SCALE"))
        scale = static_cast<uint32_t>(std::clamp(std::atoi(s), 1, 4));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ps2x-gs-hw";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
        return false;
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    const char *wanted = std::getenv("PS2X_GPU_DEVICE");
    for (VkPhysicalDevice d : devices)
    {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        const bool named = wanted && std::strstr(p.deviceName, wanted);
        if (named || (!wanted && (!physical || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)))
            physical = d;
        if (named)
            break;
    }
    if (!physical)
        return false;
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical, &features);
    if (!features.dualSrcBlend)
        return false;
    vkGetPhysicalDeviceMemoryProperties(physical, &memProps);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    family = ~0u;
    for (uint32_t i = 0; i < count && family == ~0u; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            family = i;
    if (family == ~0u)
        return false;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkPhysicalDeviceFeatures enabled{};
    enabled.dualSrcBlend = VK_TRUE;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.pEnabledFeatures = &enabled;
    if (vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS)
        return false;
    vkGetDeviceQueue(device, family, 0, &queue);
    if (std::getenv("PS2X_PERF") && families[family].timestampValidBits != 0u)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physical, &props);
        stampNs = props.limits.timestampPeriod;
        VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queries.queryCount = kSlots * kStampsPerSlot;
        vkCreateQueryPool(device, &queries, nullptr, &stampPool);
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family;
    vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool);
    for (Slot &s : slots)
    {
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = cmdPool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &alloc, &s.cmd);
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkCreateFence(device, &fence, nullptr, &s.fence);
        if (!createBuffer(s.vertices, kVertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0) ||
            !createBuffer(s.staging, kStagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0))
            return false;
    }
    if (!createBuffer(readback, 2048ull * 2048ull * 8ull, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
        return false;

    // Render passes: load and store, everything in GENERAL.
    for (int withDepth = 0; withDepth < 2; ++withDepth)
    {
        VkAttachmentDescription attachments[2]{};
        for (int i = 0; i < 2; ++i)
        {
            attachments[i].format = i ? kDepthFormat : kColorFormat;
            attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        }
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_GENERAL}, depthRef{1, VK_IMAGE_LAYOUT_GENERAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colorRef;
        sub.pDepthStencilAttachment = withDepth ? &depthRef : nullptr;
        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        for (VkSubpassDependency &d : deps)
        {
            d.srcStageMask = d.dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            d.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            d.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        }
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = withDepth ? 2u : 1u;
        rp.pAttachments = attachments;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 2;
        rp.pDependencies = deps;
        vkCreateRenderPass(device, &rp, nullptr, withDepth ? &passColorDepth : &passColor);
    }

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout);
    VkDescriptorSetLayoutBinding mapBindings[3] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                   {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                   {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo mapSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    mapSetInfo.bindingCount = 3;
    mapSetInfo.pBindings = mapBindings;
    vkCreateDescriptorSetLayout(device, &mapSetInfo, nullptr, &mapSetLayout);
    const VkDescriptorSetLayout setLayouts[2] = {setLayout, mapSetLayout};
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(HwPush)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize = sizeof(kHwVert);
    module.pCode = kHwVert;
    vkCreateShaderModule(device, &module, nullptr, &vert);
    module.codeSize = sizeof(kHwFrag);
    module.pCode = kHwFrag;
    vkCreateShaderModule(device, &module, nullptr, &frag);
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192};
    VkDescriptorPoolCreateInfo descPool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descPool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descPool.maxSets = 8192;
    descPool.poolSizeCount = 1;
    descPool.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(device, &descPool, nullptr, &pool);
    dummy = createImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 0);
    dummyMap = createImage(1, 1, VK_FORMAT_R16G16_UINT, 0);
    dummyPalette = createImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 0);
    defaultMapSet = mapSet(dummyMap.view, dummyPalette.view);
    submit(true);
    ok = true;
    return true;
}

void GSHwBackend::Renderer::destroy()
{
    if (!device)
        return;
    vkDeviceWaitIdle(device);
    if (stampPool)
        vkDestroyQueryPool(device, stampPool, nullptr);
    for (auto &s : surfaces)
        freeImage(s->image), freeImage(s->shadow);
    surfaces.clear();
    textures.clear();
    for (auto &c : contentCache)
        freeImage(c.second.image);
    contentCache.clear();
    for (Recycled &r : recycled)
        freeImage(r.image);
    recycled.clear();
    for (auto &fb : framebuffers)
        vkDestroyFramebuffer(device, fb.second, nullptr);
    framebuffers.clear();
    for (Slot &s : slots)
    {
        for (Image &img : s.garbage)
            freeImage(img);
        for (VkFramebuffer fb : s.garbageFramebuffers)
            vkDestroyFramebuffer(device, fb, nullptr);
        vkDestroyFence(device, s.fence, nullptr);
        for (Buffer *b : {&s.vertices, &s.staging})
            vkDestroyBuffer(device, b->buffer, nullptr), vkFreeMemory(device, b->memory, nullptr);
    }
    vkDestroyBuffer(device, readback.buffer, nullptr);
    vkFreeMemory(device, readback.memory, nullptr);
    freeImage(dummy);
    for (auto &m : maps)
        freeImage(m.second.image);
    maps.clear();
    for (auto &pl : palettes)
        freeImage(pl.second);
    palettes.clear();
    freeImage(dummyMap);
    freeImage(dummyPalette);
    freeImage(blockStage);
    for (PresentCopy &pc : presentCopies)
    {
        freeImage(pc.image);
        if (pc.buffer.buffer)
            vkDestroyBuffer(device, pc.buffer.buffer, nullptr), vkFreeMemory(device, pc.buffer.memory, nullptr);
    }
    for (auto &p : pipelines)
        vkDestroyPipeline(device, p.second, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroySampler(device, sampler, nullptr);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, mapSetLayout, nullptr);
    vkDestroyRenderPass(device, passColor, nullptr);
    vkDestroyRenderPass(device, passColorDepth, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE;
}

// Pipeline key: blend enable 1 | src 5 | dst 5 | op 3 | write mask 4 | depth op 3 | depth write 1 | has depth 1.
namespace
{
    struct PipeKey
    {
        uint32_t blend = 0, src = 0, dst = 0, op = 0, mask = 0xF, depthOp = VK_COMPARE_OP_ALWAYS, depthWrite = 0, hasDepth = 0;
        uint64_t pack() const
        {
            return uint64_t(blend) | (uint64_t(src) << 1) | (uint64_t(dst) << 6) | (uint64_t(op) << 11) | (uint64_t(mask) << 14) |
                   (uint64_t(depthOp) << 18) | (uint64_t(depthWrite) << 21) | (uint64_t(hasDepth) << 22) | (1ull << 40);
        }
        static PipeKey unpack(uint64_t k)
        {
            PipeKey p;
            p.blend = k & 1u, p.src = (k >> 1) & 31u, p.dst = (k >> 6) & 31u, p.op = (k >> 11) & 7u, p.mask = (k >> 14) & 15u;
            p.depthOp = (k >> 18) & 7u, p.depthWrite = (k >> 21) & 1u, p.hasDepth = (k >> 22) & 1u;
            return p;
        }
    };
}

VkPipeline GSHwBackend::Renderer::pipeline(uint64_t key)
{
    if (auto it = pipelines.find(key); it != pipelines.end())
        return it->second;
    const PipeKey k = PipeKey::unpack(key);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};
    VkVertexInputBindingDescription bindingDesc{0, sizeof(HwVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                                                  {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
                                                  {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bindingDesc;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = k.hasDepth ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = (k.hasDepth && k.depthWrite) ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = static_cast<VkCompareOp>(k.depthOp);
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = k.blend ? VK_TRUE : VK_FALSE;
    att.srcColorBlendFactor = static_cast<VkBlendFactor>(k.src);
    att.dstColorBlendFactor = static_cast<VkBlendFactor>(k.dst);
    att.colorBlendOp = static_cast<VkBlendOp>(k.op);
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // the GS writes As, it does not blend alpha
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    att.colorWriteMask = k.mask;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    VkDynamicState dyn[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 3;
    dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dy;
    info.layout = layout;
    info.renderPass = k.hasDepth ? passColorDepth : passColor;
    VkPipeline p = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p);
    pipelines.emplace(key, p);
    return p;
}

VkFramebuffer GSHwBackend::Renderer::framebuffer(Surface *color, Surface *depth)
{
    const auto key = std::make_pair(color, depth);
    if (auto it = framebuffers.find(key); it != framebuffers.end())
        return it->second;
    VkImageView views[2] = {color->image.view, depth ? depth->image.view : VK_NULL_HANDLE};
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = depth ? passColorDepth : passColor;
    info.attachmentCount = depth ? 2u : 1u;
    info.pAttachments = views;
    info.width = depth ? std::min(color->image.w, depth->image.w) : color->image.w;
    info.height = depth ? std::min(color->image.h, depth->image.h) : color->image.h;
    info.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    vkCreateFramebuffer(device, &info, nullptr, &fb);
    framebuffers.emplace(key, fb);
    return fb;
}

void GSHwBackend::Renderer::beginPass(Surface *color, Surface *depth)
{
    if (inPass && passColorSurface == color && passDepthSurface == depth)
        return;
    endPass();
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = depth ? passColorDepth : passColor;
    info.framebuffer = framebuffer(color, depth);
    info.renderArea.extent = {depth ? std::min(color->image.w, depth->image.w) : color->image.w,
                              depth ? std::min(color->image.h, depth->image.h) : color->image.h};
    gpuKind(GkDraw);
    vkCmdBeginRenderPass(cmd(), &info, VK_SUBPASS_CONTENTS_INLINE);
    inPass = true;
    passColorSurface = color;
    passDepthSurface = depth;
}

// What flushBatch last set in the open command buffer: ~2300 batches a frame re-set the same
// viewport, scissor, sets, pipeline and push constants, each a driver call.
void GSHwBackend::Renderer::flushBatch()
{
    if (batchCount == 0u || !batch.color)
    {
        batchCount = 0;
        return;
    }
    VkCommandBuffer c = cmd();
    beginPass(batch.color, batch.depth);
    const uint32_t fbw = batch.depth ? std::min(batch.color->image.w, batch.depth->image.w) : batch.color->image.w;
    const uint32_t fbh = batch.depth ? std::min(batch.color->image.h, batch.depth->image.h) : batch.color->image.h;
    const uint32_t vw = batch.color->image.w, vh = batch.color->image.h;
    if (!bound.valid || bound.viewportW != vw || bound.viewportH != vh)
    {
        VkViewport viewport{0.0f, 0.0f, static_cast<float>(vw), static_cast<float>(vh), 0.0f, 1.0f};
        vkCmdSetViewport(c, 0, 1, &viewport);
        bound.viewportW = vw, bound.viewportH = vh;
    }
    VkRect2D sc = batch.scissor;
    sc.offset.x = std::min<int32_t>(sc.offset.x, static_cast<int32_t>(fbw));
    sc.offset.y = std::min<int32_t>(sc.offset.y, static_cast<int32_t>(fbh));
    sc.extent.width = std::min<uint32_t>(sc.extent.width, fbw - static_cast<uint32_t>(sc.offset.x));
    sc.extent.height = std::min<uint32_t>(sc.extent.height, fbh - static_cast<uint32_t>(sc.offset.y));
    if (!bound.valid || std::memcmp(&bound.scissor, &sc, sizeof(sc)) != 0)
        vkCmdSetScissor(c, 0, 1, &sc), bound.scissor = sc;
    if (!bound.valid || bound.blendConstant != batch.blendConstant)
    {
        const float constants[4] = {batch.blendConstant, batch.blendConstant, batch.blendConstant, batch.blendConstant};
        vkCmdSetBlendConstants(c, constants);
        bound.blendConstant = batch.blendConstant;
    }
    if (!bound.valid)
    {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(c, 0, 1, &slots[slot].vertices.buffer, &offset);
    }
    const VkDescriptorSet sets[2] = {batch.set, batch.set1 ? batch.set1 : defaultMapSet};
    if (!bound.valid || bound.sets[0] != sets[0] || bound.sets[1] != sets[1])
    {
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, sets, 0, nullptr);
        bound.sets[0] = sets[0], bound.sets[1] = sets[1];
    }
    const auto use = [&](uint64_t key, const HwPush &push)
    {
        if (!bound.valid || bound.pipeline != key)
            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline(key)), bound.pipeline = key;
        if (!bound.valid || std::memcmp(&bound.push, &push, sizeof(HwPush)) != 0)
        {
            vkCmdPushConstants(c, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(HwPush), &push);
            std::memcpy(&bound.push, &push, sizeof(HwPush));
        }
    };
    use(batch.pipeline, batch.push);
    bound.valid = true;
    vkCmdDraw(c, batchCount, 1, static_cast<uint32_t>(batchFirst), 0);
    if (batch.pipeline2)
    {
        HwPush second = batch.push;
        second.flags |= 64u;
        use(batch.pipeline2, second);
        vkCmdDraw(c, batchCount, 1, static_cast<uint32_t>(batchFirst), 0);
    }
    ++owner.m_stats.batches;
    batch.color->gpuNewer = true;
    batch.color->writeSeq = ++writeCounter;
    if (batch.depth && PipeKey::unpack(batch.pipeline).depthWrite)
        batch.depth->gpuNewer = true, batch.depth->writeSeq = ++writeCounter;
    batchFirst += batchCount;
    batchCount = 0;
}

void GSHwBackend::Renderer::syncInner()
{
    if (!innerDirty)
        return;
    cpu.Sync(GSSyncReason::DebugReadback); // the mirror's transfers run on the CPU backend's worker
    innerDirty = false;
    for (uint32_t blk : pendingWriteList)
        pendingWrite[blk] = 0u;
    pendingWriteList.clear();
    pendingUnknown = false;
}

void GSHwBackend::Renderer::destroySurface(Surface *s)
{
    if (feedbackSurface == s)
        feedbackSurface = nullptr;
    if (passColorSurface == s || passDepthSurface == s || batch.color == s || batch.depth == s)
    {
        flushBatch();
        endPass();
        batch = BatchState{};
    }
    for (auto it = framebuffers.begin(); it != framebuffers.end();)
    {
        if (it->first.first == s || it->first.second == s)
        {
            cmd();
            slots[slot].garbageFramebuffers.push_back(it->second);
            it = framebuffers.erase(it);
        }
        else
            ++it;
    }
    retire(s->image);
    retire(s->shadow);
    ++generation;
    surfaces.erase(std::remove_if(surfaces.begin(), surfaces.end(), [s](const std::unique_ptr<Surface> &p) { return p.get() == s; }),
                   surfaces.end());
}

GSHwBackend::Renderer::Surface *GSHwBackend::Renderer::surface(uint32_t block, uint32_t fbw, uint32_t psm, bool depth, uint32_t height)
{
    fbw = std::max(fbw, 1u);
    height = std::clamp((height + 31u) & ~31u, 32u, 2048u);
    for (auto &p : surfaces)
    {
        Surface *s = p.get();
        if (s->block == block && s->fbw == fbw && s->depth == depth && s->psm == psm)
        {
            if (s->h >= height)
                return s;
            why = WhyEvict, download({s}); // grows: the bigger copy starts from the mirror
            destroySurface(s);
            break;
        }
    }
    uint32_t b0 = 0, b1 = 0;
    blockRange(block, fbw, psm, fbw * 64u, height, b0, b1);
    // The same memory as colour and as depth (MKSM: the Z buffer becomes the bloom scratch)
    // keeps both surfaces: the game does not read one through the other, and a round trip
    // through the mirror each frame cost more than the frame. Only a different footprint
    // (buffer width or base) evicts.
    // shortcut: a read of such memory as the other kind sees stale data; mirror sync it then.
    std::vector<Surface *> clash;
    static const bool evictAll = std::getenv("PS2X_GS_HW_EVICT") != nullptr;
    for (auto &p : surfaces)
        if (overlaps(b0, b1, p->b0, p->b1) && (evictAll ? !(p->block == block && p->fbw == fbw) : (p->block == block && p->fbw != fbw)))
            clash.push_back(p.get());
    bool fresh = false;
    if (!clash.empty())
    {
        // Same base, other width (MKSM's depth copies for the bloom go 640 -> 320 -> 256 wide):
        // the old content is not read through the new layout, so drop it instead of a round
        // trip through the mirror every frame. PS2X_GS_HW_STRICT=1 keeps the round trip.
        static const bool strict = std::getenv("PS2X_GS_HW_STRICT") != nullptr;
        if (strict)
            why = WhyEvict, download(clash);
        else
            fresh = true;
        for (Surface *s : clash)
            destroySurface(s);
    }
    auto s = std::make_unique<Surface>();
    s->block = block, s->fbw = fbw, s->psm = psm, s->depth = depth;
    s->w = fbw * 64u, s->h = height, s->b0 = b0, s->b1 = b1;
    s->image = createImage(s->w * scale, s->h * scale, depth ? kDepthFormat : kColorFormat,
                           depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    s->mirrorNewer = !fresh; // a dropped surface leaves a cleared one, not the stale mirror
    surfaces.push_back(std::move(s));
    return surfaces.back().get();
}

void GSHwBackend::Renderer::upload(Surface &s)
{
    // Transferred blocks reach a surface when it is used (MKSM rewrites 0x2980 in ~30 small
    // transfers a field; refreshing every surface at every draw state was ~1200 refreshes/s).
    refreshPending(&s);
    if (!s.mirrorNewer)
        return;
    ++generation;
    s.shadowFull = false;
    syncInner();
    const uint32_t texel = s.depth ? 4u : 8u;
    VkDeviceSize offset = 0;
    uint8_t *dst = stage(VkDeviceSize(s.w) * s.h * texel, offset);
    if (!dst)
        return;
    const double range = depthRange(s.psm);
    for (uint32_t y = 0; y < s.h; ++y)
        for (uint32_t x = 0; x < s.w; ++x)
        {
            uint8_t *out = dst + (size_t(y) * s.w + x) * texel;
            if (s.depth)
            {
                uint32_t z = s.psm == GS_PSM_Z32 ? GSMem::ReadZ32(vram, s.block, s.fbw, x, y)
                           : s.psm == GS_PSM_Z24 ? GSMem::ReadZ24(vram, s.block, s.fbw, x, y)
                                                 : GSMem::ReadZ16(vram, s.block, s.fbw, x, y);
                const float f = static_cast<float>(static_cast<double>(z) / range);
                std::memcpy(out, &f, 4);
            }
            else
            {
                const uint32_t c = s.psm == GS_PSM_CT24 ? (GSMem::ReadCT24(vram, s.block, s.fbw, x, y) | 0x80000000u)
                                 : s.psm == GS_PSM_Z24  ? (GSMem::ReadZ24(vram, s.block, s.fbw, x, y) | 0x80000000u)
                                 : s.psm == GS_PSM_Z32  ? GSMem::ReadZ32(vram, s.block, s.fbw, x, y)
                                                        : GSMem::ReadCT32(vram, s.block, s.fbw, x, y);
                const uint16_t h[4] = {floatToHalf((c & 0xFFu) / 255.0f), floatToHalf(((c >> 8) & 0xFFu) / 255.0f),
                                       floatToHalf(((c >> 16) & 0xFFu) / 255.0f), floatToHalf((c >> 24) / 128.0f)};
                std::memcpy(out, h, 8);
            }
        }
    VkCommandBuffer c = cmd();
    endPass();
    const VkImageAspectFlags aspect = s.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = {aspect, 0, 0, 1};
    region.imageExtent = {s.w, s.h, 1};
    gpuKind(GkUpload);
    if (scale == 1u)
        vkCmdCopyBufferToImage(c, slots[slot].staging.buffer, s.image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    else
    {
        Image temp = createImage(s.w, s.h, s.image.format, 0);
        gpuKind(GkUpload);
        vkCmdCopyBufferToImage(c, slots[slot].staging.buffer, temp.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        fullBarrier();
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {aspect, 0, 0, 1};
        blit.srcOffsets[1] = {static_cast<int32_t>(s.w), static_cast<int32_t>(s.h), 1};
        blit.dstOffsets[1] = {static_cast<int32_t>(s.w * scale), static_cast<int32_t>(s.h * scale), 1};
        vkCmdBlitImage(c, temp.image, VK_IMAGE_LAYOUT_GENERAL, s.image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
        retire(temp);
    }
    fullBarrier();
    s.mirrorNewer = false;
    if (!s.gpuNewer)
        s.markClean();
}

void GSHwBackend::Renderer::download(const std::vector<Surface *> &list)
{
    const int reason = why; // consumed here, even when nothing needs copying
    why = WhyOther;
    for (Surface *s : list)
        refreshPending(s); // the surface must hold the transferred blocks before it overwrites the mirror
                           // (only these: every transfer calls this, mostly with an empty list)
    std::vector<Surface *> dirty;
    for (Surface *s : list)
        if (s->gpuNewer)
            dirty.push_back(s);
    if (dirty.empty())
        return;
    // Oldest first, so the latest writer of aliased memory wins in the mirror.
    std::sort(dirty.begin(), dirty.end(), [](const Surface *a, const Surface *b) { return a->writeSeq < b->writeSeq; });
    syncInner();
    flushBatch();
    VkCommandBuffer c = cmd();
    endPass();
    // One surface at a time through the readback buffer (each up to 2048x2048 at 1x).
    for (Surface *s : dirty)
    {
        c = cmd();
        const VkImageAspectFlags aspect = s->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        const uint32_t texel = s->depth ? 4u : 8u;
        Image temp;
        VkImage source = s->image.image;
        if (scale != 1u)
        {
            // shortcut: nearest picks one sample per GS pixel, so detail drawn at the higher scale
            // is lost in the mirror; only matters when the game reads a target back.
            temp = createImage(s->w, s->h, s->image.format, 0);
            VkImageBlit blit{};
            blit.srcSubresource = blit.dstSubresource = {aspect, 0, 0, 1};
            blit.srcOffsets[1] = {static_cast<int32_t>(s->w * scale), static_cast<int32_t>(s->h * scale), 1};
            blit.dstOffsets[1] = {static_cast<int32_t>(s->w), static_cast<int32_t>(s->h), 1};
            gpuKind(GkDownload);
            vkCmdBlitImage(c, s->image.image, VK_IMAGE_LAYOUT_GENERAL, temp.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
            fullBarrier();
            source = temp.image;
        }
        VkBufferImageCopy region{};
        region.imageSubresource = {aspect, 0, 0, 1};
        region.imageExtent = {s->w, s->h, 1};
        gpuKind(GkDownload);
        vkCmdCopyImageToBuffer(c, source, VK_IMAGE_LAYOUT_GENERAL, readback.buffer, 1, &region);
        if (temp.image)
            retire(temp);
        submit(true);
        const uint8_t *src = static_cast<const uint8_t *>(readback.mapped);
        const double range = depthRange(s->psm);
        for (uint32_t y = 0; y < s->h; ++y)
            for (uint32_t x = 0; x < s->w; ++x)
            {
                const uint8_t *in = src + (size_t(y) * s->w + x) * texel;
                if (s->depth)
                {
                    float f;
                    std::memcpy(&f, in, 4);
                    const double z = std::clamp(static_cast<double>(f) * range + 0.5, 0.0, range - 1.0);
                    const uint32_t zi = static_cast<uint32_t>(z);
                    if (s->psm == GS_PSM_Z32)
                        GSMem::WriteZ32(vram, s->block, s->fbw, x, y, zi);
                    else if (s->psm == GS_PSM_Z24)
                        GSMem::WriteZ24(vram, s->block, s->fbw, x, y, zi);
                    else
                        GSMem::WriteZ16(vram, s->block, s->fbw, x, y, zi);
                }
                else
                {
                    uint16_t h[4];
                    std::memcpy(h, in, 8);
                    auto byte = [](float v, float mul) { return static_cast<uint32_t>(std::clamp(v * mul + 0.5f, 0.0f, 255.0f)); };
                    const uint32_t value = byte(halfToFloat(h[0]), 255.0f) | (byte(halfToFloat(h[1]), 255.0f) << 8) |
                                           (byte(halfToFloat(h[2]), 255.0f) << 16) | (byte(halfToFloat(h[3]), 128.0f) << 24);
                    if (s->psm == GS_PSM_CT24)
                        GSMem::WriteCT24(vram, s->block, s->fbw, x, y, value);
                    else if (s->psm == GS_PSM_Z24)
                        GSMem::WriteZ24(vram, s->block, s->fbw, x, y, value & 0x00FFFFFFu);
                    else if (s->psm == GS_PSM_Z32)
                        GSMem::WriteZ32(vram, s->block, s->fbw, x, y, value);
                    else
                        GSMem::WriteCT32(vram, s->block, s->fbw, x, y, value);
                }
            }
        s->gpuNewer = false;
        s->markClean();
        ++owner.m_stats.downloads;
        ++downloadsBy[reason];
        invalidateTextures(s->b0, s->b1);
    }
}

void GSHwBackend::Renderer::downloadRange(uint32_t b0, uint32_t b1)
{
    std::vector<Surface *> list;
    for (auto &p : surfaces)
        if (p->gpuNewer && overlaps(b0, b1, p->b0, p->b1))
            list.push_back(p.get());
    download(list);
}

void GSHwBackend::Renderer::downloadAll()
{
    flushBatch();
    downloadRange(0u, kVramBlocks);
}

void GSHwBackend::Renderer::mirrorWrote(uint32_t b0, uint32_t b1)
{
    for (auto &p : surfaces)
        if (overlaps(b0, b1, p->b0, p->b1))
            p->mirrorNewer = true;
    invalidateTextures(b0, b1);
}

void GSHwBackend::Renderer::invalidateTextures(uint32_t b0, uint32_t b1)
{
    for (auto it = textures.begin(); it != textures.end();)
    {
        if (overlaps(b0, b1, it->second.b0, it->second.b1))
        {
            ++generation;
            it = textures.erase(it);
        }
        else
            ++it;
    }
}


void GSHwBackend::Renderer::invalidateTexturesExact(const std::vector<uint8_t> &written, uint32_t b0, uint32_t b1)
{
    for (auto it = textures.begin(); it != textures.end();)
    {
        bool hit = false;
        if (overlaps(b0, b1, it->second.b0, it->second.b1))
            for (uint16_t blk : it->second.blocks)
                if (written[blk])
                {
                    hit = true;
                    break;
                }
        if (hit)
        {
            ++generation;
            it = textures.erase(it);
        }
        else
            ++it;
    }
}



// Only the given 8x8 blocks of a 32-bit colour surface into the mirror (a CLUT read out of the
// bloom scratch): a few hundred bytes instead of the whole surface. The surface stays newer.
bool GSHwBackend::Renderer::downloadBlocks(Surface &s, const std::vector<uint32_t> &blocks)
{
    if (s.depth || !(s.psm == GS_PSM_CT32 || s.psm == GS_PSM_CT24 || s.psm == GS_PSM_Z32 || s.psm == GS_PSM_Z24))
        return false;
    std::vector<std::pair<uint32_t, uint32_t>> origins;
    for (uint32_t blk : blocks)
    {
        uint32_t x = 0, y = 0;
        if (blk >= s.b0 && blk < s.b1 && blockOrigin(s.block, s.fbw, s.psm, blk, x, y) && x + 8u <= s.w && y + 8u <= s.h)
            origins.emplace_back(x, y);
    }
    if (origins.empty())
        return true;
    syncInner();
    flushBatch();
    endPass();
    VkCommandBuffer c = cmd();
    std::vector<VkBufferImageCopy> regions;
    for (size_t i = 0; i < origins.size(); ++i)
    {
        // One texel per GS pixel: at scale N, the top-left sample of each N x N group.
        VkBufferImageCopy r{};
        r.bufferOffset = VkDeviceSize(i) * 8u * 8u * scale * scale * 8u;
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.imageOffset = {static_cast<int32_t>(origins[i].first * scale), static_cast<int32_t>(origins[i].second * scale), 0};
        r.imageExtent = {8u * scale, 8u * scale, 1};
        regions.push_back(r);
    }
    gpuKind(GkDownload);
    vkCmdCopyImageToBuffer(c, s.image.image, VK_IMAGE_LAYOUT_GENERAL, readback.buffer, static_cast<uint32_t>(regions.size()), regions.data());
    submit(true);
    const auto *base = static_cast<const uint8_t *>(readback.mapped);
    for (size_t i = 0; i < origins.size(); ++i)
        for (uint32_t y = 0; y < 8u; ++y)
            for (uint32_t x = 0; x < 8u; ++x)
            {
                uint16_t h[4];
                std::memcpy(h, base + regions[i].bufferOffset + (size_t(y * scale) * 8u * scale + x * scale) * 8u, 8);
                auto byte = [](float v, float mul) { return static_cast<uint32_t>(std::clamp(v * mul + 0.5f, 0.0f, 255.0f)); };
                const uint32_t value = byte(halfToFloat(h[0]), 255.0f) | (byte(halfToFloat(h[1]), 255.0f) << 8) |
                                       (byte(halfToFloat(h[2]), 255.0f) << 16) | (byte(halfToFloat(h[3]), 128.0f) << 24);
                const uint32_t px = origins[i].first + x, py = origins[i].second + y;
                if (s.psm == GS_PSM_CT24)
                    GSMem::WriteCT24(vram, s.block, s.fbw, px, py, value);
                else if (s.psm == GS_PSM_Z24)
                    GSMem::WriteZ24(vram, s.block, s.fbw, px, py, value & 0x00FFFFFFu);
                else if (s.psm == GS_PSM_Z32)
                    GSMem::WriteZ32(vram, s.block, s.fbw, px, py, value);
                else
                    GSMem::WriteCT32(vram, s.block, s.fbw, px, py, value);
            }
    return true;
}


VkDescriptorSet GSHwBackend::Renderer::mapSet(VkImageView map, VkImageView palette, VkImageView dst)
{
    if (!dst)
        dst = dummyPalette.view; // never read without flag 8192
    const auto key = std::make_tuple(map, palette, dst);
    if (auto it = mapSets.find(key); it != mapSets.end())
        return it->second;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &mapSetLayout;
    if (vkAllocateDescriptorSets(device, &info, &set) != VK_SUCCESS)
        return defaultMapSet;
    VkDescriptorImageInfo images[3] = {{sampler, map, VK_IMAGE_LAYOUT_GENERAL}, {sampler, palette, VK_IMAGE_LAYOUT_GENERAL},
                                       {sampler, dst, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3u; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    mapSets.emplace(key, set);
    return set;
}

// A paletted (or reshaped 32-bit) texture whose texels a GPU target holds: read the target in
// the shader through a map built once per texture shape and target footprint. Each map texel
// says which target pixel holds the texel and which byte/nibble of it (PixelAddress gives
// bytes for T8, nibbles for T4 and 32-bit words for T8H/T4HL/T4HH; checked by the gs test
// "T8, T4 and T8H texels map to a CT32 word and byte lane through PixelAddress").
bool GSHwBackend::Renderer::mappedRead(const GSPrimitiveBatch &b, Surface *s, Surface *target, BatchState &out)
{
    const GSDrawState &state = b.state;
    const GSTex0Reg &tex = state.context.tex0;
    const uint32_t w = state.textureWidth, h = state.textureHeight;
    const bool paletted = isPaletted(tex.psm);
    const bool wide = tex.psm == GS_PSM_CT32 || tex.psm == GS_PSM_CT24 || tex.psm == GS_PSM_Z32 || tex.psm == GS_PSM_Z24;
    const bool target32 = s->psm == GS_PSM_CT32 || s->psm == GS_PSM_CT24 || s->psm == GS_PSM_Z32 || s->psm == GS_PSM_Z24;
    // shortcut: a mapped read of the target being drawn (feedback) still goes through the mirror.
    if ((!paletted && !wide) || !target32 || s == target || s == out.depth)
        return false;
    upload(*s);

    // Target pixels by word, once per footprint.
    const uint64_t targetKey = (uint64_t(s->block) << 40) ^ (uint64_t(s->fbw) << 32) ^ (uint64_t(s->psm) << 24) ^ s->h;
    auto &pixels = targetPixels[targetKey];
    const uint32_t baseWord = s->b0 * 64u, words = (s->b1 - s->b0) * 64u;
    if (pixels.empty())
    {
        pixels.assign(words, ~0u);
        const uint32_t addressPsm = (s->psm == GS_PSM_Z32 || s->psm == GS_PSM_Z24) ? GS_PSM_Z32 : GS_PSM_CT32;
        for (uint32_t y = 0; y < s->h; ++y)
            for (uint32_t x = 0; x < s->w; ++x)
            {
                const uint32_t word = GSMem::PixelAddress(addressPsm, s->block, s->fbw, x, y);
                if (word >= baseWord && word - baseWord < words)
                    pixels[word - baseWord] = x | (y << 16);
            }
    }

    // The map, once per texture shape and footprint.
    const uint64_t mapKey = (((uint64_t(tex.tbp0) << 40) ^ (uint64_t(tex.tbw) << 34) ^ (uint64_t(tex.psm) << 28) ^ (uint64_t(w) << 14) ^ h) *
                             0x9E3779B97F4A7C15ull) ^ targetKey;
    auto found = maps.find(mapKey);
    if (found == maps.end())
    {
        MapEntry entry;
        entry.complete = true;
        entry.w = w, entry.h = h;
        VkDeviceSize offset = 0;
        uint8_t *dst = stage(VkDeviceSize(w) * h * 4u, offset);
        if (!dst)
            return false;
        auto *texels = reinterpret_cast<uint16_t *>(dst);
        std::vector<uint8_t> isOutside(size_t(w) * h, 0u);
        bool anyInside = false;
        for (uint32_t v = 0; v < h; ++v)
            for (uint32_t u = 0; u < w; ++u)
            {
                const uint32_t address = GSMem::PixelAddress(tex.psm, tex.tbp0, std::max<uint32_t>(tex.tbw, 1u), u, v);
                uint32_t word = address, lane = 3u, nibble = 0u, whole = 0u;
                switch (tex.psm)
                {
                case GS_PSM_T8: word = address >> 2, lane = address & 3u; break;
                case GS_PSM_T4: word = address >> 3, lane = (address >> 1) & 3u, nibble = address & 1u; break;
                case GS_PSM_T4HH: nibble = 1u; break;
                case GS_PSM_T8H: case GS_PSM_T4HL: break;
                default: whole = 1u; break; // 32-bit read with another buffer width
                }
                const bool missingByte = lane == 3u && !whole && (s->psm == GS_PSM_Z24 || s->psm == GS_PSM_CT24);
                const uint32_t packed = (!missingByte && word >= baseWord && word - baseWord < words) ? pixels[word - baseWord] : ~0u;
                if (packed == ~0u)
                {
                    // Outside this target: never sampled when a draw passes the region check.
                    entry.complete = false;
                    isOutside[size_t(v) * w + u] = 1u;
                    texels[(size_t(v) * w + u) * 2u] = 0u;
                    texels[(size_t(v) * w + u) * 2u + 1u] = 0u;
                    continue;
                }
                anyInside = true;
                texels[(size_t(v) * w + u) * 2u] = static_cast<uint16_t>((packed & 0xFFFu) | (lane << 12) | (nibble << 14) | (whole << 15));
                texels[(size_t(v) * w + u) * 2u + 1u] = static_cast<uint16_t>(packed >> 16);
            }
        if (!entry.complete)
        {
            entry.outside.assign(size_t(w + 1u) * (h + 1u), 0u);
            for (uint32_t v = 0; v < h; ++v)
                for (uint32_t u = 0; u < w; ++u)
                    entry.outside[size_t(v + 1u) * (w + 1u) + u + 1u] = isOutside[size_t(v) * w + u] + entry.outside[size_t(v) * (w + 1u) + u + 1u] +
                                                                       entry.outside[size_t(v + 1u) * (w + 1u) + u] - entry.outside[size_t(v) * (w + 1u) + u];
        }
        if (anyInside)
        {
            entry.image = createImage(w, h, VK_FORMAT_R16G16_UINT, 0);
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {w, h, 1};
            endPass();
            gpuKind(GkUpload);
            vkCmdCopyBufferToImage(cmd(), slots[slot].staging.buffer, entry.image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
            fullBarrier();
        }
        found = maps.emplace(mapKey, entry).first;
    }
    if (!found->second.image.image)
        return false; // no texel of it lives in this target
    if (!found->second.complete)
    {
        // Only the region this primitive samples (plus the filter's neighbours) must be inside:
        // MKSM samples small parts of nominally 1024x1024 textures laid over its targets.
        float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
        for (uint32_t i = 0; i < b.vertexCount; ++i)
        {
            const GSVertex &vx = b.vertices[i];
            const float q = std::fabs(vx.q) > 1e-12f ? std::fabs(vx.q) : 1.0f;
            const float u = state.prim.fst ? vx.u / 16.0f : vx.s / q * w;
            const float vv = state.prim.fst ? vx.v / 16.0f : vx.t / q * h;
            u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, vv), v1 = std::max(v1, vv);
        }
        const bool insideTexture = u0 >= 0.0f && v0 >= 0.0f && u1 < float(w) && v1 < float(h);
        if (!insideTexture ||
            found->second.outsideIn(int(std::floor(u0)) - 2, int(std::floor(v0)) - 2, int(std::ceil(u1)) + 2, int(std::ceil(v1)) + 2) != 0u)
            return false;
    }

    // The palette, once per content.
    VkImageView paletteView = dummyPalette.view;
    if (paletted)
    {
        const Palette &converted = paletteFor(state);
        const std::array<uint32_t, 256> &colors = converted.colors;
        const uint64_t hash = converted.hash;
        auto pal = palettes.find(hash);
        if (pal == palettes.end())
        {
            if (palettes.size() > 2048u)
            {
                for (auto &old : palettes)
                    retire(old.second);
                palettes.clear();
            }
            VkDeviceSize offset = 0;
            uint8_t *dst = stage(256u * 4u, offset);
            if (!dst)
                return false;
            std::memcpy(dst, colors.data(), 256u * 4u);
            Image image = createImage(256, 1, VK_FORMAT_R8G8B8A8_UNORM, 0);
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {256, 1, 1};
            endPass();
            gpuKind(GkUpload);
            vkCmdCopyBufferToImage(cmd(), slots[slot].staging.buffer, image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
            fullBarrier();
            pal = palettes.emplace(hash, image).first;
        }
        paletteView = pal->second.view;
    }

    out.set = s->image.set;
    out.set1 = mapSet(found->second.image.view, paletteView);
    out.push.flags |= 1024u | (isFourBit(tex.psm) || tex.psm == GS_PSM_T4HL || tex.psm == GS_PSM_T4HH ? 4096u : 0u) | (s->depth ? 128u : 0u);
    out.push.depthScale = static_cast<float>(depthRange(s->psm));
    if (tex.psm == GS_PSM_CT24 || tex.psm == GS_PSM_Z24)
        out.push.texa = state.texa.ta0 | (state.texa.aem ? 256u : 0u) | 512u;
    return true;
}

void GSHwBackend::Renderer::refreshPending(Surface *only)
{
    if (only ? only->pending.empty() : std::none_of(surfaces.begin(), surfaces.end(), [](auto &p) { return !p->pending.empty(); }))
        return;
    syncInner();
    flushBatch();
    endPass();
    // Pixels keep one order inside every 256-byte block (ps2x_tests gs: "keep one order").
    static const auto tables = [] {
        struct T { uint32_t order[2][64]; uint16_t color[256], alpha[256]; } t{};
        const uint32_t psms[2] = {GS_PSM_CT32, GS_PSM_Z32};
        for (int f = 0; f < 2; ++f)
        {
            const uint32_t base = GSMem::PixelAddress(psms[f], 0u, 1u, 0u, 0u) & ~63u;
            for (uint32_t i = 0; i < 64u; ++i)
                t.order[f][i] = GSMem::PixelAddress(psms[f], 0u, 1u, i % 8u, i / 8u) - base;
        }
        for (uint32_t i = 0; i < 256u; ++i)
            t.color[i] = floatToHalf(i / 255.0f), t.alpha[i] = floatToHalf(i / 128.0f);
        return t;
    }();
    struct Item
    {
        Surface *s;
        uint32_t x0, y0, block;
    };
    std::vector<Item> items;
    const auto take = [&items](Surface *s) {
        for (uint32_t block : s->pending)
        {
            uint32_t x0 = 0, y0 = 0;
            if (blockOrigin(s->block, s->fbw, s->psm, block, x0, y0) && x0 + 8u <= s->w && y0 + 8u <= s->h)
                items.push_back({s, x0, y0, block});
        }
        s->pending.clear();
    };
    if (only)
        take(only);
    else
        for (auto &p : surfaces)
            take(p.get());
    // Grouped by surface, each block once: every copy reads the mirror as it is now, so repeats
    // carry the same bytes, and regions of one copy or blit must not overlap.
    std::sort(items.begin(), items.end(), [](const Item &l, const Item &r) { return l.s != r.s ? l.s < r.s : l.block < r.block; });
    items.erase(std::unique(items.begin(), items.end(), [](const Item &l, const Item &r) { return l.s == r.s && l.block == r.block; }), items.end());

    // The blocks of each surface as rectangles of its block grid, one copy region (and at larger
    // scales one blit region) each: MKSM rewrites all of 0x2980 every field, one rectangle instead
    // of ~1300 regions (the driver did work of its own per region; ~5-10% of the GS thread at 2x).
    struct Rect
    {
        Surface *s;
        uint32_t x, y, w, h;          // in blocks
        std::vector<uint32_t> blocks; // row-major
    };
    std::vector<Rect> rects;
    constexpr uint32_t kMaxRect = 128u; // blocks per side; the scratch is that square at 8 pixels a block
    for (size_t i = 0; i < items.size();)
    {
        size_t j = i;
        while (j < items.size() && items[j].s == items[i].s)
            ++j;
        Surface *s = items[i].s;
        const uint32_t gw = s->w / 8u, gh = s->h / 8u;
        std::vector<uint32_t> cell(size_t(gw) * gh, ~0u); // block, ~0u = nothing pending
        for (size_t k = i; k < j; ++k)
            cell[size_t(items[k].y0 / 8u) * gw + items[k].x0 / 8u] = items[k].block;
        for (uint32_t y = 0; y < gh; ++y)
            for (uint32_t x = 0; x < gw; ++x)
            {
                if (cell[size_t(y) * gw + x] == ~0u)
                    continue;
                uint32_t w = 0, h = 1;
                while (x + w < gw && w < kMaxRect && cell[size_t(y) * gw + x + w] != ~0u)
                    ++w;
                while (y + h < gh && h < kMaxRect &&
                       std::all_of(cell.begin() + ptrdiff_t(size_t(y + h) * gw + x), cell.begin() + ptrdiff_t(size_t(y + h) * gw + x + w),
                                   [](uint32_t c) { return c != ~0u; }))
                    ++h;
                Rect r{s, x, y, w, h, std::vector<uint32_t>(size_t(w) * h)};
                for (uint32_t ry = 0; ry < h; ++ry)
                    for (uint32_t rx = 0; rx < w; ++rx)
                    {
                        uint32_t &c = cell[size_t(y + ry) * gw + x + rx];
                        r.blocks[size_t(ry) * w + rx] = c;
                        c = ~0u;
                    }
                s->noteDrawn(int(x * 8u), int(y * 8u), int((x + w) * 8u) - 1, int((y + h) * 8u) - 1); // the shadow misses these
                rects.push_back(std::move(r));
            }
        i = j;
    }

    for (size_t first = 0; first < rects.size();)
    {
        // As many rectangles as fit the scratch (shelves; at scale 1 they go straight to the surface).
        std::vector<std::array<uint32_t, 2>> at;
        VkDeviceSize bytes = 0;
        uint32_t sx = 0, sy = 0, shelf = 0;
        size_t last = first;
        for (; last < rects.size(); ++last)
        {
            const Rect &r = rects[last];
            if (scale != 1u)
            {
                if (sx + r.w > kMaxRect)
                    sx = 0u, sy += shelf, shelf = 0u;
                if (sy + r.h > kMaxRect)
                    break;
                at.push_back({sx, sy});
                sx += r.w, shelf = std::max(shelf, r.h);
            }
            bytes += VkDeviceSize(r.w) * r.h * 512u;
        }
        VkDeviceSize offset = 0;
        uint8_t *dst = stage(bytes, offset);
        if (!dst)
            break;
        std::vector<VkBufferImageCopy> copies;
        VkDeviceSize at0 = 0;
        for (size_t k = first; k < last; ++k)
        {
            const Rect &r = rects[k];
            const bool z = r.s->psm == GS_PSM_Z32 || r.s->psm == GS_PSM_Z24;
            const uint32_t keep = (r.s->psm == GS_PSM_CT24 || r.s->psm == GS_PSM_Z24) ? 0x00FFFFFFu : 0xFFFFFFFFu;
            const uint32_t force = keep == 0xFFFFFFFFu ? 0u : 0x80000000u;
            const uint32_t pitch = r.w * 8u; // pixels
            uint8_t *base = dst + at0;
            for (uint32_t ry = 0; ry < r.h; ++ry)
                for (uint32_t rx = 0; rx < r.w; ++rx)
                {
                    const uint8_t *words = vram + size_t(r.blocks[size_t(ry) * r.w + rx] % kVramBlocks) * 256u;
                    for (uint32_t p = 0; p < 64u; ++p)
                    {
                        uint32_t c = 0;
                        std::memcpy(&c, words + tables.order[z][p] * 4u, 4);
                        c = (c & keep) | force;
                        const uint16_t h[4] = {tables.color[c & 0xFFu], tables.color[(c >> 8) & 0xFFu], tables.color[(c >> 16) & 0xFFu], tables.alpha[c >> 24]};
                        std::memcpy(base + (size_t(ry * 8u + p / 8u) * pitch + rx * 8u + p % 8u) * 8u, h, 8);
                    }
                }
            VkBufferImageCopy region{};
            region.bufferOffset = offset + at0;
            region.bufferRowLength = pitch;
            region.bufferImageHeight = r.h * 8u;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {r.w * 8u, r.h * 8u, 1};
            if (scale == 1u)
                region.imageOffset = {static_cast<int32_t>(r.x * 8u), static_cast<int32_t>(r.y * 8u), 0};
            else
                region.imageOffset = {static_cast<int32_t>(at[k - first][0] * 8u), static_cast<int32_t>(at[k - first][1] * 8u), 0};
            copies.push_back(region);
            at0 += VkDeviceSize(r.w) * r.h * 512u;
        }
        VkCommandBuffer c = cmd();
        gpuKind(GkUpload);
        if (scale == 1u)
        {
            for (size_t i = first; i < last;)
            {
                size_t j = i;
                while (j < last && rects[j].s == rects[i].s)
                    ++j;
                vkCmdCopyBufferToImage(c, slots[slot].staging.buffer, rects[i].s->image.image, VK_IMAGE_LAYOUT_GENERAL,
                                       static_cast<uint32_t>(j - i), copies.data() + (i - first));
                i = j;
            }
        }
        else
        {
            if (!blockStage.image)
                blockStage = createImage(kMaxRect * 8u, kMaxRect * 8u, kColorFormat, 0);
            vkCmdCopyBufferToImage(c, slots[slot].staging.buffer, blockStage.image, VK_IMAGE_LAYOUT_GENERAL,
                                   static_cast<uint32_t>(copies.size()), copies.data());
            fullBarrier();
            std::vector<VkImageBlit> blits;
            for (size_t k = first; k < last; ++k)
            {
                const Rect &r = rects[k];
                VkImageBlit blit{};
                blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.srcOffsets[0] = copies[k - first].imageOffset;
                blit.srcOffsets[1] = {blit.srcOffsets[0].x + int32_t(r.w * 8u), blit.srcOffsets[0].y + int32_t(r.h * 8u), 1};
                blit.dstOffsets[0] = {static_cast<int32_t>(r.x * 8u * scale), static_cast<int32_t>(r.y * 8u * scale), 0};
                blit.dstOffsets[1] = {static_cast<int32_t>((r.x + r.w) * 8u * scale), static_cast<int32_t>((r.y + r.h) * 8u * scale), 1};
                blits.push_back(blit);
            }
            for (size_t i = first; i < last;)
            {
                size_t j = i;
                while (j < last && rects[j].s == rects[i].s)
                    ++j;
                vkCmdBlitImage(c, blockStage.image, VK_IMAGE_LAYOUT_GENERAL, rects[i].s->image.image, VK_IMAGE_LAYOUT_GENERAL,
                               static_cast<uint32_t>(j - i), blits.data() + (i - first), VK_FILTER_NEAREST);
                i = j;
            }
            fullBarrier(); // the next group reuses the scratch image
        }
        first = last;
    }
    fullBarrier();
    ++generation;
}

void GSHwBackend::Renderer::loadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    if (!isPaletted(tex0.psm))
        return;
    switch (tex0.cld)
    {
    case 1u: break;
    case 2u: clutCbp[0] = tex0.cbp; break;
    case 3u: clutCbp[1] = tex0.cbp; break;
    case 4u: if (clutCbp[0] == tex0.cbp) return; clutCbp[0] = tex0.cbp; break;
    case 5u: if (clutCbp[1] == tex0.cbp) return; clutCbp[1] = tex0.cbp; break;
    default: return;
    }
    const bool fourBit = isFourBit(tex0.psm);
    const bool sixteenBit = tex0.cpsm == GS_PSM_CT16 || tex0.cpsm == GS_PSM_CT16S;
    const bool thirtyTwoBit = tex0.cpsm == GS_PSM_CT32 || tex0.cpsm == GS_PSM_CT24;
    if (!sixteenBit && !thirtyTwoBit)
        return;
    uint32_t c0 = 0, c1 = 0;
    blockRange(tex0.cbp, std::max<uint32_t>(texclut.cbw, 1u), tex0.cpsm, 256u, 64u, c0, c1);
    syncIfPending(c0, c1);
    std::vector<uint32_t> clutBlocks;
    {
        // The exact CLUT blocks; surfaces that hold them give up just those blocks.
        const uint32_t cw = tex0.csm == 0u ? (fourBit ? 8u : 16u) : 256u, ch = tex0.csm == 0u ? (fourBit ? 2u : 16u) : 1u;
        const uint32_t cx = tex0.csm == 0u ? 0u : static_cast<uint32_t>(texclut.cou) * 16u, cy = tex0.csm == 0u ? 0u : texclut.cov;
        const uint32_t cbw = tex0.csm == 0u ? 1u : std::max<uint32_t>(texclut.cbw, 1u);
        forEachBlock(tex0.cbp, cbw, tex0.cpsm, cx, cy, cw, ch, [&](uint32_t blk) {
            if (std::find(clutBlocks.begin(), clutBlocks.end(), blk) == clutBlocks.end())
                clutBlocks.push_back(blk);
        });
        for (auto &sp : surfaces)
        {
            if (!sp->gpuNewer || !overlaps(c0, c1, sp->b0, sp->b1))
                continue;
            // Uploaded after the surface's last GPU write: the mirror already has these blocks.
            bool mirrorCurrent = true;
            for (uint32_t blk : clutBlocks)
                if (!blockCurrent(*sp, blk))
                    mirrorCurrent = false;
            if (!mirrorCurrent && !downloadBlocks(*sp, clutBlocks))
                why = WhyClut, download({sp.get()});
        }
    }
    const uint32_t entryCount = fourBit ? 16u : 256u;
    const uint32_t destinationBase = (static_cast<uint32_t>(tex0.csa) & (sixteenBit ? 0x1Fu : 0x0Fu)) << 4u;
    const bool csm1Suffix = tex0.csm == 0u && thirtyTwoBit && !fourBit;
    const uint32_t firstEntry = csm1Suffix ? destinationBase : 0u;
    // The source blocks as the mirror holds them now (current after the above) pick a stored load.
    const uint64_t key = uint64_t(tex0.cbp) | uint64_t(tex0.cpsm) << 14 | uint64_t(tex0.csm) << 20 | uint64_t(tex0.csa) << 21 |
                         uint64_t(fourBit) << 26 |
                         (tex0.csm ? (uint64_t(texclut.cbw) << 27 | uint64_t(texclut.cou) << 33 | uint64_t(texclut.cov) << 39) : 0u);
    uint64_t raw = mix64(key);
    for (uint32_t blk : clutBlocks)
        raw = mix64(raw ^ hashBytes(vram + size_t(blk) * 256u, 256u));
    ClutLoad &stored = clutLoads[raw & (clutLoads.size() - 1u)];
    if (stored.key != key || stored.raw != raw)
    {
        // The CLUT texels a row at a time (CSM1: a 16x16 or 16x2 area, then the entry order; CSM2:
        // one row), instead of a full address per entry.
        uint32_t texelsRead[256];
        const uint32_t readPsm = sixteenBit ? tex0.cpsm : GS_PSM_CT32;
        if (tex0.csm == 0u)
            for (uint32_t y = 0; y < (fourBit ? 2u : 16u); ++y)
                GSMem::ReadRow(vram, readPsm, tex0.cbp, 1u, 0u, y, texelsRead + y * 16u, 16u);
        else
            GSMem::ReadRow(vram, readPsm, tex0.cbp, texclut.cbw ? texclut.cbw : 1u, static_cast<uint32_t>(texclut.cou) << 4u, texclut.cov,
                           texelsRead, entryCount);
        for (uint32_t entry = firstEntry; entry < entryCount; ++entry)
        {
            const uint32_t texel = tex0.csm == 0u ? texelsRead[(entry & ~0x18u) | ((entry & 0x08u) << 1u) | ((entry & 0x10u) >> 1u)]
                                                  : texelsRead[entry];
            stored.lo[entry] = static_cast<uint16_t>(sixteenBit ? texel : (texel & 0xFFFFu));
            stored.hi[entry] = static_cast<uint16_t>(texel >> 16u);
        }
        stored.key = key, stored.raw = raw;
    }
    bool clutChanged = false;
    const uint32_t mask = sixteenBit ? 0x1FFu : 0x0FFu, d0 = (csm1Suffix ? firstEntry : destinationBase + firstEntry) & mask;
    const uint32_t n = entryCount - firstEntry;
    if (d0 + n <= mask + 1u)
    {
        clutChanged = std::memcmp(&clut[d0], &stored.lo[firstEntry], n * 2u) != 0 ||
                      (!sixteenBit && std::memcmp(&clut[d0 + 256u], &stored.hi[firstEntry], n * 2u) != 0);
        std::memcpy(&clut[d0], &stored.lo[firstEntry], n * 2u);
        if (!sixteenBit)
            std::memcpy(&clut[d0 + 256u], &stored.hi[firstEntry], n * 2u);
    }
    else
        for (uint32_t entry = firstEntry; entry < entryCount; ++entry)
        {
            const uint32_t d = (csm1Suffix ? entry : destinationBase + entry) & mask;
            if (clut[d] != stored.lo[entry] || (!sixteenBit && clut[d + 256u] != stored.hi[entry]))
                clutChanged = true;
            clut[d] = stored.lo[entry];
            if (!sixteenBit)
                clut[d + 256u] = stored.hi[entry];
        }
    if (clutChanged)
        ++clutSerial, clutHash = hashBytes(clut.data(), clut.size() * sizeof(clut[0]));
}

// The CLUT converted for how this draw reads it, redone only when the CLUT or that changes (256
// clutColor calls per texture lookup and per mapped read were ~14% of the renderer thread at 2x).
const GSHwBackend::Renderer::Palette &GSHwBackend::Renderer::paletteFor(const GSDrawState &state)
{
    const GSTex0Reg &tex = state.context.tex0;
    const uint32_t n = isFourBit(tex.psm) ? 16u : 256u;
    const uint64_t how = uint64_t(tex.cpsm) | (uint64_t(tex.csa) << 8) | (uint64_t(tex.csm) << 16) | (uint64_t(n) << 24) |
                         (uint64_t(state.texa.ta0) << 32) | (uint64_t(state.texa.ta1) << 40) | (uint64_t(state.texa.aem) << 48);
    Palette &p = palettesRead[mix64(clutHash ^ how) & (palettesRead.size() - 1u)];
    if (p.clut == clutHash && p.how == how)
        return p;
    p.colors.fill(0u);
    p.hash = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < n; ++i)
    {
        p.colors[i] = clutColor(state, i);
        p.hash = (p.hash ^ p.colors[i]) * 0x100000001B3ull;
    }
    p.clut = clutHash, p.how = how;
    return p;
}

uint32_t GSHwBackend::Renderer::clutColor(const GSDrawState &state, uint32_t index) const
{
    const GSTex0Reg &tex = state.context.tex0;
    const bool sixteenBit = tex.cpsm == GS_PSM_CT16 || tex.cpsm == GS_PSM_CT16S;
    const uint32_t base = (static_cast<uint32_t>(tex.csa) & (sixteenBit ? 0x1Fu : 0x0Fu)) << 4u;
    const uint32_t source = isFourBit(tex.psm) ? (index & 0x0Fu) : (index & 0xFFu);
    uint32_t i = (base + source) & (sixteenBit ? 0x1FFu : 0x0FFu);
    if (!sixteenBit && tex.csm == 0u && !isFourBit(tex.psm))
        i = std::min((source & 0xF0u) + base, 240u) + (source & 0x0Fu);
    if (sixteenBit)
        return expand5551(clut[i], state.texa);
    const uint32_t raw = static_cast<uint32_t>(clut[i]) | (static_cast<uint32_t>(clut[i + 256u]) << 16u);
    return applyTexa(state.texa, tex.cpsm, tex.cpsm == GS_PSM_CT24 ? (raw & 0x00FFFFFFu) : raw);
}

// Brings the shadow up to the image: only the rectangles drawn since the last copy when it was
// whole (the whole image is ~9 MB at 2x, and the stage needed ~55 copies a frame).
void GSHwBackend::Renderer::gpuCollect(uint32_t index)
{
    const uint32_t n = stampsUsed[index];
    stampsUsed[index] = 0u;
    if (n >= 2u)
    {
        uint64_t t[kStampsPerSlot];
        if (vkGetQueryPoolResults(device, stampPool, index * kStampsPerSlot, n, n * sizeof(uint64_t), t, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
            for (uint32_t i = 1; i < n; ++i)
                if (stampKinds[index][i] < GkStart && t[i] >= t[i - 1])
                    gpuNs[stampKinds[index][i]] += double(t[i] - t[i - 1]) * stampNs;
            if (t[n - 1] >= t[0])
                gpuBusyNs += double(t[n - 1] - t[0]) * stampNs;
        }
    }
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (gpuReportMs == 0)
        gpuReportMs = now;
    if (now - gpuReportMs < 2000)
        return;
    const double perSecond = 1e-6 * 1000.0 / double(now - gpuReportMs); // ns -> ms per second
    std::fprintf(stderr, "[perf] GPU novo (ms por s): desenho %.0f, sombra %.0f, envio %.0f, leitura %.0f, janela %.0f, outros %.0f | ocupada %.0f%%%s\n",
                 gpuNs[GkDraw] * perSecond, gpuNs[GkShadow] * perSecond, gpuNs[GkUpload] * perSecond, gpuNs[GkDownload] * perSecond,
                 gpuNs[GkPresent] * perSecond, gpuNs[GkOther] * perSecond, gpuBusyNs * perSecond / 10.0, gpuStampsLost ? " (marcas perdidas)" : "");
    for (double &v : gpuNs)
        v = 0.0;
    gpuBusyNs = 0.0, gpuStampsLost = 0u, gpuReportMs = now;
}

void GSHwBackend::Renderer::refreshShadow(Surface &s)
{
    flushBatch();
    endPass();
    const VkImageSubresourceLayers layers = {static_cast<VkImageAspectFlags>(s.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 0, 1};
    std::vector<VkImageCopy> regions;
    if (s.shadowFull)
        for (const auto &r : s.drawnSinceCopy)
        {
            const int x0 = std::clamp(r[0], 0, int(s.w)), y0 = std::clamp(r[1], 0, int(s.h));
            const int x1 = std::clamp(r[2] + 1, 0, int(s.w)), y1 = std::clamp(r[3] + 1, 0, int(s.h));
            if (x1 <= x0 || y1 <= y0)
                continue;
            VkImageCopy copy{};
            copy.srcSubresource = copy.dstSubresource = layers;
            copy.srcOffset = copy.dstOffset = {int32_t(x0 * scale), int32_t(y0 * scale), 0};
            copy.extent = {uint32_t((x1 - x0) * scale), uint32_t((y1 - y0) * scale), 1};
            regions.push_back(copy);
        }
    else
    {
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = layers;
        copy.extent = {s.image.w, s.image.h, 1};
        regions.push_back(copy);
    }
    if (!regions.empty())
    {
        gpuKind(GkShadow);
        vkCmdCopyImage(cmd(), s.image.image, VK_IMAGE_LAYOUT_GENERAL, s.shadow.image, VK_IMAGE_LAYOUT_GENERAL,
                       static_cast<uint32_t>(regions.size()), regions.data());
        fullBarrier();
    }
    s.shadowFull = true;
    s.drawnSinceCopy.clear();
}

static void readArea(const GSPrimitiveBatch &b, const float offset[2], int &x0, int &y0, int &x1, int &y1);

bool GSHwBackend::Renderer::resolveTexture(const GSPrimitiveBatch &b, Surface *target, BatchState &out)
{
    const GSDrawState &state = b.state;
    const GSTex0Reg &tex = state.context.tex0;
    const uint32_t w = state.textureWidth, h = state.textureHeight;
    out.push.texSize[0] = static_cast<float>(w);
    out.push.texSize[1] = static_cast<float>(h);
    out.push.scale = static_cast<float>(scale);
    // A CLUT load only matters to paletted textures (it was most of the misses here and in draw).
    if (lastTexGeneration == generation && (!isPaletted(tex.psm) || lastTexClutSerial == clutSerial) &&
        std::memcmp(&lastTex0, &tex, sizeof(tex)) == 0 && lastTexW == w && lastTexH == h &&
        lastTexa.ta0 == state.texa.ta0 && lastTexa.ta1 == state.texa.ta1 && lastTexa.aem == state.texa.aem &&
        lastTexTex1 == state.context.tex1 && lastTexMiptbp1 == state.context.miptbp1 && lastTexMiptbp2 == state.context.miptbp2)
    {
        out.set = lastTexState.set;
        out.set1 = lastTexState.set1;
        std::memcpy(out.push.texOffset, lastTexState.push.texOffset, sizeof(out.push.texOffset));
        out.push.flags |= lastTexState.push.flags & (16u | 128u | 1024u | 4096u | 32768u);
        out.push.mip = lastTexState.push.mip;
        out.push.depthScale = lastTexState.push.depthScale;
        out.push.texa = lastTexState.push.texa;
        return true;
    }
    const bool ok = resolveTextureSlow(b, target, out);
    // Feedback copies must be redone for every draw; everything else can be reused.
    if (ok && !feedbackRead && !partialRead)
    {
        lastTexGeneration = generation, lastTexClutSerial = clutSerial, lastTex0 = tex, lastTexa = state.texa, lastTexW = w, lastTexH = h, lastTexState = out;
        lastTexTex1 = state.context.tex1, lastTexMiptbp1 = state.context.miptbp1, lastTexMiptbp2 = state.context.miptbp2;
    }
    else
        lastTexGeneration = 0;
    return ok;
}

namespace
{
    // The texels a primitive can sample (its UV box plus the filter's neighbours), when it stays
    // inside the texture with plain repeat/clamp (no region modes) and covers a small part of it.
    bool sampledRegion(const GSPrimitiveBatch &b, uint32_t w, uint32_t h, uint32_t &x0, uint32_t &y0, uint32_t &x1, uint32_t &y1)
    {
        const uint64_t clamp = b.state.context.clamp;
        if ((clamp & 3u) > 1u || ((clamp >> 2) & 3u) > 1u)
            return false;
        float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
        for (uint32_t i = 0; i < b.vertexCount; ++i)
        {
            const GSVertex &vx = b.vertices[i];
            if (!b.state.prim.fst && vx.q <= 0.0f)
                return false;
            const float u = b.state.prim.fst ? vx.u / 16.0f : vx.s / vx.q * w;
            const float v = b.state.prim.fst ? vx.v / 16.0f : vx.t / vx.q * h;
            u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
        }
        if (!(u0 >= 0.0f && v0 >= 0.0f && u1 <= float(w) && v1 <= float(h)))
            return false;
        x0 = static_cast<uint32_t>(std::max(0.0f, std::floor(u0) - 2.0f));
        y0 = static_cast<uint32_t>(std::max(0.0f, std::floor(v0) - 2.0f));
        x1 = static_cast<uint32_t>(std::min(float(w), std::ceil(u1) + 2.0f));
        y1 = static_cast<uint32_t>(std::min(float(h), std::ceil(v1) + 2.0f));
        return x1 > x0 && y1 > y0 && uint64_t(x1 - x0) * (y1 - y0) * 4u <= uint64_t(w) * h;
    }
}

bool GSHwBackend::Renderer::resolveTextureSlow(const GSPrimitiveBatch &b, Surface *target, BatchState &out)
{
    const GSDrawState &state = b.state;
    const GSTex0Reg &tex = state.context.tex0;
    const uint32_t w = state.textureWidth, h = state.textureHeight;
    feedbackLast = false;
    feedbackRead = false;
    partialRead = false;
    uint32_t rx0 = 0u, ry0 = 0u, rx1 = w, ry1 = h;
    const bool regionKnown = sampledRegion(b, w, h, rx0, ry0, rx1, ry1);
    uint32_t b0 = 0, b1 = 0;
    blockRange(tex.tbp0, tex.tbw, tex.psm, w, h, b0, b1);

    // A texture inside a GPU surface: sample the surface when the layouts agree. The surface
    // starting at the texture base comes first: MKSM samples the frame through a 1024x1024
    // texture at block 0 whose nominal range covers every other target.
    // shortcut: with an exact base match the other overlapped surfaces are assumed unsampled.
    std::vector<Surface *> order;
    for (auto &p : surfaces)
        if (p->block == tex.tbp0 && p->fbw == std::max<uint32_t>(tex.tbw, 1u))
            order.insert(order.begin(), p.get());
        else if (overlaps(b0, b1, p->b0, p->b1))
            order.push_back(p.get());
    const bool exact = !order.empty() && order.front()->block == tex.tbp0;
    if (exact)
        order.resize(1);
    for (Surface *s : order)
    {
        const bool layoutOk = (tex.psm == GS_PSM_CT32 || tex.psm == GS_PSM_CT24 || tex.psm == GS_PSM_Z32 || tex.psm == GS_PSM_Z24) && std::max<uint32_t>(tex.tbw, 1u) == s->fbw &&
                              tex.tbp0 >= s->block && ((tex.tbp0 - s->block) % 32u) == 0u;
        if (!layoutOk)
        {
            // Paletted or reshaped read: decode from the mirror. MKSM streams textures into the
            // bloom scratch's memory, so the mirror usually already holds these blocks (uploaded
            // after the surface's last GPU write) and the surface need not come back.
            bool mirrorCurrent = true;
            if (s->gpuNewer)
                forEachBlock(tex.tbp0, tex.tbw, tex.psm, 0u, 0u, w, h, [&](uint32_t blk) {
                    if (!blockCurrent(*s, blk))
                        mirrorCurrent = false;
                });
            if (!mirrorCurrent && regionKnown)
            {
                // MKSM samples a 32x32 corner of a 1024x1024 texture whose footprint reaches the
                // Z target: if the corner itself is current in the mirror, decode just that part
                // instead of reading the target back (was ~2 GPU waits per frame in 2x).
                mirrorCurrent = true;
                forEachBlock(tex.tbp0, tex.tbw, tex.psm, rx0, ry0, rx1 - rx0, ry1 - ry0, [&](uint32_t blk) {
                    if (!blockCurrent(*s, blk))
                        mirrorCurrent = false;
                });
                partialRead = partialRead || mirrorCurrent;
            }
            if (!mirrorCurrent)
            {
                // Read it on the GPU through a texel -> target pixel/byte map, from the target
                // written last (colour and depth copies of the same memory coexist).
                // Byte 3 of a word belongs to the 32-bit target sharing the memory: a 24-bit one
                // (Z24 depth, CT24/Z24 frames) never writes it. MKSM's bloom reads the Z buffer's
                // memory as T4HL/T4HH/T8H to get the alpha a colour pass left there.
                const bool highByte = tex.psm == GS_PSM_T8H || tex.psm == GS_PSM_T4HL || tex.psm == GS_PSM_T4HH;
                auto is24 = [](const Surface *t) { return t->psm == GS_PSM_Z24 || t->psm == GS_PSM_CT24 || t->psm == GS_PSM_Z16 || t->psm == GS_PSM_Z16S; };
                Surface *newest = nullptr;
                for (auto &p : surfaces)
                    if (p->gpuNewer && overlaps(b0, b1, p->b0, p->b1) && !(highByte && is24(p.get())) &&
                        (!newest || p->writeSeq > newest->writeSeq))
                        newest = p.get();
                if (newest && mappedRead(b, newest, target, out))
                    return true;
                why = WhyTextureRead, download({s});
            }
            continue;
        }
        upload(*s);
        const uint32_t page = (tex.tbp0 - s->block) / 32u;
        out.push.texOffset[0] = static_cast<float>((page % s->fbw) * 64u);
        out.push.texOffset[1] = static_cast<float>((page / s->fbw) * 32u);
        out.push.flags |= 16u | (s->depth ? 128u : 0u);
        out.push.depthScale = static_cast<float>(depthRange(s->psm));
        if (tex.psm == GS_PSM_CT24 || tex.psm == GS_PSM_Z24)
            out.push.texa = state.texa.ta0 | (state.texa.aem ? 256u : 0u) | 512u;
        Image *image = &s->image;
        if (s == target || s == out.depth)
        {
            // Feedback (bloom): the draw reads memory as it was before it, so it samples the shadow.
            ++owner.m_stats.feedback;
            feedbackRead = true;
            if (!s->shadow.image)
                s->shadow = createImage(s->image.w, s->image.h, s->image.format, 0), s->shadowFull = false;
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            readArea(b, out.push.texOffset, x0, y0, x1, y1);
            if (!s->shadowServes(x0, y0, x1, y1))
                refreshShadow(*s);
            feedbackSurface = s;
            feedbackOffset[0] = out.push.texOffset[0], feedbackOffset[1] = out.push.texOffset[1];
            image = &s->shadow;
        }
        out.set = image->set;
        return true;
    }

    // Decoded from the mirror, cached by everything that changes the texels.
    uint64_t key = (uint64_t(tex.tbp0) << 40) ^ (uint64_t(tex.tbw) << 34) ^ (uint64_t(tex.psm) << 28) ^ (uint64_t(tex.tw) << 24) ^
                   (uint64_t(tex.th) << 20);
    const bool paletted = isPaletted(tex.psm);
    if (paletted)
    {
        key = (key ^ paletteFor(state).hash) * 0x100000001B3ull;
    }
    else
        key = (key ^ (uint64_t(state.texa.ta0) | (uint64_t(state.texa.ta1) << 8) | (uint64_t(state.texa.aem) << 16))) * 0x100000001B3ull;
    if (!partialRead)
        rx0 = 0u, ry0 = 0u, rx1 = w, ry1 = h;
    else
        key = (key ^ (uint64_t(rx0) | uint64_t(ry0) << 16 | uint64_t(rx1) << 32 | uint64_t(ry1) << 48)) * 0x100000001B3ull;
    // Mipmap per pixel (TEX1 bit 10, set by the frontend): levels 1..MXL from MIPTBP1/2, same
    // format and palette, go in an atlas below level 0 (level n at x = widths of 1..n-1); the
    // shader picks the level from Q as the GS does. Before, one level per primitive (N04).
    struct MipLevel
    {
        uint32_t tbp, tbw, w, h, x, y;
    };
    std::array<MipLevel, 7> levels{};
    levels[0] = {tex.tbp0, tex.tbw, w, h, 0u, 0u};
    uint32_t levelCount = 1u;
    const uint64_t tex1 = state.context.tex1;
    if ((tex1 & (1ull << 10)) != 0u && !partialRead)
    {
        const uint32_t mxl = std::min<uint32_t>(static_cast<uint32_t>((tex1 >> 2) & 7u), 6u);
        for (uint32_t n = 1u, x = 0u; n <= mxl; ++n)
        {
            const uint64_t reg = n <= 3u ? state.context.miptbp1 : state.context.miptbp2;
            const uint32_t shift = ((n - 1u) % 3u) * 20u;
            const uint32_t tbp = static_cast<uint32_t>((reg >> shift) & 0x3FFFu), tbw = static_cast<uint32_t>((reg >> (shift + 14u)) & 0x3Fu);
            if (tbp == 0u || tbw == 0u)
                break;
            const uint32_t lw = std::max(w >> n, 1u), lh = std::max(h >> n, 1u);
            // MKSM leaves MIPTBP of levels it never reaches pointing at render targets: reading
            // them meant a GPU readback per texture (cutscene 50 -> 31 frames/s). Stop there.
            uint32_t l0 = 0u, l1 = 0u;
            blockRange(tbp, tbw, tex.psm, lw, lh, l0, l1);
            bool onTarget = false;
            for (auto &sp : surfaces)
                onTarget = onTarget || overlaps(l0, l1, sp->b0, sp->b1);
            if (onTarget)
                break;
            levels[levelCount++] = {tbp, tbw, lw, lh, x, h};
            x += lw;
        }
    }
    if (levelCount > 1u)
    {
        key = (key ^ (state.context.miptbp1 * 0x9E3779B97F4A7C15ull) ^ (state.context.miptbp2 << 7) ^ levelCount) * 0x100000001B3ull;
        out.push.flags |= 32768u;
        const uint32_t k12 = static_cast<uint32_t>(tex1 >> 32) & 0xFFFu;
        out.push.mip = (levelCount - 1u) | (static_cast<uint32_t>((tex1 >> 6) & 7u) << 3) | (static_cast<uint32_t>((tex1 >> 5) & 1u) << 6) |
                       (static_cast<uint32_t>((tex1 >> 19) & 3u) << 7) | (static_cast<uint32_t>(tex1 & 1u) << 9) | (k12 << 12);
        for (uint32_t n = 1u; n < levelCount; ++n)
        {
            uint32_t l0 = 0u, l1 = 0u;
            blockRange(levels[n].tbp, levels[n].tbw, tex.psm, levels[n].w, levels[n].h, l0, l1);
            b0 = std::min(b0, l0), b1 = std::max(b1, l1);
        }
    }
    // Every block the texels come from, mip levels included.
    const auto forEachTexBlock = [&](auto &&fn)
    {
        forEachBlock(tex.tbp0, tex.tbw, tex.psm, rx0, ry0, rx1 - rx0, ry1 - ry0, fn);
        for (uint32_t n = 1u; n < levelCount; ++n)
            forEachBlock(levels[n].tbp, levels[n].tbw, tex.psm, 0u, 0u, levels[n].w, levels[n].h, fn);
    };
    const uint32_t imageH = levelCount > 1u ? h + levels[1].h : h;
    if (auto it = textures.find(key); it != textures.end())
    {
        if (auto c = contentCache.find(it->second.content); c != contentCache.end())
        {
            it->second.lastUse = c->second.lastUse = ++useCounter;
            out.set = c->second.image.set;
            return true;
        }
        textures.erase(it);
    }
    syncIfPending(b0, b1);
    {
        std::vector<Surface *> stale;
        for (auto &p : surfaces)
        {
            if (!p->gpuNewer || !overlaps(b0, b1, p->b0, p->b1))
                continue;
            bool current = true;
            forEachTexBlock([&](uint32_t blk) {
                if (!blockCurrent(*p, blk))
                    current = false;
            });
            if (!current)
                stale.push_back(p.get());
        }
        why = WhyDecode;
        download(stale);
    }
    if (textures.size() > 4000u)
        textures.clear();
    TexEntry entry;
    entry.b0 = b0, entry.b1 = b1;
    entry.lastUse = ++useCounter;
    {
        std::vector<uint8_t> seen(kVramBlocks, 0u);
        forEachTexBlock([&](uint32_t blk) {
            if (!seen[blk])
                seen[blk] = 1u, entry.blocks.push_back(static_cast<uint16_t>(blk));
        });
    }
    uint64_t content = (key ^ 0x9E3779B97F4A7C15ull) * 0xC2B2AE3D27D4EB4Full;
    for (uint16_t blk : entry.blocks)
    {
        const auto *words = reinterpret_cast<const uint64_t *>(vram + size_t(blk) * 256u);
        for (uint32_t i = 0; i < 32u; ++i)
            content = (content ^ words[i]) * 0x100000001B3ull, content ^= content >> 29;
    }
    entry.content = content;
    if (auto found = contentCache.find(content); found != contentCache.end())
    {
        found->second.lastUse = ++useCounter;
        out.set = found->second.image.set;
        textures.emplace(key, std::move(entry));
        return true;
    }
    if (contentCache.size() > 3000u || contentBytes + uint64_t(w) * imageH * 4u > kContentBudget)
    {
        // Drop the least recently used down to 3/4 of both limits (textures entries that point
        // at a dropped one miss on their next lookup).
        std::vector<std::pair<uint64_t, uint64_t>> ages;
        for (auto &c : contentCache)
            ages.emplace_back(c.second.lastUse, c.first);
        std::sort(ages.begin(), ages.end());
        for (size_t i = 0; i < ages.size() && (contentCache.size() > 2250u || contentBytes > kContentBudget / 4u * 3u); ++i)
        {
            auto dropped = contentCache.find(ages[i].second);
            contentBytes -= textureBytes(dropped->second.image);
            recycle(dropped->second.image);
            contentCache.erase(dropped);
        }
    }
    VkDeviceSize offset = 0;
    uint8_t *dst = stage(VkDeviceSize(w) * imageH * 4u, offset);
    if (!dst)
        return false;
    auto *texels = reinterpret_cast<uint32_t *>(dst);
    if (partialRead || levelCount > 1u) // texels outside the sampled region (or the levels) are never read
        std::memset(texels, 0, size_t(w) * imageH * 4u);
    // A row at a time through GSMem::ReadRow (the read per texel was ~5% of the GS thread at 2x),
    // decoded in ordinary memory: the staging buffer is write-combined, reading it back is slow.
    const uint32_t *colors = paletted ? paletteFor(state).colors.data() : nullptr;
    thread_local std::vector<uint32_t> rowBuffer;
    for (uint32_t level = 0u; level < levelCount; ++level)
    {
    const MipLevel &lv = levels[level];
    const uint32_t lx0 = level == 0u ? rx0 : 0u, ly0 = level == 0u ? ry0 : 0u;
    const uint32_t lx1 = level == 0u ? rx1 : lv.w, ly1 = level == 0u ? ry1 : lv.h;
    const uint32_t n = lx1 > lx0 ? lx1 - lx0 : 0u;
    rowBuffer.resize(n);
    uint32_t *row = rowBuffer.data();
    for (uint32_t y = ly0; y < ly1 && n != 0u; ++y)
    {
        switch (tex.psm)
        {
        case GS_PSM_T4: case GS_PSM_T4HL: case GS_PSM_T4HH: case GS_PSM_T8: case GS_PSM_T8H:
        {
            GSMem::ReadRow(vram, tex.psm, lv.tbp, lv.tbw, lx0, y, row, n);
            const uint32_t mask = isFourBit(tex.psm) ? 0xFu : 0xFFu;
            for (uint32_t i = 0; i < n; ++i)
                row[i] = colors[row[i] & mask];
            break;
        }
        case GS_PSM_CT32: case GS_PSM_Z32:
            GSMem::ReadRow(vram, tex.psm, lv.tbp, lv.tbw, lx0, y, row, n);
            break;
        case GS_PSM_CT24: case GS_PSM_Z24:
            GSMem::ReadRow(vram, tex.psm, lv.tbp, lv.tbw, lx0, y, row, n);
            for (uint32_t i = 0; i < n; ++i)
                row[i] = applyTexa(state.texa, tex.psm, row[i]);
            break;
        case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16:
            GSMem::ReadRow(vram, tex.psm, lv.tbp, lv.tbw, lx0, y, row, n);
            for (uint32_t i = 0; i < n; ++i)
                row[i] = expand5551(row[i], state.texa);
            break;
        default:
            std::fill(row, row + n, 0xFFFF00FFu);
            break;
        }
        std::memcpy(texels + size_t(lv.y + y) * w + lv.x + lx0, row, size_t(n) * 4u);
    }
    }
    ContentEntry decoded;
    decoded.image = textureImage(w, imageH);
    decoded.lastUse = useCounter;
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, imageH, 1};
    endPass();
    gpuKind(GkUpload);
    vkCmdCopyBufferToImage(cmd(), slots[slot].staging.buffer, decoded.image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    fullBarrier();
    out.set = decoded.image.set;
    contentBytes += textureBytes(decoded.image);
    contentCache.emplace(content, decoded);
    textures.emplace(key, std::move(entry));
    return true;
}

namespace
{
    // GS blend (A - B) * C + D with A, B, D in {Cs, Cd, 0} and C in {As, Ad, FIX} as a fixed
    // function blend. As comes from the second fragment output, so FBA does not disturb it.
    void mapBlend(uint64_t alpha, PipeKey &k)
    {
        const uint32_t A = alpha & 3u, B = (alpha >> 2) & 3u, C = (alpha >> 4) & 3u, D = (alpha >> 6) & 3u;
        const VkBlendFactor F = C == 0u ? VK_BLEND_FACTOR_SRC1_ALPHA : C == 1u ? VK_BLEND_FACTOR_DST_ALPHA : VK_BLEND_FACTOR_CONSTANT_ALPHA;
        const VkBlendFactor F1 = C == 0u ? VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
                               : C == 1u ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA
                                         : VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        auto set = [&](VkBlendFactor s, VkBlendFactor d, VkBlendOp op) { k.blend = 1, k.src = s, k.dst = d, k.op = op; };
        constexpr uint32_t Cs = 0, Cd = 1, Z = 2;
        const VkBlendFactor ONE = VK_BLEND_FACTOR_ONE, ZERO = VK_BLEND_FACTOR_ZERO;
        const VkBlendOp ADD = VK_BLEND_OP_ADD, SUB = VK_BLEND_OP_SUBTRACT, RSUB = VK_BLEND_OP_REVERSE_SUBTRACT;
        if (A == B)
            return D == Cs ? set(ONE, ZERO, ADD) : D == Cd ? set(ZERO, ONE, ADD) : set(ZERO, ZERO, ADD);
        // shortcut: (1 + F) terms have no fixed function form; they keep the plain operand.
        if (A == Cs && B == Cd) return D == Cd ? set(F, F1, ADD) : D == Cs ? set(ONE, ZERO, ADD) : set(F, F, SUB);
        if (A == Cs && B == Z) return D == Cd ? set(F, ONE, ADD) : D == Cs ? set(ONE, ZERO, ADD) : set(F, ZERO, ADD);
        if (A == Cd && B == Cs) return D == Cs ? set(F1, F, ADD) : D == Cd ? set(ZERO, ONE, ADD) : set(F, F, RSUB);
        if (A == Cd && B == Z) return D == Cs ? set(ONE, F, ADD) : D == Cd ? set(ZERO, ONE, ADD) : set(ZERO, F, ADD);
        if (A == Z && B == Cs) return D == Cs ? set(F1, ZERO, ADD) : D == Cd ? set(F, ONE, RSUB) : set(ZERO, ZERO, ADD);
        if (A == Z && B == Cd) return D == Cs ? set(ONE, F, SUB) : D == Cd ? set(ZERO, F1, ADD) : set(ZERO, ZERO, ADD);
        set(ONE, ZERO, ADD);
    }
}

bool GSHwBackend::Renderer::draw(const GSPrimitiveBatch &b)
{
    const GSDrawState &state = b.state;
    const bool same = batchCount != 0u && batch.color && lastDrawGeneration == generation && lastDrawCount == b.vertexCount &&
                      (!(state.prim.tme && isPaletted(state.context.tex0.psm)) || lastDrawClutSerial == clutSerial) &&
                      !feedbackLast && std::memcmp(&lastDrawState, &state, sizeof(state)) == 0 && (!feedbackSurface || feedbackServed(b)) && (!dateSurface || dateServed(b));
    // shortcut: after a partial read (partialRead) the next primitive with the same state reuses
    // that decode even if it samples another region: MKSM's 4x7 grid of 32x32 tiles out of the
    // 1024x1024 texture at 0x2580 gets tiles 2-28 from zeros. The field pages come out identical
    // (bench_cutscene), and decoding each tile cost ~4 GPU readbacks per frame (2.9 -> 6.3 s);
    // fix it if that grid ever shows on screen.
    if (!same)
    {
        if (!drawState(b))
            return false;
        std::memcpy(&lastDrawState, &state, sizeof(state));
        lastDrawGeneration = generation, lastDrawClutSerial = clutSerial;
        lastDrawCount = b.vertexCount;
    }
    appendVertices(b);
    return true;
}

// Texels a primitive reads, in GS pixels of the surface it samples: exact for point sampling
// (texel floor(u) for u in [u0, u1)), one more on each side when filtered.
static void readArea(const GSPrimitiveBatch &b, const float offset[2], int &x0, int &y0, int &x1, int &y1)
{
    const GSDrawState &state = b.state;
    float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
    for (uint32_t i = 0; i < b.vertexCount; ++i)
    {
        const GSVertex &v = b.vertices[i];
        const float q = std::fabs(v.q) > 1e-12f ? std::fabs(v.q) : 1.0f;
        const float u = state.prim.fst ? v.u / 16.0f : v.s / q * state.textureWidth;
        const float vv = state.prim.fst ? v.v / 16.0f : v.t / q * state.textureHeight;
        u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, vv), v1 = std::max(v1, vv);
    }
    const int reach = state.linearFilter ? 1 : 0;
    x0 = static_cast<int>(std::floor(u0 + offset[0])) - reach, y0 = static_cast<int>(std::floor(v0 + offset[1])) - reach;
    x1 = std::max(x0, static_cast<int>(std::ceil(u1 + offset[0])) - 1 + reach);
    y1 = std::max(y0, static_cast<int>(std::ceil(v1 + offset[1])) - 1 + reach);
}

bool GSHwBackend::Renderer::feedbackServed(const GSPrimitiveBatch &b) const
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    readArea(b, feedbackOffset, x0, y0, x1, y1);
    return feedbackSurface->shadowServes(x0, y0, x1, y1);
}

// Pixels a primitive may cover (its vertex box), in GS pixels of the target.
static void drawArea(const GSPrimitiveBatch &b, int &x0, int &y0, int &x1, int &y1)
{
    const GSContext &ctx = b.state.context;
    const float ofx = static_cast<float>(ctx.xyoffset.ofx >> 4), ofy = static_cast<float>(ctx.xyoffset.ofy >> 4);
    float fx0 = 1e9f, fy0 = 1e9f, fx1 = -1e9f, fy1 = -1e9f;
    for (uint32_t i = 0; i < b.vertexCount; ++i)
    {
        const float x = b.vertices[i].x - ofx, y = b.vertices[i].y - ofy;
        fx0 = std::min(fx0, x), fx1 = std::max(fx1, x), fy0 = std::min(fy0, y), fy1 = std::max(fy1, y);
    }
    x0 = static_cast<int>(std::floor(fx0)), y0 = static_cast<int>(std::floor(fy0));
    x1 = static_cast<int>(std::ceil(fx1)), y1 = static_cast<int>(std::ceil(fy1));
}

bool GSHwBackend::Renderer::dateServed(const GSPrimitiveBatch &b) const
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    drawArea(b, x0, y0, x1, y1);
    return dateSurface->shadowServes(x0, y0, x1, y1);
}

bool GSHwBackend::Renderer::drawState(const GSPrimitiveBatch &b)
{
    feedbackSurface = nullptr;
    dateSurface = nullptr;
    const GSDrawState &state = b.state;
    const GSContext &ctx = state.context;
    const bool triangle = b.vertexCount == 3u &&
                          (state.prim.type == GS_PRIM_TRIANGLE || state.prim.type == GS_PRIM_TRISTRIP || state.prim.type == GS_PRIM_TRIFAN);
    const bool sprite = b.vertexCount == 2u && state.prim.type == GS_PRIM_SPRITE;
    const bool zte = (ctx.test >> 16) & 1u;
    const uint32_t ztst = static_cast<uint32_t>((ctx.test >> 17) & 3u);
    const bool needDepth = zte && !(ztst == 1u && ctx.zbuf.zmask);
    if ((!triangle && !sprite) || !isColorTargetPsm(ctx.frame.psm) || (needDepth && !isDepthPsm(ctx.zbuf.psm)))
        return false;
    if (zte && ztst == 0u)
        return true; // depth test NEVER: nothing to draw

    const uint32_t frameBlock = GSInternal::framePageBaseToBlock(ctx.frame.fbp);
    const uint32_t depthBlock = GSInternal::framePageBaseToBlock(ctx.zbuf.zbp);
    const uint32_t height = static_cast<uint32_t>(ctx.scissor.y1) + 1u;
    Surface *color = surface(frameBlock, ctx.frame.fbw, ctx.frame.psm, false, height);
    Surface *depth = needDepth ? surface(depthBlock, ctx.frame.fbw, ctx.zbuf.psm, true, height) : nullptr;
    if (needDepth)
    {
        // The depth surface may have evicted an overlapping colour one: look it up again.
        color = surface(frameBlock, ctx.frame.fbw, ctx.frame.psm, false, height);
        if (!depth || std::find_if(surfaces.begin(), surfaces.end(), [depth](auto &p) { return p.get() == depth; }) == surfaces.end())
            return false;
    }
    upload(*color);
    if (depth)
        upload(*depth);

    BatchState next;
    next.color = color;
    next.depth = depth;
    next.set = dummy.set;
    next.set1 = defaultMapSet;
    next.push.targetSize[0] = static_cast<float>(color->w);
    next.push.targetSize[1] = static_cast<float>(color->h);
    next.push.flags = (state.prim.tme ? 1u : 0u) | (state.prim.fst ? 2u : 0u) | (state.linearFilter ? 4u : 0u) | (state.prim.fge ? 8u : 0u) |
                      ((ctx.fba & 1u) ? 32u : 0u);
    next.push.tfxTcc = ctx.tex0.tfx | (ctx.tex0.tcc ? 4u : 0u);
    const bool ate = ctx.test & 1u;
    const uint32_t atst = ate ? static_cast<uint32_t>((ctx.test >> 1) & 7u) : 1u;
    next.push.alphaTest = atst | (static_cast<uint32_t>((ctx.test >> 4) & 0xFFu) << 8);
    next.push.fogColor = state.fogR | (uint32_t(state.fogG) << 8) | (uint32_t(state.fogB) << 16);
    next.push.wrap = static_cast<uint32_t>(ctx.clamp & 0xFu);
    next.push.clampU = static_cast<uint32_t>((ctx.clamp >> 4) & 0x3FFu) | (static_cast<uint32_t>((ctx.clamp >> 14) & 0x3FFu) << 16);
    next.push.clampV = static_cast<uint32_t>((ctx.clamp >> 24) & 0x3FFu) | (static_cast<uint32_t>((ctx.clamp >> 34) & 0x3FFu) << 16);
    next.push.scale = static_cast<float>(scale);
    if (state.prim.tme && !resolveTexture(b, color, next))
        return false;
    // DATE: pass where the destination alpha's bit 7 equals DATM, read from the target's shadow
    // (the memory as it was before the primitive). PS2X_GS_HW_DATE=0 ignores it, as before.
    static const bool dateOn = [] { const char *v = std::getenv("PS2X_GS_HW_DATE"); return !(v && v[0] == '0'); }();
    if (dateOn && ((ctx.test >> 14) & 1u) && next.set1 == defaultMapSet && color->psm != GS_PSM_CT24 && !color->depth)
    {
        if (!color->shadow.image)
            color->shadow = createImage(color->image.w, color->image.h, color->image.format, 0), color->shadowFull = false;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        drawArea(b, x0, y0, x1, y1);
        if (!color->shadowServes(x0, y0, x1, y1))
            refreshShadow(*color);
        next.set1 = mapSet(dummyMap.view, dummyPalette.view, color->shadow.view);
        next.push.flags |= 8192u | (((ctx.test >> 15) & 1u) ? 16384u : 0u);
        dateSurface = color;
    }

    PipeKey k;
    k.hasDepth = depth ? 1u : 0u;
    k.depthOp = !zte ? VK_COMPARE_OP_ALWAYS : ztst == 1u ? VK_COMPARE_OP_ALWAYS : ztst == 2u ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_GREATER;
    k.depthWrite = (zte && !ctx.zbuf.zmask) ? 1u : 0u;
    uint32_t mask = 0xFu;
    const uint32_t fbmsk = ctx.frame.fbmsk;
    for (uint32_t c = 0; c < 4u; ++c)
        if (((fbmsk >> (c * 8u)) & 0xFFu) == 0xFFu)
            mask &= ~(1u << c);
    if (ctx.frame.psm == GS_PSM_CT24 || ctx.frame.psm == GS_PSM_Z24)
        mask &= 0x7u;
    k.mask = mask;
    if (state.prim.abe)
        mapBlend(ctx.alpha, k);
    next.blendConstant = static_cast<float>((ctx.alpha >> 32) & 0xFFu) / 128.0f;
    next.pipeline = k.pack();
    const uint32_t afail = static_cast<uint32_t>((ctx.test >> 12) & 3u);
    if (ate && atst != 1u && afail != 0u)
    {
        PipeKey k2 = k;
        k2.depthWrite = afail == 2u ? k.depthWrite : 0u;
        k2.mask = afail == 1u ? k.mask : afail == 2u ? 0u : (k.mask & 0x7u);
        next.pipeline2 = k2.pack();
    }
    const int sx0 = ctx.scissor.x0, sy0 = ctx.scissor.y0;
    next.scissor.offset = {sx0 * int(scale), sy0 * int(scale)};
    next.scissor.extent = {uint32_t(std::max(0, ctx.scissor.x1 - sx0 + 1)) * scale, uint32_t(std::max(0, ctx.scissor.y1 - sy0 + 1)) * scale};

    if (!(next == batch))
    {
        flushBatch();
        batch = next;
    }
    return true;
}

void GSHwBackend::Renderer::appendVertices(const GSPrimitiveBatch &b)
{
    const GSDrawState &state = b.state;
    const GSContext &ctx = state.context;
    const bool triangle = b.vertexCount == 3u;
    // Vertices.
    const float ofx = static_cast<float>(ctx.xyoffset.ofx >> 4), ofy = static_cast<float>(ctx.xyoffset.ofy >> 4);
    const double zRange = depthRange(ctx.zbuf.psm);
    // Each vertex is written once, in place, with its depth computed once (the triangle path
    // computed it twice and the sprite path four times; appendVertices was ~17% of the GS worker).
    const auto put = [](HwVertex &o, const GSVertex &attr, float x, float y, float z, float s, float t, float q) {
        o.pos[0] = x, o.pos[1] = y, o.pos[2] = z, o.pos[3] = q;
        o.color[0] = attr.r, o.color[1] = attr.g, o.color[2] = attr.b, o.color[3] = attr.a;
        o.tex[0] = s, o.tex[1] = t, o.tex[2] = attr.fog, o.tex[3] = 0.0f;
    };
    HwVertex verts[6];
    uint32_t n = 0;
    if (triangle)
    {
        for (uint32_t i = 0; i < 3u; ++i)
        {
            const GSVertex &v = b.vertices[i];
            const GSVertex &attr = state.prim.iip ? v : b.vertices[2];
            put(verts[i], attr, v.x - ofx, v.y - ofy, static_cast<float>(std::clamp(v.z / zRange, 0.0, 1.0)),
                state.prim.fst ? v.u / 16.0f : v.s, state.prim.fst ? v.v / 16.0f : v.t, state.prim.fst ? 1.0f : v.q);
        }
        n = 3;
    }
    else
    {
        const GSVertex &v0 = b.vertices[0], &v1 = b.vertices[1];
        const float x0 = std::floor(v0.x) - ofx, y0 = std::floor(v0.y) - ofy, x1 = std::floor(v1.x) - ofx, y1 = std::floor(v1.y) - ofy;
        float s0, t0, s1, t1;
        if (state.prim.fst)
            s0 = v0.u / 16.0f, t0 = v0.v / 16.0f, s1 = v1.u / 16.0f, t1 = v1.v / 16.0f;
        else
        {
            const float q0 = std::fabs(v0.q) > 1e-12f ? v0.q : 1.0f, q1 = std::fabs(v1.q) > 1e-12f ? v1.q : 1.0f;
            s0 = v0.s / q0, t0 = v0.t / q0, s1 = v1.s / q1, t1 = v1.t / q1;
        }
        const float z = static_cast<float>(std::clamp(v1.z / zRange, 0.0, 1.0));
        put(verts[0], v1, x0, y0, z, s0, t0, 1.0f);
        put(verts[1], v1, x1, y0, z, s1, t0, 1.0f);
        put(verts[2], v1, x0, y1, z, s0, t1, 1.0f);
        put(verts[4], v1, x1, y1, z, s1, t1, 1.0f);
        verts[3] = verts[1], verts[5] = verts[2];
        n = 6;
    }
    {
        float x0 = verts[0].pos[0], y0 = verts[0].pos[1], x1 = x0, y1 = y0;
        for (uint32_t i = 1; i < n; ++i)
            x0 = std::min(x0, verts[i].pos[0]), x1 = std::max(x1, verts[i].pos[0]), y0 = std::min(y0, verts[i].pos[1]), y1 = std::max(y1, verts[i].pos[1]);
        const int ix0 = std::max(int(std::floor(x0)) - 1, int(ctx.scissor.x0)), iy0 = std::max(int(std::floor(y0)) - 1, int(ctx.scissor.y0));
        const int ix1 = std::min(int(std::ceil(x1)) + 1, int(ctx.scissor.x1)), iy1 = std::min(int(std::ceil(y1)) + 1, int(ctx.scissor.y1));
        if (ix1 >= ix0 && iy1 >= iy0 && batch.color)
        {
            batch.color->markDrawn(ix0, iy0, ix1, iy1);
            if (batch.depth && PipeKey::unpack(batch.pipeline).depthWrite)
                batch.depth->markDrawn(ix0, iy0, ix1, iy1);
            // Exact pixels for the shadow: a sprite covers [x0, x1); a triangle gets its whole box.
            const int edge = n == 6u ? 1 : 0;
            const int ex0 = std::max(int(std::floor(x0)), int(ctx.scissor.x0)), ey0 = std::max(int(std::floor(y0)), int(ctx.scissor.y0));
            const int ex1 = std::min(int(std::ceil(x1)) - edge, int(ctx.scissor.x1)), ey1 = std::min(int(std::ceil(y1)) - edge, int(ctx.scissor.y1));
            if (ex1 >= ex0 && ey1 >= ey0)
            {
                batch.color->noteDrawn(ex0, ey0, ex1, ey1);
                if (batch.depth && PipeKey::unpack(batch.pipeline).depthWrite)
                    batch.depth->noteDrawn(ex0, ey0, ex1, ey1);
            }
        }
    }
    cmd();
    Slot &s = slots[slot];
    if (s.vertexUsed + sizeof(verts) > kVertexBytes)
    {
        const BatchState keep = batch;
        submit(false);
        cmd();
        batch = keep;
    }
    Slot &cur = slots[slot];
    std::memcpy(static_cast<uint8_t *>(cur.vertices.mapped) + cur.vertexUsed, verts, n * sizeof(HwVertex));
    if (batchCount == 0u)
        batchFirst = cur.vertexUsed / sizeof(HwVertex);
    cur.vertexUsed += n * sizeof(HwVertex);
    batchCount += n;
}


namespace
{
    int64_t steadyMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // The frame page the display circuit shows (CRT1 first).
    uint32_t shownFramePage(const GSPresentationRequest &request)
    {
        return static_cast<uint32_t>(((request.pmode & 1u) ? request.dispfb1 : request.dispfb2) & 0x1FFu);
    }
}

// Records a copy of the display area of `sourceFbp` (~0u: the page the request shows) and
// publishes it once submitted. Called with the renderer lock held.
bool GSHwBackend::Renderer::record(const GSPresentationRequest &request, uint32_t sourceFbp)
{
    const auto failed = [this] {
        std::lock_guard<std::mutex> lock(presentMutex);
        presentFailed = true;
        return false;
    };
    refreshPending();
    const bool crt1 = request.pmode & 1u, crt2 = (request.pmode >> 1) & 1u;
    if (!crt1 && !crt2)
        return failed();
    const uint64_t dispfb = crt1 ? request.dispfb1 : request.dispfb2;
    const uint64_t display = crt1 ? request.display1 : request.display2;
    const uint32_t fbp = sourceFbp != ~0u ? sourceFbp : static_cast<uint32_t>(dispfb & 0x1FFu);
    const uint32_t fbw = static_cast<uint32_t>((dispfb >> 9) & 0x3Fu);
    const uint32_t ox = static_cast<uint32_t>((dispfb >> 32) & 0x7FFu), oy = static_cast<uint32_t>((dispfb >> 43) & 0x7FFu);
    uint32_t w = (static_cast<uint32_t>((display >> 32) & 0xFFFu) + 1u) / (static_cast<uint32_t>((display >> 23) & 0xFu) + 1u);
    uint32_t h = static_cast<uint32_t>((display >> 44) & 0x7FFu) + 1u;
    if (w < 64u || h < 64u)
        w = 640u, h = 448u;
    const bool doubleRows = (request.smode2 & 1u) && (request.smode2 & 2u) && h <= 256u;
    const uint32_t block = GSInternal::framePageBaseToBlock(fbp);
    Surface *src = nullptr;
    for (auto &p : surfaces)
        if (!p->depth && p->block == block && p->fbw == std::max(fbw, 1u))
            src = p.get();
    if (!src)
        return failed();
    flushBatch();
    upload(*src);
    w = std::min(w, src->w > ox ? src->w - ox : 0u);
    h = std::min(h, src->h > oy ? src->h - oy : 0u);
    if (w == 0u || h == 0u)
        return failed();

    // Overwrite the oldest copy that is neither the newest nor being read: the one before the
    // newest stays readable while the GPU, often a field or two behind, finishes the newest.
    int next = -1;
    {
        std::lock_guard<std::mutex> lock(presentMutex);
        for (int i = 0; i < kPresentCopies; ++i)
            if (i != published && i != reading && (next < 0 || presentCopies[i].seq < presentCopies[next].seq))
                next = i;
        presentCopies[next].seq = 0u; // not readable while recorded
    }
    PresentCopy &cur = presentCopies[next];
    const uint32_t pw = w * scale, ph = h * scale;
    if (cur.image.w != pw || cur.image.h != ph)
    {
        retire(cur.image);
        if (cur.buffer.buffer)
            submit(true), vkDestroyBuffer(device, cur.buffer.buffer, nullptr), vkFreeMemory(device, cur.buffer.memory, nullptr), cur.buffer = Buffer{};
        cur.image = createImage(pw, ph, VK_FORMAT_R8G8B8A8_UNORM, 0);
        createBuffer(cur.buffer, VkDeviceSize(pw) * ph * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    }
    VkCommandBuffer c = cmd();
    endPass();
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {static_cast<int32_t>(ox * scale), static_cast<int32_t>(oy * scale), 0};
    blit.srcOffsets[1] = {static_cast<int32_t>((ox + w) * scale), static_cast<int32_t>((oy + h) * scale), 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(pw), static_cast<int32_t>(ph), 1};
    gpuKind(GkPresent);
    vkCmdBlitImage(c, src->image.image, VK_IMAGE_LAYOUT_GENERAL, cur.image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
    fullBarrier();
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {pw, ph, 1};
    vkCmdCopyImageToBuffer(c, cur.image.image, VK_IMAGE_LAYOUT_GENERAL, cur.buffer.buffer, 1, &region);
    cur.slot = slot;
    submit(false);
    cur.serial = slots[cur.slot].serial;
    cur.w = pw, cur.h = ph, cur.doubleRows = doubleRows, cur.fbp = fbp;
    std::lock_guard<std::mutex> lock(presentMutex);
    cur.seq = ++publishedSeq;
    cur.recordedNs = ps2_perf::nowNs();
    published = next;
    presentFailed = false;
    return true;
}

// GS side, before each draw: MKSM draws one field per vsync, alternating frame pages 0x00 and
// 0x46 (bloom, depth copies and HUD in between); switching pages means the previous field is
// complete, so that is the copy the window gets.
void GSHwBackend::Renderer::noteDraw(uint32_t fbp)
{
    if ((fbp != 0x00u && fbp != 0x46u) || fbp == lastMainFbp)
        return;
    const uint32_t done = lastMainFbp;
    lastMainFbp = fbp;
    GSPresentationRequest geom;
    {
        std::lock_guard<std::mutex> lock(presentMutex);
        if (done == ~0u || !hasPresentGeom)
            return;
        geom = presentGeom;
    }
    const uint32_t shown = shownFramePage(geom);
    if (shown != 0x00u && shown != 0x46u)
        return; // the screen shows another page (menus): the window asks for its own copies
    if (record(geom, done))
        lastFieldCaptureMs.store(steadyMs(), std::memory_order_relaxed);
}

// The slot's fence, or the slot moving on (it is only reused after its fence), means done.
// shortcut: reads the GS side's serial and fence without the renderer lock, as the worker path did.
bool GSHwBackend::Renderer::copyDone(const PresentCopy &copy) const
{
    const Slot &s = slots[copy.slot];
    return __atomic_load_n(&s.serial, __ATOMIC_ACQUIRE) != copy.serial || vkGetFenceStatus(device, s.fence) == VK_SUCCESS;
}

// Window side: the next finished copy, or the last one shown while the GPU is still on it.
// One copy per window frame, oldest first: the window and the game tick on separate 60 Hz clocks,
// and taking only the newest showed a field twice and dropped the next one 2-5 times a second
// (57 of 60 game frames/s shown). Past two waiting it jumps to the newest, so the delay stays ~1 field;
// two waiting for half a second (the clocks drifted a field apart: 54 ms from field to window instead of
// 20-29) also jump once.
bool GSHwBackend::Renderer::takeNewest(PresentationFrame &out)
{
    int index = -1;
    {
        std::lock_guard<std::mutex> lock(presentMutex);
        int newest = -1, waiting = 0;
        for (int i = 0; i < kPresentCopies; ++i)
            if (presentCopies[i].seq > shownSeq && copyDone(presentCopies[i]))
            {
                ++waiting;
                if (index < 0 || presentCopies[i].seq < presentCopies[index].seq)
                    index = i;
                if (newest < 0 || presentCopies[i].seq > presentCopies[newest].seq)
                    newest = i;
            }
        backlogTicks = waiting >= 2 ? backlogTicks + 1 : 0;
        if (waiting > 2 || backlogTicks >= 30)
            index = newest, backlogTicks = 0;
        reading = index;
    }
    if (index >= 0)
    {
        const PresentCopy &copy = presentCopies[index];
        convertPresent(copy, shown);
        shownSeq = copy.seq;
        ps2_perf::counters().newImages.fetch_add(1u, std::memory_order_relaxed);
        if (ps2_perf::enabled())
            ps2_perf::counters().imageAgeNs.fetch_add(ps2_perf::nowNs() - copy.recordedNs, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(presentMutex);
        reading = -1;
    }
    if (shownSeq == 0u)
        return false;
    out = shown;
    return true;
}

void GSHwBackend::Renderer::convertPresent(const PresentCopy &ready, PresentationFrame &out) const
{
    const uint32_t outH = ready.doubleRows ? ready.h * 2u : ready.h;
    out.pixels.resize(size_t(ready.w) * outH * 4u);
    const auto *in = static_cast<const uint32_t *>(ready.buffer.mapped);
    auto *dst = reinterpret_cast<uint32_t *>(out.pixels.data());
    for (uint32_t y = 0; y < outH; ++y)
    {
        const uint32_t *row = in + size_t(ready.doubleRows ? y / 2u : y) * ready.w;
        uint32_t *o = dst + size_t(y) * ready.w;
        for (uint32_t x = 0; x < ready.w; ++x)
            o[x] = row[x] | 0xFF000000u; // the window blends with alpha: show the picture opaque
    }
    out.width = ready.w;
    out.height = outH;
    out.displayFbp = out.sourceFbp = ready.fbp;
}

void GSHwBackend::Renderer::fallback(const GSPrimitiveBatch &b)
{
    // Something the GPU path does not draw: let the CPU backend draw it into the mirror.
    ++owner.m_stats.fallback;
    // Only the surfaces the primitive reads or writes come back from the GPU, and the draw stays
    // queued on the CPU backend like a host transfer: its blocks are pending, and whatever reads
    // them from the mirror syncs first (syncIfPending, upload). The loading screens copy a CT32 page
    // the GPU draws into a CT16 one with thousands of sprites: three queue waits and every surface
    // coming back on each one kept the EE waiting on the GS, and the game clock crawled.
    const GSContext &ctx = b.state.context;
    const bool zWrite = !ctx.zbuf.zmask;
    const bool zRead = ((ctx.test >> 16) & 1u) && ((ctx.test >> 17) & 3u) >= 2u; // GEQUAL, GREATER
    uint32_t f0 = 0, f1 = 0, z0 = 0, z1 = 0;
    blockRange(GSInternal::framePageBaseToBlock(ctx.frame.fbp), ctx.frame.fbw, ctx.frame.psm, ctx.frame.fbw * 64u,
               static_cast<uint32_t>(ctx.scissor.y1) + 1u, f0, f1);
    blockRange(GSInternal::framePageBaseToBlock(ctx.zbuf.zbp), ctx.frame.fbw, ctx.zbuf.psm, ctx.frame.fbw * 64u,
               static_cast<uint32_t>(ctx.scissor.y1) + 1u, z0, z1);
    flushBatch();
    if (b.state.prim.tme && ((ctx.tex1 >> 2) & 7u) != 0u)
        downloadAll(); // mipmapped: the levels live elsewhere
    else
    {
        std::vector<Surface *> list;
        const auto need = [&](uint32_t r0, uint32_t r1) {
            for (auto &p : surfaces)
                if (p->gpuNewer && overlaps(r0, r1, p->b0, p->b1) && std::find(list.begin(), list.end(), p.get()) == list.end())
                    list.push_back(p.get());
        };
        need(f0, f1); // the CPU writes only some pixels: the rest of the frame must be current
        if (zWrite || zRead)
            need(z0, z1);
        if (b.state.prim.tme)
        {
            uint32_t t0 = 0, t1 = 0;
            blockRange(ctx.tex0.tbp0, std::max<uint32_t>(ctx.tex0.tbw, 1u), ctx.tex0.psm, 1u << std::min<uint32_t>(ctx.tex0.tw, 10u),
                       1u << std::min<uint32_t>(ctx.tex0.th, 10u), t0, t1);
            need(t0, t1);
        }
        download(list);
    }
    if (cpuClutSerial != clutSerial || cpuClutCbp[0] != clutCbp[0] || cpuClutCbp[1] != clutCbp[1])
    {
        cpu.SetClutTable(clut, clutCbp); // waits for the CPU queue: only when the CLUT changed
        cpuClutSerial = clutSerial, cpuClutCbp[0] = clutCbp[0], cpuClutCbp[1] = clutCbp[1];
    }
    cpu.Submit(b);
    innerDirty = true;
    const auto wrote = [&](uint32_t b0, uint32_t b1) {
        mirrorWrote(b0, b1);
        for (uint32_t blk = b0; blk < b1; ++blk)
            notePendingWrite(blk);
    };
    wrote(f0, f1);
    if (zWrite)
        wrote(z0, z1);
}
#endif // PS2X_HAVE_VULKAN

// ---------------------------------------------------------------- entry points

#if !defined(PS2X_HAVE_VULKAN)
struct GSHwBackend::Renderer
{
};
#endif

#if defined(PS2X_HAVE_VULKAN)
struct GSHwBackend::Async
{
    enum Kind : uint8_t
    {
        KSubmit,
        KLoadClut,
        KTransfer,
        KUpload,
        KFlush,
        KTextureFlush,
        KSync,
        KPresent
    };
    struct Command
    {
        Kind kind;
        uint32_t index; // into the matching side list
        GSPrimitiveBatch batch;
    };
    struct Work
    {
        std::vector<Command> commands;
        std::vector<std::pair<GSTex0Reg, GSTexClutReg>> cluts;
        std::vector<GSTransferCommand> transfers;
        std::vector<std::vector<uint8_t>> uploads;
        std::vector<GSSyncReason> syncs;
        std::vector<GSPresentationRequest> presents;
        void clear()
        {
            commands.clear(), cluts.clear(), transfers.clear(), uploads.clear(), syncs.clear(), presents.clear();
        }
    };
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    Work pending;
    uint64_t queued = 0, done = 0; // commands handed over / finished by the worker
    bool stop = false;
};

template <typename F>
void GSHwBackend::enqueue(F &&fill)
{
    Async &a = *m_async;
    constexpr size_t kMaxCommands = 65536u;
    std::unique_lock<std::mutex> lock(a.mutex);
    if (a.pending.commands.size() >= kMaxCommands)
        a.cv.wait(lock, [&a] { return a.pending.commands.size() < kMaxCommands; });
    fill(a.pending);
    ++a.queued;
    if (a.pending.commands.size() == 1u)
        a.cv.notify_all();
}

void GSHwBackend::waitIdle() const
{
    if (!m_async || std::this_thread::get_id() == m_async->thread.get_id())
        return;
    Async &a = *m_async;
    std::unique_lock<std::mutex> lock(a.mutex);
    const uint64_t target = a.queued; // what was queued before this call, not what comes later
    a.cv.wait(lock, [&a, target] { return a.done >= target; });
}

void GSHwBackend::workerLoop()
{
    pthread_setname_np(pthread_self(), "GSHwWorker"); // PS2X_PROFILE_THREAD picks threads by name
    Async &a = *m_async;
    Async::Work work;
    std::unique_lock<std::mutex> lock(a.mutex);
    for (;;)
    {
        a.cv.wait(lock, [&a] { return a.stop || !a.pending.commands.empty(); });
        if (a.pending.commands.empty())
            break;
        std::swap(work, a.pending);
        const uint64_t end = a.queued;
        lock.unlock();
        a.cv.notify_all(); // producers waiting for room
        const uint64_t busyStart = ps2_perf::enabled() ? ps2_perf::nowNs() : 0u;
        {
            std::lock_guard<std::recursive_mutex> render(m_lock);
            for (const Async::Command &c : work.commands)
                switch (c.kind)
                {
                case Async::KSubmit: submitNow(c.batch); break;
                case Async::KLoadClut: loadClutNow(work.cluts[c.index].first, work.cluts[c.index].second); break;
                case Async::KTransfer: beginTransferNow(work.transfers[c.index]); break;
                case Async::KUpload:
                    uploadImageNow(work.uploads[c.index].data(), static_cast<uint32_t>(work.uploads[c.index].size()));
                    break;
                case Async::KFlush: flushNow(); break;
                case Async::KTextureFlush: m_inner.TextureFlush(); break;
                case Async::KSync: syncNow(work.syncs[c.index]); break;
                case Async::KPresent: presentNow(work.presents[c.index]); break;
                }
        }
        work.clear();
        if (busyStart)
            ps2_perf::counters().hwWorkerBusyNs.fetch_add(ps2_perf::nowNs() - busyStart, std::memory_order_relaxed);
        lock.lock();
        a.done = end;
        a.cv.notify_all();
    }
}

void GSHwBackend::presentNow(const GSPresentationRequest &request) { m_renderer->record(request, ~0u); }
#else
struct GSHwBackend::Async
{
};
template <typename F>
void GSHwBackend::enqueue(F &&) {}
void GSHwBackend::waitIdle() const {}
void GSHwBackend::workerLoop() {}
void GSHwBackend::presentNow(const GSPresentationRequest &) {}
#endif

void GSHwBackend::Submit(const GSPrimitiveBatch &batch)
{
    if (m_async)
        return enqueue([&](Async::Work &w) {
            w.commands.push_back({Async::KSubmit, 0u, {}});
            // Byte for byte, padding too: draw() compares states with memcmp, and a member-wise
            // copy left garbage there (shortcut misses, run-to-run differences in what was drawn).
            std::memcpy(&w.commands.back().batch, &batch, sizeof(batch));
        });
    submitNow(batch);
}

void GSHwBackend::LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    if (m_async)
        return enqueue([&](Async::Work &w) {
            w.commands.push_back({Async::KLoadClut, static_cast<uint32_t>(w.cluts.size()), {}});
            w.cluts.emplace_back(tex0, texclut);
        });
    loadClutNow(tex0, texclut);
}

void GSHwBackend::BeginTransfer(const GSTransferCommand &command)
{
    if (m_async)
        return enqueue([&](Async::Work &w) {
            w.commands.push_back({Async::KTransfer, static_cast<uint32_t>(w.transfers.size()), {}});
            w.transfers.push_back(command);
        });
    beginTransferNow(command);
}

void GSHwBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes)
{
    if (m_async)
        return enqueue([&](Async::Work &w) {
            w.commands.push_back({Async::KUpload, static_cast<uint32_t>(w.uploads.size()), {}});
            w.uploads.emplace_back(data, data + sizeBytes);
        });
    uploadImageNow(data, sizeBytes);
}

bool GSHwBackend::onPresentThread() const
{
#if defined(PS2X_HAVE_VULKAN)
    return m_renderer && m_renderer->presentThread.load(std::memory_order_relaxed) == std::this_thread::get_id();
#else
    return false;
#endif
}

void GSHwBackend::Flush()
{
    if (onPresentThread())
        return; // the window's latch: Present takes copies without the renderer lock or the queue
    if (m_async)
        return enqueue([&](Async::Work &w) { w.commands.push_back({Async::KFlush, 0u, {}}); });
    flushNow();
}

void GSHwBackend::TextureFlush()
{
    if (m_async)
        return enqueue([&](Async::Work &w) { w.commands.push_back({Async::KTextureFlush, 0u, {}}); });
    m_inner.TextureFlush();
}

void GSHwBackend::Sync(GSSyncReason reason)
{
    if (reason == GSSyncReason::Presentation && onPresentThread())
        return; // see Flush
    // FINISH only flushes: nothing reads state back.
    if (m_async && (reason == GSSyncReason::Finish || reason == GSSyncReason::Presentation))
        return enqueue([&](Async::Work &w) {
            w.commands.push_back({Async::KSync, static_cast<uint32_t>(w.syncs.size()), {}});
            w.syncs.push_back(reason);
        });
    waitIdle();
    syncNow(reason);
}

GSHwBackend::GSHwBackend(GSRasterBackend &inner, Mode mode) : m_inner(inner), m_mode(mode)
{
#if defined(PS2X_HAVE_VULKAN)
    if (mode == Mode::Render)
        if (auto *cpu = dynamic_cast<GSCpuBackend *>(&inner))
        {
            cpu->DisableGpu();
            m_renderer = std::make_unique<Renderer>(*this, *cpu);
            if (!m_renderer->init())
            {
                m_renderer->destroy();
                m_renderer.reset();
            }
            // Decoding the GS and drawing overlap (+16-21% in the cutscene at 2x); the game runner
            // turns it on by default (main.cpp), tests and the replay tool do not.
            const char *threaded = std::getenv("PS2X_GS_HW_THREAD");
            if (m_renderer && threaded && threaded[0] == '1')
            {
                m_async = std::make_unique<Async>();
                m_async->thread = std::thread(&GSHwBackend::workerLoop, this);
            }
        }
#endif
}

GSHwBackend::~GSHwBackend()
{
    if (g_activeHw == this)
        g_activeHw = nullptr;
#if defined(PS2X_HAVE_VULKAN)
    if (m_async)
    {
        {
            std::lock_guard<std::mutex> lock(m_async->mutex);
            m_async->stop = true;
        }
        m_async->cv.notify_all();
        m_async->thread.join();
    }
    if (m_renderer)
        m_renderer->destroy();
#endif
}

bool GSHwBackend::ready() const { return m_mode == Mode::Count || m_renderer != nullptr; }

GSHwBackend *GSHwBackend::Active() { return g_activeHw; }

uint32_t GSHwBackend::Scale() const
{
#if defined(PS2X_HAVE_VULKAN)
    return m_renderer ? m_renderer->scale : 0u;
#else
    return 0u;
#endif
}

void GSHwBackend::SetScale(uint32_t scale)
{
#if defined(PS2X_HAVE_VULKAN)
    scale = std::clamp<uint32_t>(scale, 1u, 4u);
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    if (!m_renderer || m_renderer->scale == scale)
        return;
    m_renderer->downloadAll(); // what only the GPU has goes back to the mirror first
    invalidateAllLocked();
    m_renderer->scale = scale;
#else
    (void)scale;
#endif
}

static bool g_hwRendering = false;

bool gsMipPerPixel()
{
    static const bool enabled = [] { const char *v = std::getenv("PS2X_MIP_PIXEL"); return !(v && v[0] == '0'); }();
    return enabled; // both renderers sample the levels per pixel (the batching one since N04)
}

std::unique_ptr<GSRasterBackend> GSHwBackend::WrapFromEnvironment(std::unique_ptr<GSRasterBackend> inner)
{
    const char *mode = std::getenv("PS2X_GS_HW");
    if (!mode || !*mode || std::strcmp(mode, "0") == 0)
        return inner;
    const Mode m = std::strcmp(mode, "count") == 0 ? Mode::Count : Mode::Render;
    auto wrapper = std::make_unique<GSHwBackend>(*inner, m);
    if (!wrapper->ready())
    {
        std::fprintf(stderr, "[gs:hw] Vulkan renderer unavailable, keeping the current one\n");
        return inner;
    }
    std::fprintf(stderr, "[gs:hw] %s\n", m == Mode::Count ? "counting batches" : "batching GPU renderer");
    g_hwRendering = m == Mode::Render;
    if (g_hwRendering)
        g_activeHw = wrapper.get();
    wrapper->m_owned = std::move(inner);
    return wrapper;
}

#if defined(PS2X_HAVE_VULKAN)
#define HW (m_renderer)
#else
#define HW (static_cast<Renderer *>(nullptr))
#endif

void GSHwBackend::InvalidateAll()
{
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    invalidateAllLocked();
}

// Waiting for the queue with m_lock held would deadlock: the GSHwWorker needs the lock to drain it.
void GSHwBackend::invalidateAllLocked()
{
#if defined(PS2X_HAVE_VULKAN)
    if (!m_renderer)
        return;
    m_renderer->flushBatch();
    m_renderer->batch = Renderer::BatchState{};
    while (!m_renderer->surfaces.empty())
        m_renderer->destroySurface(m_renderer->surfaces.back().get());
    m_renderer->invalidateTextures(0u, kVramBlocks);
    m_renderer->clutCbp[0] = m_renderer->clutCbp[1] = ~0u;
#endif
}

void GSHwBackend::Initialize(uint8_t *vram, uint32_t vramSize)
{
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    m_inner.Initialize(vram, vramSize);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
    {
        m_renderer->vram = vram;
        invalidateAllLocked();
    }
#endif
}

void GSHwBackend::Reset()
{
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
        m_renderer->downloadAll(), invalidateAllLocked();
#endif
    m_inner.Reset();
}

void GSHwBackend::submitNow(const GSPrimitiveBatch &batch)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    if (m_mode == Mode::Count || !m_renderer)
    {
        countBatch(batch);
        m_inner.Submit(batch);
        return;
    }
#if defined(PS2X_HAVE_VULKAN)
    ++m_stats.primitives;
    // A FRAME with a pixel format the GS does not define (seen on MKSM's Wu Shi walls: PSM 0x12,
    // FBW 47, ~10k triangles a second) covers most of VRAM as a target: sent to the CPU, every
    // triangle brought every surface back from the GPU and the game crawled at 1 frame a second.
    // The real GS gives nothing useful for it either: draw nothing.
    switch (batch.state.context.frame.psm)
    {
    case GS_PSM_CT32: case GS_PSM_CT24: case GS_PSM_CT16: case GS_PSM_CT16S:
    case GS_PSM_Z32: case GS_PSM_Z24: case GS_PSM_Z16: case GS_PSM_Z16S:
        break;
    default:
        return;
    }
    m_renderer->noteDraw(batch.state.context.frame.fbp);
    if (!m_renderer->draw(batch))
        m_renderer->fallback(batch);
#endif
}

void GSHwBackend::loadClutNow(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
        return m_renderer->loadClut(tex0, texclut); // the CPU fallback gets this CLUT when it draws (SetClutTable)
#endif
    m_inner.LoadClut(tex0, texclut);
}

void GSHwBackend::beginTransferNow(const GSTransferCommand &command)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    m_open = false;
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
    {
        Renderer &r = *m_renderer;
        r.flushBatch();
        const GSBitBltBuf &buf = command.bitbltbuf;
        const uint32_t w = command.trxreg.rrw, h = command.trxreg.rrh;
        uint32_t s0 = 0, s1 = 0, d0 = 0, d1 = 0;
        blockRange(buf.sbp, buf.sbw, buf.spsm, command.trxpos.ssax + w, command.trxpos.ssay + h, s0, s1);
        blockRange(buf.dbp, buf.dbw, buf.dpsm, command.trxpos.dsax + w, command.trxpos.dsay + h, d0, d1);
        r.why = Renderer::WhyTransfer;
        // Host-to-local data lands in the mirror on this thread: draws that read it later don't
        // wait for the CPU backend's worker (~1400 waits/s in the stage). PS2X_GS_HW_INLINE_MIRROR=0
        // queues it as before.
        static const bool inlineMirror = [] {
            const char *setting = std::getenv("PS2X_GS_HW_INLINE_MIRROR");
            return !(setting && setting[0] == '0');
        }();
        r.inlineUpload = inlineMirror && command.direction == 0u;
        if (command.direction == 1u || command.direction == 2u)
            r.downloadRange(s0, s1);
        if (command.direction == 0u || command.direction == 2u)
        {
            std::vector<uint8_t> written(kVramBlocks, 0u);
            std::vector<uint32_t> blocks;
            uint32_t lo = kVramBlocks, hi = 0;
            const uint64_t seq = ++r.writeCounter;
            forEachBlock(buf.dbp, buf.dbw, buf.dpsm, command.trxpos.dsax, command.trxpos.dsay, w, h, [&](uint32_t blk) {
                r.blockTransferSeq[blk] = seq;
                if (!r.inlineUpload)
                    r.notePendingWrite(blk);
                if (!written[blk])
                    written[blk] = 1u, blocks.push_back(blk);
                lo = std::min(lo, blk), hi = std::max(hi, blk + 1u);
            });
            // A transfer that rewrites whole blocks needs only those blocks refreshed in the surface;
            // T8H/T4HL/T4HH and 24-bit formats keep the other bytes of each word.
            uint32_t bwPx = 8u, bhPx = 8u;
            switch (buf.dpsm)
            {
            case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S: bwPx = 16u; break;
            case GS_PSM_T8: bwPx = 16u, bhPx = 16u; break;
            case GS_PSM_T4: bwPx = 32u, bhPx = 16u; break;
            default: break;
            }
            const bool fullBlocks = command.direction == 0u && buf.dpsm != GS_PSM_T8H && buf.dpsm != GS_PSM_T4HL &&
                                    buf.dpsm != GS_PSM_T4HH && buf.dpsm != GS_PSM_CT24 && buf.dpsm != GS_PSM_Z24 &&
                                    command.trxpos.dsax % bwPx == 0u && command.trxpos.dsay % bhPx == 0u &&
                                    w % bwPx == 0u && h % bhPx == 0u;
            auto byBlocks = [](const Renderer::Surface *sp) {
                return !sp->depth && sp->gpuNewer && !sp->mirrorNewer &&
                       (sp->psm == GS_PSM_CT32 || sp->psm == GS_PSM_CT24 || sp->psm == GS_PSM_Z32 || sp->psm == GS_PSM_Z24);
            };
            std::vector<Renderer::Surface *> hit, whole;
            // A surface counts only if one of the blocks the transfer writes is inside it: the range
            // [d0, d1) runs from the base block to the end of the area, and MKSM streams ~200 textures
            // a field next to its depth buffer. Each overlap of a depth surface brought it all back
            // from the GPU (and sent it again for the next draw): the game crawled at 1 frame a second.
            for (auto &sp : r.surfaces)
                if (overlaps(lo, hi, sp->b0, sp->b1) &&
                    std::any_of(blocks.begin(), blocks.end(), [&](uint32_t blk) { return blk >= sp->b0 && blk < sp->b1; }))
                    (byBlocks(sp.get()) ? hit : whole).push_back(sp.get());
            // Whole copies first: download() applies queued block refreshes, which must not run
            // after this transfer's blocks are queued and before its data lands.
            r.download(whole); // the parts the transfer does not cover must survive
            for (Renderer::Surface *sp : whole)
                sp->mirrorNewer = true;
            // shortcut: a transfer covering part of a block also brings the block's other (stale
            // mirror) bytes back; reading them from the GPU cost a GPU wait per small texture
            // (~130/s in the stage). PS2X_GS_HW_EXACT_TRANSFER=1 reads them back.
            static const bool exact = std::getenv("PS2X_GS_HW_EXACT_TRANSFER") != nullptr;
            if (exact && !fullBlocks && !hit.empty())
                r.refreshPending(); // older transfers reach the surfaces before blocks are read back
            // Oldest first, so the newest writer of aliased memory wins in the mirror.
            std::sort(hit.begin(), hit.end(), [](const Renderer::Surface *x, const Renderer::Surface *y) { return x->writeSeq < y->writeSeq; });
            for (Renderer::Surface *sp : hit)
            {
                if (exact && !fullBlocks)
                    r.downloadBlocks(*sp, blocks); // the bytes the transfer leaves alone come from the GPU
                for (uint32_t blk : blocks)
                    if (blk >= sp->b0 && blk < sp->b1)
                        sp->pending.push_back(blk);
                if (sp->pending.size() > 4u * (sp->b1 - sp->b0)) // a surface nobody uses: repeats merge
                {
                    std::sort(sp->pending.begin(), sp->pending.end());
                    sp->pending.erase(std::unique(sp->pending.begin(), sp->pending.end()), sp->pending.end());
                }
            }
            if (lo < hi)
                r.invalidateTexturesExact(written, lo, hi);
        }
        if (!r.inlineUpload)
            r.innerDirty = true;
        if (r.inlineUpload)
        {
            r.cpu.BeginTransferNow(command);
            return;
        }
    }
#endif
    m_inner.BeginTransfer(command);
}

void GSHwBackend::uploadImageNow(const uint8_t *data, uint32_t sizeBytes)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer && m_renderer->inlineUpload)
    {
        m_renderer->cpu.UploadImageNow(data, sizeBytes);
        return;
    }
    if (m_renderer)
        m_renderer->innerDirty = true;
#endif
    m_inner.UploadImage(data, sizeBytes);
}

void GSHwBackend::flushNow()
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
        m_renderer->submit(false);
#endif
    m_inner.Flush();
}


void GSHwBackend::syncNow(GSSyncReason reason)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer && reason != GSSyncReason::Presentation && reason != GSSyncReason::Finish)
        m_renderer->downloadAll();
    else if (m_renderer)
        m_renderer->submit(false);
#endif
    m_inner.Sync(reason);
}

uint64_t gsGameFrameSerial(); // gs_frontend.cpp

PresentationFrame GSHwBackend::Present(const GSPresentationRequest &request)
{
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
    {
        Renderer &r = *m_renderer;
        r.presentThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
        bool failed = false;
        {
            std::lock_guard<std::mutex> lock(r.presentMutex);
            r.presentGeom = request, r.hasPresentGeom = true;
            failed = r.presentFailed;
        }
        ++m_stats.frames;
        if (m_stats.frames % 120u == 0u && std::getenv("PS2X_GS_HW_STATS"))
            if (std::unique_lock<std::recursive_mutex> lock(m_lock, std::try_to_lock); lock.owns_lock())
                report();
        // Without field copies (menus, a scene that does not flip pages) the window asks for one:
        // in stream order on the worker, or here if the GS side is not holding the renderer.
        const uint32_t shown = shownFramePage(request);
        const bool fields = (shown == 0x00u || shown == 0x46u) &&
                            steadyMs() - r.lastFieldCaptureMs.load(std::memory_order_relaxed) < 250;
        if (!fields && m_async)
            enqueue([&](Async::Work &w) {
                w.commands.push_back({Async::KPresent, static_cast<uint32_t>(w.presents.size()), {}});
                w.presents.push_back(request);
            });
        else if (!fields)
            if (std::unique_lock<std::recursive_mutex> lock(m_lock, std::try_to_lock); lock.owns_lock())
                failed = !r.record(request, ~0u);
        if ((request.pmode & 3u) == 3u) // both circuits: the picture is their blend (presentFallback)
        {
            --m_stats.frames;
            waitIdle();
            return presentFallback(request);
        }
        PresentationFrame frame;
        if (!failed && r.takeNewest(frame))
            return frame;
        --m_stats.frames;
        waitIdle(); // no display surface (movies, 512-wide menus): the mirror, in stream order
        return presentFallback(request);
    }
#endif
    { // PS2X_PERF "new images": the window shows a game frame it had not shown yet
        thread_local uint64_t shownSerial = 0u;
        if (const uint64_t serial = gsGameFrameSerial(); serial != shownSerial)
            shownSerial = serial, ps2_perf::counters().newImages.fetch_add(1u, std::memory_order_relaxed);
    }
    return presentFallback(request);
}

PresentationFrame GSHwBackend::presentFallback(const GSPresentationRequest &request)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    ++m_stats.frames;
    if (m_stats.frames % 120u == 0u && (m_mode == Mode::Count || std::getenv("PS2X_GS_HW_STATS")))
        report();
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
    {
        // No display surface (movies uploaded straight to VRAM, boot, 512-wide menus): present
        // the mirror, bringing back only the targets the enabled read circuits cover (all of
        // them, every window frame, was most of the menus' copies).
        m_renderer->flushBatch();
        for (int circuit = 0; circuit < 2; ++circuit)
        {
            if (!((request.pmode >> circuit) & 1u))
                continue;
            const uint64_t dispfb = circuit ? request.dispfb2 : request.dispfb1;
            const uint64_t display = circuit ? request.display2 : request.display1;
            const uint32_t fbp = static_cast<uint32_t>(dispfb & 0x1FFu), fbw = static_cast<uint32_t>((dispfb >> 9) & 0x3Fu);
            const uint32_t psm = static_cast<uint32_t>((dispfb >> 15) & 0x1Fu);
            const uint32_t ox = static_cast<uint32_t>((dispfb >> 32) & 0x7FFu), oy = static_cast<uint32_t>((dispfb >> 43) & 0x7FFu);
            uint32_t w = (static_cast<uint32_t>((display >> 32) & 0xFFFu) + 1u) / (static_cast<uint32_t>((display >> 23) & 0xFu) + 1u);
            uint32_t h = static_cast<uint32_t>((display >> 44) & 0x7FFu) + 1u;
            if (w < 64u || h < 64u)
                w = 640u, h = 448u;
            uint32_t b0 = 0, b1 = 0;
            blockRange(GSInternal::framePageBaseToBlock(fbp), std::max(fbw, 1u), psm, ox + w, oy + h, b0, b1);
            m_renderer->why = Renderer::WhyPresent;
            m_renderer->downloadRange(b0, b1);
        }
        m_renderer->syncInner();
        // The CPU backend only builds a picture from draws it ran itself (none here), so read the
        // circuits straight from the mirror (movies display their upload).
        const auto readCircuit = [&](int circuit) {
            const uint64_t dispfb = circuit ? request.dispfb2 : request.dispfb1;
            const uint64_t display = circuit ? request.display2 : request.display1;
            const uint32_t fbp = static_cast<uint32_t>(dispfb & 0x1FFu), fbw = std::max(static_cast<uint32_t>((dispfb >> 9) & 0x3Fu), 1u);
            const uint32_t psm = static_cast<uint32_t>((dispfb >> 15) & 0x1Fu);
            const uint32_t ox = static_cast<uint32_t>((dispfb >> 32) & 0x7FFu), oy = static_cast<uint32_t>((dispfb >> 43) & 0x7FFu);
            uint32_t w = (static_cast<uint32_t>((display >> 32) & 0xFFFu) + 1u) / (static_cast<uint32_t>((display >> 23) & 0xFu) + 1u);
            uint32_t h = static_cast<uint32_t>((display >> 44) & 0x7FFu) + 1u;
            if (w < 64u || h < 64u)
                w = 640u, h = 448u;
            w = std::min(w, 1024u), h = std::min(h, 1024u);
            const bool doubleRows = (request.smode2 & 1u) && (request.smode2 & 2u) && h <= 256u;
            const uint32_t block = GSInternal::framePageBaseToBlock(fbp);
            uint8_t *vram = m_renderer->vram;
            PresentationFrame frame;
            const uint32_t outH = doubleRows ? h * 2u : h;
            frame.pixels.resize(size_t(w) * outH * 4u);
            auto *out = reinterpret_cast<uint32_t *>(frame.pixels.data());
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                {
                    uint32_t c = 0;
                    if (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S)
                    {
                        const uint32_t v = psm == GS_PSM_CT16 ? GSMem::ReadCT16(vram, block, fbw, ox + x, oy + y)
                                                              : GSMem::ReadCT16S(vram, block, fbw, ox + x, oy + y);
                        c = ((v & 0x1Fu) << 3) | (((v >> 5) & 0x1Fu) << 11) | (((v >> 10) & 0x1Fu) << 19) | ((v & 0x8000u) ? 0x80000000u : 0u);
                    }
                    else
                        c = GSMem::ReadCT32(vram, block, fbw, ox + x, oy + y);
                    if (doubleRows)
                        out[size_t(y * 2u) * w + x] = out[size_t(y * 2u + 1u) * w + x] = c;
                    else
                        out[size_t(y) * w + x] = c;
                }
            frame.width = w;
            frame.height = outH;
            frame.displayFbp = frame.sourceFbp = fbp;
            return frame;
        };
        const auto opaque = [](PresentationFrame &frame) { // the window blends with alpha: show the picture opaque
            for (size_t i = 3; i < frame.pixels.size(); i += 4)
                frame.pixels[i] = 0xFFu;
        };
        if ((request.pmode & 3u) == 3u)
        {
            // Both circuits: the PCRTC merges them, circuit 1 over circuit 2 (or over BGCOLOR with
            // SLBG), weighted by ALP (MMOD) or by circuit 1's own alpha; the same blend as
            // GSCpuBackend::PresentFromLocalMemory. MKSM draws a menu on one and fades the scene on
            // the other: showing one circuit left a black screen waiting for an invisible menu.
            const PresentationFrame top = readCircuit(0), base = readCircuit(1);
            const bool mmod = (request.pmode >> 5) & 1u, slbg = (request.pmode >> 7) & 1u;
            const uint32_t alp = static_cast<uint32_t>((request.pmode >> 8) & 0xFFu);
            PresentationFrame frame;
            frame.width = std::max(top.width, base.width);
            frame.height = std::max(top.height, base.height);
            frame.pixels.assign(size_t(frame.width) * frame.height * 4u, 0u);
            for (uint32_t y = 0; y < frame.height; ++y)
                for (uint32_t x = 0; x < frame.width; ++x)
                {
                    uint8_t *dst = frame.pixels.data() + (size_t(y) * frame.width + x) * 4u;
                    if (!slbg && x < base.width && y < base.height)
                        std::memcpy(dst, base.pixels.data() + (size_t(y) * base.width + x) * 4u, 4u);
                    else
                        dst[0] = static_cast<uint8_t>(request.bgcolor), dst[1] = static_cast<uint8_t>(request.bgcolor >> 8u),
                        dst[2] = static_cast<uint8_t>(request.bgcolor >> 16u);
                    if (x >= top.width || y >= top.height)
                        continue;
                    const uint8_t *src = top.pixels.data() + (size_t(y) * top.width + x) * 4u;
                    const int factor = static_cast<int>(mmod ? alp : std::min<uint32_t>(255u, src[3] * 2u));
                    for (int ch = 0; ch < 3; ++ch)
                        dst[ch] = static_cast<uint8_t>(std::clamp(dst[ch] + ((src[ch] - dst[ch]) * factor) / 255, 0, 255));
                }
            opaque(frame);
            frame.displayFbp = top.displayFbp, frame.sourceFbp = top.sourceFbp;
            return frame;
        }
        for (int circuit = 0; circuit < 2; ++circuit)
        {
            if (!((request.pmode >> circuit) & 1u))
                continue;
            PresentationFrame frame = readCircuit(circuit);
            opaque(frame);
            return frame;
        }
    }
#endif
    return m_inner.Present(request);
}

bool GSHwBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba)
{
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
    {
        m_renderer->downloadAll();
        m_renderer->syncInner();
        const bool done = m_inner.ClearFramebuffer(context, rgba);
        m_renderer->mirrorWrote(0u, kVramBlocks);
        return done;
    }
#endif
    return m_inner.ClearFramebuffer(context, rgba);
}

uint32_t GSHwBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
    return m_inner.ConsumeLocalToHostBytes(dst, maxBytes);
}

uint32_t GSHwBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
        m_renderer->downloadAll();
#endif
    return m_inner.ReadVram(psm, base, bw, x, y);
}

void GSHwBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
    {
        m_renderer->downloadAll();
        m_renderer->mirrorWrote(0u, kVramBlocks);
        m_renderer->innerDirty = true;
        m_renderer->pendingUnknown = true;
    }
#endif
    m_inner.WriteVram(psm, base, bw, x, y, value);
}

void GSHwBackend::SnapshotVram(std::vector<uint8_t> &out) const
{
    waitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_lock);
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
        m_renderer->downloadAll(), m_renderer->syncInner();
#endif
    m_inner.SnapshotVram(out);
}

GSTransferSnapshot GSHwBackend::GetTransferSnapshot() const
{
    return m_inner.GetTransferSnapshot(); // debug only; the CPU backend locks its own state
}
void GSHwBackend::report() const
{
#if defined(PS2X_HAVE_VULKAN)
    if (m_renderer)
    {
        Renderer &r = *m_renderer;
        std::fprintf(stderr,
                     "[gs:hw] surface->mirror copies since last line: texread %llu, decode %llu, clut %llu, present %llu, "
                     "transfer %llu, evict %llu, other %llu | mirror->surface uploads %llu\n",
                     (unsigned long long)r.downloadsBy[Renderer::WhyTextureRead], (unsigned long long)r.downloadsBy[Renderer::WhyDecode],
                     (unsigned long long)r.downloadsBy[Renderer::WhyClut], (unsigned long long)r.downloadsBy[Renderer::WhyPresent],
                     (unsigned long long)r.downloadsBy[Renderer::WhyTransfer], (unsigned long long)r.downloadsBy[Renderer::WhyEvict],
                     (unsigned long long)r.downloadsBy[Renderer::WhyOther], (unsigned long long)r.uploadsToSurface);
        std::fill(std::begin(r.downloadsBy), std::end(r.downloadsBy), 0u);
        r.uploadsToSurface = 0;
    }
#endif
    const Stats &s = m_stats;
    const double frames = s.frames ? static_cast<double>(s.frames) : 1.0;
    std::fprintf(stderr,
                 "[gs:hw] per frame: %.0f primitives in %.0f batches (breaks: target %.0f, texture %.0f, blend %.0f, test %.0f, other %.0f), "
                 "feedback %.0f, fallback %.0f, downloads %.1f\n",
                 s.primitives / frames, s.batches / frames, s.breaksTarget / frames, s.breaksTexture / frames, s.breaksBlend / frames,
                 s.breaksTest / frames, s.breaksOther / frames, s.feedback / frames, s.fallback / frames, s.downloads / frames);
}

#undef HW
