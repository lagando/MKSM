#include <immintrin.h>
#include "gs_gpu_accelerator.h"
#include "runtime/ps2_perf.h"

#if PS2X_HAVE_VULKAN

#include "runtime/gs/ps2_gs_memory.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

namespace
{
    const uint32_t kSpriteShader[] = {
#include "gs_sprite.inc"
    };
    const uint32_t kTilesShader[] = {
#include "gs_tiles.inc"
    };
    const uint32_t kRasterVertShader[] = {
#include "gs_raster_vert.inc"
    };
    constexpr size_t kCaptureBytes = 140u * 8192u; // see QueueCapture
    const uint32_t kRasterFragShader[] = {
#include "gs_raster_frag.inc"
    };
    constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    constexpr VkDeviceSize kPageBytes = 8192u;
    constexpr uint32_t kUvCapacity = 1u << 20; // int32 texel coordinates per submission
    constexpr uint32_t kRecordWords = 64u;

    // Copy into the BAR-mapped rings (records, uv, tiles, clut): they are write-combined, and
    // streaming stores beat memcpy's rep movsb there. Submit fences them (_mm_sfence).
    inline void streamCopy(void *dst, const void *src, size_t bytes)
    {
        auto *d = static_cast<uint8_t *>(dst);
        const auto *s = static_cast<const uint8_t *>(src);
        const size_t head = (32u - (reinterpret_cast<uintptr_t>(d) & 31u)) & 31u;
        if (bytes < 64u || head > bytes)
        {
            std::memcpy(d, s, bytes);
            return;
        }
        std::memcpy(d, s, head);
        d += head, s += head, bytes -= head;
        for (; bytes >= 32u; d += 32, s += 32, bytes -= 32u)
            _mm256_stream_si256(reinterpret_cast<__m256i *>(d), _mm256_loadu_si256(reinterpret_cast<const __m256i *>(s)));
        std::memcpy(d, s, bytes);
    }
    constexpr uint32_t kRecordCapacity = 1u << 15; // primitives per submission
    constexpr uint32_t kTileWordsCapacity = 1u << 22;
    constexpr uint32_t kTileSize = 8u;              // must match kTileSize in gs_tiles.comp
    constexpr uint32_t kTileGroup = kTileSize * kTileSize;
    constexpr uint32_t kTileGrid = 2048u / kTileSize; // tiles over the 2048x2048 GS space
    constexpr uint32_t kTableWords = 45056u;
    constexpr uint32_t kBindings = 8u;
    constexpr uint32_t kClutVersions = 256u; // CLUT changes per submission
    // Submission slots: the CPU fills one while the GPU runs the others. With two the worker
    // waited ~35% of the time for the slot two submissions back (fase3.gsr).
    constexpr uint32_t kSlots = 3u;
    // Decoded textures (swizzle + CLUT + TEXA already applied) per submission. Sampling
    // from them is one read instead of a page table -> VRAM -> CLUT chain. The fase2 replay
    // decodes ~270K texels per submission; textures past kMaxCachedTexels, or a full slot,
    // are sampled from VRAM as before.
    constexpr uint32_t kTexCacheTexels = 1u << 21;
    constexpr uint32_t kMaxCachedTexels = 1u << 18;

    struct TexKey
    {
        uint32_t words[9]; // tbp, tbw, psm, cpsm, csm, csa, tw, th, texa (CLUT fields 0 if unpaletted)
        uint64_t clutHash;
        bool operator==(const TexKey &o) const { return clutHash == o.clutHash && std::memcmp(words, o.words, sizeof(words)) == 0; }
    };
    struct TexKeyHash
    {
        size_t operator()(const TexKey &k) const
        {
            uint64_t h = k.clutHash;
            for (const uint32_t w : k.words)
                h = (h ^ w) * 1099511628211ull;
            return static_cast<size_t>(h);
        }
    };
    struct TexEntry
    {
        uint32_t offset, start, end; // texel offset in texCache; blocks it was decoded from
    };

    // Page tables per memory layout, at the offsets gs_tiles.comp expects:
    // C32, Z32, C16, C16S, Z16, Z16S, P8, P4.
    struct LayoutTable
    {
        uint32_t psm, pageW, pageH, offset;
    };
    constexpr LayoutTable kLayouts[] = {
        {0x00u, 64u, 32u, 0u}, {0x30u, 64u, 32u, 2048u}, {0x02u, 64u, 64u, 4096u}, {0x0Au, 64u, 64u, 8192u},
        {0x32u, 64u, 64u, 12288u}, {0x3Au, 64u, 64u, 16384u}, {0x13u, 128u, 64u, 20480u}, {0x14u, 128u, 128u, 28672u}};

    struct TilesPush
    {
        uint32_t tileBase, recordBase;
        uint32_t upBlock = 0, upWidth = 0, upPsm = 0, upX = 0, upY = 0, upRowWidth = 0, upFirstPixel = 0, upCount = 0, upData = 0;
        uint32_t srcBlock = 0, srcWidth = 0, srcPsm = 0, srcX = 0, srcY = 0;
    };

    struct BatchWrite
    {
        uint32_t start, end, width, psm, kind; // kind 0 frame, 1 depth
    };

    struct SpritePush // gs_sprite.comp: a batch of 16-word descriptors in the uv ring
    {
        uint32_t descBase, count;
    };
    constexpr uint32_t kSpriteDescWords = 16u;
    constexpr uint32_t kMaxBatchedSprites = 1024u;

    // A queued sprite of the open batch: what it writes and reads, to keep later sprites
    // that depend on it out of the same dispatch.
    struct BatchedSprite
    {
        int x0, y0, x1, y1;                // written pixels (inclusive)
        uint32_t fbBlock, fbWidth;
        uint32_t writeStart, writeEnd;     // the same in blocks
        bool readsTexture = false, readsVram = false; // readsVram: texture not from the snapshot
        int u0 = 0, v0 = 0, u1 = 0, v1 = 0; // texels read (inclusive bounds)
        uint32_t texBlock = 0, texWidth = 0;
        uint32_t readStart = 0, readEnd = 0;
    };
}

struct GSGpuAccelerator::Impl
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;

    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
        VkMemoryPropertyFlags flags = 0;
    };
    // vram is the host copy the CPU backend draws into. On a discrete GPU it sits in system
    // RAM, too slow for per-pixel GPU access, so the GPU works on deviceVram and the two
    // are synced by block ranges: host writes go up, GPU writes come back on Flush.
    Buffer vram, deviceVram, snapshot, swizzle, uv, records, tiles, clut, texCache;
    // Whole-memory copies for presentation, one per slot: written by that slot's submission.
    Buffer capture[kSlots];
    // Sprites queued but not dispatched yet (see QueueSprite).
    std::vector<BatchedSprite> spriteBatch;
    std::vector<uint32_t> spriteDescs;
    std::vector<uint32_t> spritePages; // snapshot pages the batch's feedback sprites read
    int spriteMaxW = 0, spriteMaxH = 0;
    // PS2X_PERF: GPU timestamps at both ends of each submission give the GPU's busy share.
    VkQueryPool timeQueries = VK_NULL_HANDLE;
    double timestampNs = 1.0;
    double gpuBusyNs = 0.0;
    uint64_t gpuBusySince = 0;
    bool captureInSlot[kSlots] = {}; // a capture was queued in the slot's submission
    bool captureDone[kSlots] = {}; // ...and its fence has signalled
    uint64_t captureSeq[kSlots] = {};
    uint64_t nextCaptureSeq = 0;
    bool mirror = false;
    std::vector<std::pair<uint32_t, uint32_t>> hostDirty; // host writes since the last submission
    VkDeviceSize vramSize = 0;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline spritePipeline = VK_NULL_HANDLE;
    VkPipeline tilesPipeline = VK_NULL_HANDLE;
    VkPipeline uploadPipeline = VK_NULL_HANDLE; // gs_tiles.comp with kMode = 1
    VkPipeline copyPipeline = VK_NULL_HANDLE;   // gs_tiles.comp with kMode = 2
    VkPipeline decodePipeline = VK_NULL_HANDLE; // gs_tiles.comp with kMode = 3
    VkPipeline clutPipeline = VK_NULL_HANDLE;   // gs_tiles.comp with kMode = 4
    // Rasterizer path (default; PS2X_GS_RASTER=0 keeps compute tiles): tile batches become draws of gs_raster.vert plus
    // gs_tiles.comp built as a fragment shader with the ordered pixel interlock.
    bool raster = false;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipeline rasterPipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    // kSlots submission slots: the CPU fills one while the GPU runs the others. Each owns a
    // command buffer, a fence and its own half of the uv/record/tile/CLUT rings.
    VkCommandBuffer slotCommands[kSlots] = {};
    VkFence slotFences[kSlots] = {};
    bool inFlight[kSlots] = {};
    std::vector<std::pair<uint32_t, uint32_t>> flightRanges[kSlots][2]; // [slot][read, written]
    uint32_t slot = 0;
    VkCommandBuffer commands = VK_NULL_HANDLE; // the recording slot's buffer

    bool recording = false;
    uint32_t uvUsed = 0;
    uint32_t recordsUsed = 0;
    uint32_t tileWordsUsed = 0;
    uint32_t uvLimit() const { return (slot + 1u) * kUvCapacity; }
    uint32_t recordLimit() const { return (slot + 1u) * kRecordCapacity; }
    uint32_t tileWordsLimit() const { return (slot + 1u) * kTileWordsCapacity; }
    uint32_t clutLimit() const { return (slot + 1u) * kClutVersions; }

    void noteGpuTime(uint32_t index)
    {
        uint64_t stamps[2] = {};
        if (vkGetQueryPoolResults(device, timeQueries, index * 2u, 2u, sizeof(stamps), stamps, 8u, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            gpuBusyNs += double(stamps[1] - stamps[0]) * timestampNs;
        const uint64_t now = ps2_perf::nowNs();
        if (gpuBusySince == 0u)
            gpuBusySince = now;
        else if (now - gpuBusySince >= 2000000000ull)
        {
            std::fprintf(stderr, "[perf] GPU ocupada %.0f%%\n", 100.0 * gpuBusyNs / double(now - gpuBusySince));
            gpuBusyNs = 0.0;
            gpuBusySince = now;
        }
    }

    void waitSlot(uint32_t index)
    {
        if (!inFlight[index])
            return;
        vkWaitForFences(device, 1, &slotFences[index], VK_TRUE, UINT64_MAX);
        if (captureInSlot[index])
            captureDone[index] = true;
        if (timeQueries)
            noteGpuTime(index);
        vkResetFences(device, 1, &slotFences[index]);
        vkResetCommandBuffer(slotCommands[index], 0);
        inFlight[index] = false;
        flightRanges[index][0].clear();
        flightRanges[index][1].clear();
    }

    // Tile batch being collected: primitives since batchRecordBase, binned per tile.
    uint32_t batchRecordBase = 0;
    std::vector<std::vector<uint32_t>> tileLists = std::vector<std::vector<uint32_t>>(kTileGrid * kTileGrid);
    std::vector<uint32_t> activeTiles;
    std::vector<BatchWrite> batchWrites;
    std::vector<std::pair<uint32_t, uint32_t>> batchReads; // textures sampled in this batch
    uint32_t pendingListWords = 0;
    // Everything queued since the last submission touches these block ranges.
    std::vector<std::pair<uint32_t, uint32_t>> pendingRanges[2]; // [0] read, [1] written
    std::array<uint16_t, 512> currentClut{};
    uint32_t clutVersion = 0;
    uint64_t clutHash = 0; // of currentClut; 0 = not computed yet
    // Entries only the GPU knows (GPU CLUT loads). In a version the host writes, such an
    // entry is bit 31 | the word offset of the version a kMode 4 pass wrote its value to
    // (clutEntryVersion); across submissions values pass through a carry version after both
    // slots.
    std::array<uint64_t, 8> clutUnknown{};
    std::array<uint32_t, 512> clutEntryVersion{};
    std::unordered_map<uint64_t, uint32_t> clutLoadVersions; // load id -> version holding its result, this submission
    // Per GPU-only entry, the id of the load that wrote it: its arguments and the write
    // generation of its source. Equal ids mean equal values, which keys the texture cache
    // and skips the game's repeated loads (CLD=1 on every TEX0, ~1000 per field).
    std::array<uint64_t, 512> clutEntryId{};
    struct WatchedSource
    {
        uint32_t start, end;
        uint64_t generation;
    };
    std::vector<WatchedSource> watchedSources; // CLUT sources loaded on the GPU
    uint64_t sourceGenerations = 0;
    uint64_t sourceGeneration(uint32_t start, uint32_t end)
    {
        for (const WatchedSource &source : watchedSources)
            if (source.start == start && source.end == end)
                return source.generation;
        if (watchedSources.size() >= 32u)
            watchedSources.erase(watchedSources.begin());
        watchedSources.push_back({start, end, ++sourceGenerations});
        watchLo = UINT32_MAX, watchHi = 0u;
        for (const WatchedSource &source : watchedSources)
            watchLo = std::min(watchLo, source.start), watchHi = std::max(watchHi, source.end);
        return watchedSources.back().generation;
    }
    uint32_t watchLo = UINT32_MAX, watchHi = 0u; // bounds of watchedSources
    void noteSourceWrite(uint32_t start, uint32_t end)
    {
        // Frame and depth writes of every primitive land here; almost none reach a CLUT source.
        if (end <= watchLo || watchHi <= start)
            return;
        for (WatchedSource &source : watchedSources)
            if (start < source.end && source.start < end)
                source.generation = ++sourceGenerations;
    }
    static constexpr uint32_t kClutCarry = kSlots * kClutVersions; // version index
    bool clutHasUnknown() const
    {
        for (const uint64_t word : clutUnknown)
            if (word != 0u)
                return true;
        return false;
    }
    // Decoded textures of the recording slot; any write to their blocks drops them.
    std::unordered_map<TexKey, TexEntry, TexKeyHash> texEntries;
    // Consecutive primitives mostly sample the same texture: the last hit skips the hash
    // lookup (its prime-modulo division showed in the worker profile). Map nodes keep
    // their address on rehash; any erase or clear forgets it.
    const TexKey *lastTexKey = nullptr;
    const TexEntry *lastTexEntry = nullptr;
    uint32_t texLo = UINT32_MAX, texHi = 0u; // union of the cached textures' blocks (only grows until the clear)
    uint32_t texCacheUsed = 0;

    void dropTextures(uint32_t start, uint32_t end)
    {
        // The bound skips the map walk for writes that miss every cached texture (most of them).
        if (end <= start || end <= texLo || texHi <= start)
            return;
        lastTexKey = nullptr, lastTexEntry = nullptr;
        for (auto it = texEntries.begin(); it != texEntries.end();)
            it = start < it->second.end && it->second.start < end ? texEntries.erase(it) : std::next(it);
    }

    static void mergeRange(std::vector<std::pair<uint32_t, uint32_t>> &ranges, uint32_t start, uint32_t end)
    {
        if (end <= start)
            return;
        for (auto &range : ranges)
            if (start <= range.second && range.first <= end)
            {
                range.first = std::min(range.first, start);
                range.second = std::max(range.second, end);
                return;
            }
        ranges.emplace_back(start, end);
    }

    void notePending(uint32_t start, uint32_t end, bool written)
    {
        if (written)
        {
            dropTextures(start, end);
            noteSourceWrite(start, end);
        }
        mergeRange(pendingRanges[written ? 1 : 0], start, end);
    }

    VkBuffer gpuVram() const { return mirror ? deviceVram.buffer : vram.buffer; }

    // Copies block ranges between the host and device copies, ordered after earlier GPU work.
    void copyVram(VkBuffer from, VkBuffer to, const std::vector<std::pair<uint32_t, uint32_t>> &ranges)
    {
        std::vector<VkBufferCopy> regions;
        for (const auto &[start, end] : ranges)
        {
            const VkDeviceSize first = std::min<VkDeviceSize>(VkDeviceSize(start) * 256u, vramSize);
            const VkDeviceSize last = std::min<VkDeviceSize>(VkDeviceSize(end) * 256u, vramSize);
            if (last > first)
                regions.push_back({first, first, last - first});
        }
        if (regions.empty())
            return;
        memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdCopyBuffer(commands, from, to, static_cast<uint32_t>(regions.size()), regions.data());
        memoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
    }

    // Version toVersion gets fromVersion's values, then runs the load in push (if any).
    void dispatchClut(uint32_t fromVersion, uint32_t toVersion, TilesPush push)
    {
        push.srcBlock = fromVersion * 512u;
        push.srcWidth = toVersion * 512u;
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, clutPipeline);
        vkCmdPushConstants(commands, pipelineLayout, kPushStages, 0, sizeof(push), &push);
        vkCmdDispatch(commands, 1, 1, 1);
        memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT);
    }

    void writeClut(uint32_t version)
    {
        auto *clutWords = static_cast<uint32_t *>(clut.mapped) + version * 512u;
        for (uint32_t i = 0; i < 512u; ++i)
            clutWords[i] = ((clutUnknown[i >> 6] >> (i & 63u)) & 1u) ? 0x80000000u | (clutEntryVersion[i] * 512u) : currentClut[i];
    }

    // Dispatches the open sprite batch: snapshot pages first (one copy), then one layer per sprite.
    void flushSprites()
    {
        if (spriteBatch.empty())
            return;
        if (!spritePages.empty())
        {
            std::sort(spritePages.begin(), spritePages.end());
            spritePages.erase(std::unique(spritePages.begin(), spritePages.end()), spritePages.end());
            std::vector<VkBufferCopy> regions;
            regions.reserve(spritePages.size());
            for (const uint32_t page : spritePages)
                regions.push_back({page * kPageBytes, page * kPageBytes, kPageBytes});
            vkCmdCopyBuffer(commands, gpuVram(), snapshot.buffer, static_cast<uint32_t>(regions.size()), regions.data());
            memoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        }
        SpritePush push{uvUsed, static_cast<uint32_t>(spriteBatch.size())};
        streamCopy(static_cast<uint32_t *>(uv.mapped) + uvUsed, spriteDescs.data(), spriteDescs.size() * 4u);
        uvUsed += static_cast<uint32_t>(spriteDescs.size());
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, spritePipeline);
        vkCmdPushConstants(commands, pipelineLayout, kPushStages, 0, sizeof(push), &push);
        vkCmdDispatch(commands, static_cast<uint32_t>((spriteMaxW + 7) / 8), static_cast<uint32_t>((spriteMaxH + 7) / 8), push.count);
        // Draws run in GS order: the next one may blend over or sample these pixels.
        memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
        spriteBatch.clear();
        spriteDescs.clear();
        spritePages.clear();
        spriteMaxW = spriteMaxH = 0;
    }

    void emitTileBatch()
    {
        const uint32_t count = recordsUsed - batchRecordBase;
        if (count == 0u)
            return;
        if (raster)
        {
            VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            begin.renderPass = renderPass;
            begin.framebuffer = framebuffer;
            begin.renderArea.extent = {2048u, 2048u};
            vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline);
            const TilesPush push{0u, batchRecordBase};
            vkCmdPushConstants(commands, pipelineLayout, kPushStages, 0, sizeof(push), &push);
            vkCmdDraw(commands, count * 3u, 1u, 0u, 0u);
            vkCmdEndRenderPass(commands);
            memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
            batchWrites.clear();
            batchReads.clear();
            batchRecordBase = recordsUsed;
            return;
        }
        uint32_t listWords = 0u;
        for (const uint32_t tile : activeTiles)
            listWords += static_cast<uint32_t>(tileLists[tile].size());
        auto *words = static_cast<uint32_t *>(tiles.mapped);
        const uint32_t headerBase = tileWordsUsed;
        uint32_t listAt = headerBase + static_cast<uint32_t>(activeTiles.size()) * 4u;
        for (size_t i = 0; i < activeTiles.size(); ++i)
        {
            const uint32_t tile = activeTiles[i];
            std::vector<uint32_t> &list = tileLists[tile];
            uint32_t *header = words + headerBase + i * 4u;
            header[0] = tile % kTileGrid;
            header[1] = tile / kTileGrid;
            header[2] = listAt;
            header[3] = static_cast<uint32_t>(list.size());
            std::memcpy(words + listAt, list.data(), list.size() * 4u);
            listAt += static_cast<uint32_t>(list.size());
            list.clear();
        }
        tileWordsUsed = listAt;
        (void)listWords;
        pendingListWords = 0u;

        if (!activeTiles.empty())
        {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, tilesPipeline);
            const TilesPush push{headerBase, batchRecordBase};
            vkCmdPushConstants(commands, pipelineLayout, kPushStages, 0, sizeof(push), &push);
            vkCmdDispatch(commands, static_cast<uint32_t>(activeTiles.size()), 1, 1);
            memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
        }
        activeTiles.clear();
        batchWrites.clear();
        batchReads.clear();
        batchRecordBase = recordsUsed;
    }

    bool createBuffer(Buffer &out, VkDeviceSize size, VkBufferUsageFlags usage, bool preferDevice = false, bool deviceOnly = false)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &info, nullptr, &out.buffer) != VK_SUCCESS)
            return false;
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, out.buffer, &requirements);
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        // Host-visible and coherent: the CPU fills every buffer. preferDevice picks GPU memory
        // the CPU only writes (BAR); otherwise cached memory the CPU also reads.
        // deviceOnly: GPU memory the CPU never touches (not mapped).
        const VkMemoryPropertyFlags wanted = deviceOnly ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                                        : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        int chosen = -1;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        {
            const VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
            if (!(requirements.memoryTypeBits & (1u << i)) || (flags & wanted) != wanted)
                continue;
            if (preferDevice)
            {
                if (chosen < 0 || (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                    chosen = static_cast<int>(i);
                continue;
            }
            if (chosen < 0 || (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
                chosen = static_cast<int>(i);
        }
        if (chosen < 0)
            return false;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = static_cast<uint32_t>(chosen);
        out.flags = properties.memoryTypes[chosen].propertyFlags;
        if (vkAllocateMemory(device, &allocate, nullptr, &out.memory) != VK_SUCCESS)
            return false;
        vkBindBufferMemory(device, out.buffer, out.memory, 0);
        if (deviceOnly)
            return true;
        return vkMapMemory(device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped) == VK_SUCCESS;
    }

    void destroyBuffer(Buffer &buffer)
    {
        if (buffer.memory)
            vkFreeMemory(device, buffer.memory, nullptr);
        if (buffer.buffer)
            vkDestroyBuffer(device, buffer.buffer, nullptr);
        buffer = {};
    }

    bool createRasterPipeline()
    {
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &passInfo, nullptr, &renderPass) != VK_SUCCESS)
            return false;
        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.width = 2048u;
        framebufferInfo.height = 2048u;
        framebufferInfo.layers = 1u;
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS)
            return false;
        auto module = [&](const uint32_t *code, size_t size)
        {
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = size;
            info.pCode = code;
            VkShaderModule out = VK_NULL_HANDLE;
            vkCreateShaderModule(device, &info, nullptr, &out);
            return out;
        };
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                     module(kRasterVertShader, sizeof(kRasterVertShader)), "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                     module(kRasterFragShader, sizeof(kRasterFragShader)), "main", nullptr};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        const VkViewport viewport{0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, {2048u, 2048u}};
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;
        // Conservative: every pixel the edge test might accept gets a fragment; the
        // fragment shader's test decides, as in the tile path.
        VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservativeLimits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &conservativeLimits;
        vkGetPhysicalDeviceProperties2(physical, &properties);
        VkPipelineRasterizationConservativeStateCreateInfoEXT conservative{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT};
        conservative.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT; // with the interlock it ran ~2x slower; gs_raster.vert dilates instead
        conservative.extraPrimitiveOverestimationSize = conservativeLimits.maxExtraPrimitiveOverestimationSize;
        VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.pNext = &conservative;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blend;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &rasterPipeline);
        vkDestroyShaderModule(device, stages[0].module, nullptr);
        vkDestroyShaderModule(device, stages[1].module, nullptr);
        return result == VK_SUCCESS;
    }

    void memoryBarrier(VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
    {
        if (raster) // draws read and write the same buffers as the compute passes
        {
            if (srcStage & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
                srcStage |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            if (dstStage & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
                dstStage |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        }
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(commands, srcStage, dstStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    void beginIfNeeded()
    {
        if (recording)
            return;
        waitSlot(slot); // submitted two batches ago; its ring half is reused now
        commands = slotCommands[slot];
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commands, &begin);
        if (timeQueries)
        {
            vkCmdResetQueryPool(commands, timeQueries, slot * 2u, 2u);
            vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timeQueries, slot * 2u);
        }
        // CPU draws since the last submission wrote this memory directly.
        memoryBarrier(VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
        // The other slot may still run: keep GS order across submissions on the queue.
        memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        if (raster)
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        recording = true;
        pendingRanges[0].clear();
        pendingRanges[1].clear();
        clutVersion = slot * kClutVersions;
        texEntries.clear();
        lastTexKey = nullptr, lastTexEntry = nullptr;
        texLo = UINT32_MAX, texHi = 0u;
        texCacheUsed = slot * kTexCacheTexels;
        uvUsed = slot * kUvCapacity;
        clutEntryVersion.fill(kClutCarry); // GPU-only values left the last submission there
        clutLoadVersions.clear();
        writeClut(clutVersion);
        recordsUsed = slot * kRecordCapacity;
        tileWordsUsed = slot * kTileWordsCapacity;
        batchRecordBase = recordsUsed;
        if (mirror && !hostDirty.empty())
        {
            // The copy runs at submission: a host write there before then must flush first.
            copyVram(vram.buffer, deviceVram.buffer, hostDirty);
            for (const auto &[start, end] : hostDirty)
                notePending(start, end, false);
            hostDirty.clear();
        }
    }
};

GSGpuAccelerator::GSGpuAccelerator() : m_impl(std::make_unique<Impl>()) {}

GSGpuAccelerator::~GSGpuAccelerator()
{
    Impl &v = *m_impl;
    if (!v.device)
    {
        if (v.instance)
            vkDestroyInstance(v.instance, nullptr);
        return;
    }
    vkDeviceWaitIdle(v.device);
    for (VkFence fence : v.slotFences)
        vkDestroyFence(v.device, fence, nullptr);
    vkDestroyCommandPool(v.device, v.commandPool, nullptr);
    vkDestroyDescriptorPool(v.device, v.descriptorPool, nullptr);
    vkDestroyPipeline(v.device, v.spritePipeline, nullptr);
    vkDestroyPipeline(v.device, v.tilesPipeline, nullptr);
    vkDestroyPipeline(v.device, v.uploadPipeline, nullptr);
    vkDestroyPipeline(v.device, v.copyPipeline, nullptr);
    vkDestroyPipeline(v.device, v.decodePipeline, nullptr);
    vkDestroyPipeline(v.device, v.clutPipeline, nullptr);
    vkDestroyPipeline(v.device, v.rasterPipeline, nullptr);
    vkDestroyFramebuffer(v.device, v.framebuffer, nullptr);
    vkDestroyRenderPass(v.device, v.renderPass, nullptr);
    vkDestroyPipelineLayout(v.device, v.pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(v.device, v.setLayout, nullptr);
    v.destroyBuffer(v.vram);
    v.destroyBuffer(v.deviceVram);
    v.destroyBuffer(v.snapshot);
    if (v.timeQueries)
        vkDestroyQueryPool(v.device, v.timeQueries, nullptr);
    for (auto &capture : v.capture)
        v.destroyBuffer(capture);
    v.destroyBuffer(v.swizzle);
    v.destroyBuffer(v.uv);
    v.destroyBuffer(v.records);
    v.destroyBuffer(v.tiles);
    v.destroyBuffer(v.clut);
    v.destroyBuffer(v.texCache);
    vkDestroyDevice(v.device, nullptr);
    vkDestroyInstance(v.instance, nullptr);
}

std::unique_ptr<GSGpuAccelerator> GSGpuAccelerator::CreateFromEnvironment(uint32_t vramSize)
{
    // On by default (3.5x faster in Goro's Lair, VRAM identical to the CPU in every replay);
    // PS2X_GS_GPU=0 draws on the CPU.
    const char *enabled = std::getenv("PS2X_GS_GPU");
    if (enabled && enabled[0] == '0')
        return nullptr;
    std::unique_ptr<GSGpuAccelerator> accelerator(new GSGpuAccelerator());
    if (!accelerator->Initialize(vramSize))
    {
        std::cerr << "[gs:gpu] Vulkan unavailable, drawing on the CPU" << std::endl;
        return nullptr;
    }
    return accelerator;
}

bool GSGpuAccelerator::Initialize(uint32_t vramSize)
{
    Impl &v = *m_impl;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ps2x-gs";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    if (vkCreateInstance(&instanceInfo, nullptr, &v.instance) != VK_SUCCESS)
        return false;

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(v.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(v.instance, &count, devices.data());
    // PS2X_GPU_DEVICE=<part of the name> picks one; otherwise a discrete GPU (measured
    // faster, and the integrated one hit i915 GPU hangs), else the first device.
    const char *wanted = std::getenv("PS2X_GPU_DEVICE");
    int best = -1;
    for (uint32_t i = 0; i < count; ++i)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        const bool named = wanted && std::string(properties.deviceName).find(wanted) != std::string::npos;
        const bool discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        if (named || (!wanted && (best < 0 || discrete)))
            best = static_cast<int>(i);
        if (named)
            break;
    }
    if (best < 0)
        return false;
    v.physical = devices[static_cast<size_t>(best)];
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(v.physical, &properties);
    v.timestampNs = properties.limits.timestampPeriod;

    vkGetPhysicalDeviceQueueFamilyProperties(v.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(v.physical, &count, families.data());
    bool found = false;
    for (uint32_t i = 0; i < count && !found; ++i) // graphics too: the rasterizer path draws
        if ((families[i].queueFlags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) == (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT))
            v.queueFamily = i, found = true;
    for (uint32_t i = 0; i < count && !found; ++i)
        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            v.queueFamily = i, found = true;
    if (!found)
        return false;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = v.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    {
        const char *setting = std::getenv("PS2X_GS_RASTER");
        v.raster = (setting == nullptr || setting[0] != '0') && (families[v.queueFamily].queueFlags & VK_QUEUE_GRAPHICS_BIT);
    }
    const char *rasterExtensions[] = {"VK_EXT_fragment_shader_interlock", "VK_EXT_conservative_rasterization"};
    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    if (v.raster)
    {
        VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
        VkPhysicalDeviceFeatures2 available{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        available.pNext = &supported;
        vkGetPhysicalDeviceFeatures2(v.physical, &available);
        v.raster = supported.fragmentShaderPixelInterlock && available.features.fragmentStoresAndAtomics && available.features.shaderFloat64;
    }
    if (v.raster)
    {
        interlock.fragmentShaderPixelInterlock = VK_TRUE;
        features.pNext = &interlock;
        features.features.fragmentStoresAndAtomics = VK_TRUE;
        features.features.shaderFloat64 = VK_TRUE;
        deviceInfo.pNext = &features;
        deviceInfo.enabledExtensionCount = 2u;
        deviceInfo.ppEnabledExtensionNames = rasterExtensions;
        if (vkCreateDevice(v.physical, &deviceInfo, nullptr, &v.device) != VK_SUCCESS)
        {
            v.raster = false; // extensions missing: compute tiles as before
            deviceInfo.pNext = nullptr;
            deviceInfo.enabledExtensionCount = 0u;
        }
    }
    if (!v.raster && vkCreateDevice(v.physical, &deviceInfo, nullptr, &v.device) != VK_SUCCESS)
        return false;
    vkGetDeviceQueue(v.device, v.queueFamily, 0, &v.queue);
    if (ps2_perf::enabled())
    {
        VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queries.queryCount = 2u * kSlots; // begin and end of each slot's submission
        vkCreateQueryPool(v.device, &queries, nullptr, &v.timeQueries);
    }

    v.vramSize = vramSize;
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkBufferUsageFlags copies = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!v.createBuffer(v.vram, vramSize, storage | copies))
        return false;
    v.mirror = (v.vram.flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0u;
    if ((v.mirror && !v.createBuffer(v.deviceVram, vramSize, storage | copies, true)) ||
        !v.createBuffer(v.snapshot, vramSize, storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true) ||
        !v.createBuffer(v.swizzle, kTableWords * 4u, storage, true) ||
        !v.createBuffer(v.uv, kSlots * kUvCapacity * 4u, storage, true) ||
        !v.createBuffer(v.records, kSlots * kRecordCapacity * kRecordWords * 4u, storage, true) ||
        !v.createBuffer(v.tiles, kSlots * kTileWordsCapacity * 4u, storage, true) ||
        !v.createBuffer(v.clut, (kSlots * kClutVersions + 1u) * 512u * 4u, storage, true) || // + carry
        !v.createBuffer(v.texCache, kSlots * kTexCacheTexels * 4u, storage, false, true))
        return false;
    for (Impl::Buffer &capture : v.capture) // cached: the CPU reads it back
        if (!v.createBuffer(capture, vramSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT))
            return false;
    std::memset(v.vram.mapped, 0, vramSize);
    v.hostDirty.emplace_back(0u, vramSize / 256u);
    // Position inside a page for each layout: block 0 of a one-page-wide buffer gives
    // BlockId * pixelsPerBlock + ColumnId, exactly what the CPU tables hold.
    auto *tables = static_cast<uint32_t *>(v.swizzle.mapped);
    for (const LayoutTable &layout : kLayouts)
        for (uint32_t y = 0; y < layout.pageH; ++y)
            for (uint32_t x = 0; x < layout.pageW; ++x)
                tables[layout.offset + y * layout.pageW + x] = GSMem::PixelAddress(layout.psm, 0u, layout.pageW / 64u, x, y);
    std::memset(v.clut.mapped, 0, kSlots * kClutVersions * 512u * 4u);

    VkDescriptorSetLayoutBinding bindings[kBindings]{};
    for (uint32_t i = 0; i < kBindings; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = kPushStages;
    }
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = kBindings;
    setInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(v.device, &setInfo, nullptr, &v.setLayout);
    VkPushConstantRange push{kPushStages, 0, std::max<uint32_t>(sizeof(SpritePush), sizeof(TilesPush))};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &v.setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    vkCreatePipelineLayout(v.device, &layoutInfo, nullptr, &v.pipelineLayout);

    auto createPipeline = [&](const uint32_t *code, size_t size, VkPipeline &out, uint32_t mode = 0u)
    {
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = size;
        moduleInfo.pCode = code;
        VkShaderModule module;
        if (vkCreateShaderModule(v.device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
            return false;
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        const VkSpecializationMapEntry modeEntry{0u, 0u, sizeof(uint32_t)};
        const VkSpecializationInfo specialization{1u, &modeEntry, sizeof(uint32_t), &mode};
        pipelineInfo.stage.pSpecializationInfo = mode != 0u ? &specialization : nullptr;
        pipelineInfo.layout = v.pipelineLayout;
        const VkResult result = vkCreateComputePipelines(v.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out);
        vkDestroyShaderModule(v.device, module, nullptr);
        return result == VK_SUCCESS;
    };
    if (!createPipeline(kSpriteShader, sizeof(kSpriteShader), v.spritePipeline) ||
        !createPipeline(kTilesShader, sizeof(kTilesShader), v.tilesPipeline) ||
        !createPipeline(kTilesShader, sizeof(kTilesShader), v.uploadPipeline, 1u) ||
        !createPipeline(kTilesShader, sizeof(kTilesShader), v.copyPipeline, 2u) ||
        !createPipeline(kTilesShader, sizeof(kTilesShader), v.decodePipeline, 3u) ||
        !createPipeline(kTilesShader, sizeof(kTilesShader), v.clutPipeline, 4u) ||
        (v.raster && !v.createRasterPipeline()))
        return false;

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kBindings};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(v.device, &poolInfo, nullptr, &v.descriptorPool);
    VkDescriptorSetAllocateInfo allocateSet{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateSet.descriptorPool = v.descriptorPool;
    allocateSet.descriptorSetCount = 1;
    allocateSet.pSetLayouts = &v.setLayout;
    vkAllocateDescriptorSets(v.device, &allocateSet, &v.descriptorSet);
    VkDescriptorBufferInfo infos[kBindings] = {{v.gpuVram(), 0, VK_WHOLE_SIZE}, {v.snapshot.buffer, 0, VK_WHOLE_SIZE},
                                               {v.swizzle.buffer, 0, VK_WHOLE_SIZE}, {v.uv.buffer, 0, VK_WHOLE_SIZE},
                                               {v.records.buffer, 0, VK_WHOLE_SIZE}, {v.tiles.buffer, 0, VK_WHOLE_SIZE},
                                               {v.clut.buffer, 0, VK_WHOLE_SIZE}, {v.texCache.buffer, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet writes[kBindings]{};
    for (uint32_t i = 0; i < kBindings; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = v.descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(v.device, kBindings, writes, 0, nullptr);

    VkCommandPoolCreateInfo poolCreate{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCreate.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCreate.queueFamilyIndex = v.queueFamily;
    vkCreateCommandPool(v.device, &poolCreate, nullptr, &v.commandPool);
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = v.commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = kSlots;
    vkAllocateCommandBuffers(v.device, &commandInfo, v.slotCommands);
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    for (VkFence &fence : v.slotFences)
        vkCreateFence(v.device, &fenceInfo, nullptr, &fence);

    std::cerr << "[gs:gpu] " << properties.deviceName << ": sprites and triangles on the GPU" << std::endl;
    return true;
}

uint8_t *GSGpuAccelerator::vram() const
{
    return static_cast<uint8_t *>(m_impl->vram.mapped);
}

bool GSGpuAccelerator::HasPendingWork() const
{
    return m_impl->recording || m_impl->inFlight[0] || m_impl->inFlight[1];
}

void GSGpuAccelerator::QueueSprite(const GSGpuSpriteJob &job)
{
    Impl &v = *m_impl;
    if (job.x0 > job.x1 || job.y0 > job.y1)
        return; // fully outside the scissor
    // The bloom and blur passes are ~1200 small sprites per field; one dispatch, barrier and
    // snapshot copy each took ~30% of the GPU. Sprites that don't depend on each other share
    // a dispatch: a later one may not write pixels an earlier one writes or reads from VRAM,
    // nor read pixels an earlier one writes. Feedback sprites read the snapshot, which the
    // batch copies before it runs, so their own reads see the memory as it was before.
    BatchedSprite entry{};
    entry.x0 = job.x0, entry.y0 = job.y0, entry.x1 = job.x1, entry.y1 = job.y1;
    entry.fbBlock = job.fbBlock, entry.fbWidth = job.fbWidth;
    entry.writeStart = job.fbBlock;
    entry.writeEnd = job.fbBlock + job.fbWidth * (static_cast<uint32_t>(job.y1) / 32u + 1u) * 32u;
    if (job.flags & 2u)
    {
        entry.readsTexture = true;
        entry.readsVram = (job.flags & 8u) == 0u;
        const auto [minU, maxU] = std::minmax_element(job.columnU.begin(), job.columnU.end());
        const auto [minV, maxV] = std::minmax_element(job.rowV.begin(), job.rowV.end());
        entry.u0 = job.columnU.empty() ? 0 : *minU, entry.u1 = job.columnU.empty() ? 0 : *maxU;
        entry.v0 = job.rowV.empty() ? 0 : *minV, entry.v1 = job.rowV.empty() ? 0 : *maxV;
        entry.texBlock = job.texBlock, entry.texWidth = std::max<uint32_t>(job.texWidth, 1u);
        entry.readStart = job.texBlock;
        entry.readEnd = job.texBlock + entry.texWidth * (static_cast<uint32_t>(std::max(entry.v1, 0)) / 32u + 2u) * 32u;
    }
    auto blocksOverlap = [](uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a0 < b1 && b0 < a1; };
    auto rectsOverlap = [](int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1)
    { return ax0 <= bx1 && bx0 <= ax1 && ay0 <= by1 && by0 <= ay1; };
    // Same CT32 buffer: compare pixels; otherwise compare block ranges (conservative).
    auto writeHitsWrite = [&](const BatchedSprite &a, const BatchedSprite &b)
    {
        if (a.fbBlock == b.fbBlock && a.fbWidth == b.fbWidth)
            return rectsOverlap(a.x0, a.y0, a.x1, a.y1, b.x0, b.y0, b.x1, b.y1);
        return blocksOverlap(a.writeStart, a.writeEnd, b.writeStart, b.writeEnd);
    };
    auto readHitsWrite = [&](const BatchedSprite &reader, const BatchedSprite &writer)
    {
        if (!reader.readsTexture)
            return false;
        if (reader.texBlock == writer.fbBlock && reader.texWidth == writer.fbWidth)
            return rectsOverlap(reader.u0, reader.v0, reader.u1, reader.v1, writer.x0, writer.y0, writer.x1, writer.y1);
        return blocksOverlap(reader.readStart, reader.readEnd, writer.writeStart, writer.writeEnd);
    };
    bool dependent = v.spriteBatch.size() >= kMaxBatchedSprites;
    for (size_t i = 0; i < v.spriteBatch.size() && !dependent; ++i)
    {
        const BatchedSprite &earlier = v.spriteBatch[i];
        dependent = writeHitsWrite(entry, earlier) || readHitsWrite(entry, earlier) ||
                    (earlier.readsVram && readHitsWrite(earlier, entry));
    }
    if (dependent)
        v.flushSprites();

    const uint32_t uvNeeded = static_cast<uint32_t>(job.columnU.size() + job.rowV.size());
    // Room for this sprite's columns/rows and for every descriptor of the batch.
    const uint32_t descNeeded = static_cast<uint32_t>(v.spriteDescs.size()) + kSpriteDescWords;
    if (v.recording && v.uvUsed + uvNeeded + descNeeded > v.uvLimit())
        Submit(); // flushes the batch first
    v.beginIfNeeded();
    if (v.spriteBatch.empty())
        v.emitTileBatch(); // earlier triangles come first

    uint32_t uvOffsetU = 0u, uvOffsetV = 0u;
    if (uvNeeded)
    {
        auto *uv = static_cast<int32_t *>(v.uv.mapped) + v.uvUsed;
        streamCopy(uv, job.columnU.data(), job.columnU.size() * 4u);
        streamCopy(uv + job.columnU.size(), job.rowV.data(), job.rowV.size() * 4u);
        uvOffsetU = v.uvUsed;
        uvOffsetV = v.uvUsed + static_cast<uint32_t>(job.columnU.size());
        v.uvUsed += uvNeeded;
    }
    const uint32_t desc[kSpriteDescWords] = {static_cast<uint32_t>(job.x0), static_cast<uint32_t>(job.y0),
                                             static_cast<uint32_t>(job.x1), static_cast<uint32_t>(job.y1),
                                             job.fbBlock, job.fbWidth, job.source, job.alphaMode, job.alphaFix,
                                             job.flags, job.texBlock, job.texWidth, uvOffsetU, uvOffsetV, 0u, 0u};
    v.spriteDescs.insert(v.spriteDescs.end(), desc, desc + kSpriteDescWords);
    v.spritePages.insert(v.spritePages.end(), job.snapshotPages.begin(), job.snapshotPages.end());
    v.spriteMaxW = std::max(v.spriteMaxW, job.x1 - job.x0 + 1);
    v.spriteMaxH = std::max(v.spriteMaxH, job.y1 - job.y0 + 1);
    v.spriteBatch.push_back(entry);

    v.notePending(entry.writeStart, entry.writeEnd, true);
    if (entry.readsTexture)
        v.notePending(entry.readStart, entry.readEnd, false);
}

bool GSGpuAccelerator::QueueUpload(const GSGpuUpload &upload, const uint8_t *data, uint32_t sizeBytes, bool mirrored)
{
    Impl &v = *m_impl;
    v.flushSprites(); // queued sprites come first
    const uint32_t words = (sizeBytes + 3u) / 4u;
    if (upload.count == 0u || words > kUvCapacity)
        return false;
    if (v.recording && v.uvUsed + words > v.uvLimit())
        Submit();
    v.beginIfNeeded();
    v.emitTileBatch(); // earlier triangles come first
    auto *ring = static_cast<uint32_t *>(v.uv.mapped) + v.uvUsed;
    ring[words - 1u] = 0u;
    std::memcpy(ring, data, sizeBytes);
    TilesPush push{0u, 0u, upload.block, upload.width, upload.psm, upload.x, upload.y, upload.rowWidth,
                   upload.firstPixel, upload.count, v.uvUsed};
    v.uvUsed += words;
    vkCmdBindPipeline(v.commands, VK_PIPELINE_BIND_POINT_COMPUTE, v.uploadPipeline);
    vkCmdPushConstants(v.commands, v.pipelineLayout, kPushStages, 0, sizeof(push), &push);
    vkCmdDispatch(v.commands, (upload.count + kTileGroup - 1u) / kTileGroup, 1, 1);
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
    // A mirrored upload stays a pending read: later host writes there still wait for it.
    v.dropTextures(upload.destStart, upload.destEnd);
    v.notePending(upload.destStart, upload.destEnd, !mirrored);
    return true;
}

void GSGpuAccelerator::QueueLocalCopy(const GSGpuCopy &copy)
{
    Impl &v = *m_impl;
    v.flushSprites(); // queued sprites come first
    if (copy.dest.count == 0u)
        return;
    v.beginIfNeeded();
    v.emitTileBatch(); // earlier triangles come first
    TilesPush push{0u, 0u, copy.dest.block, copy.dest.width, copy.dest.psm, copy.dest.x, copy.dest.y, copy.dest.rowWidth,
                   0u, copy.dest.count, copy.dir, copy.block, copy.width, copy.psm, copy.x, copy.y};
    vkCmdBindPipeline(v.commands, VK_PIPELINE_BIND_POINT_COMPUTE, v.copyPipeline);
    vkCmdPushConstants(v.commands, v.pipelineLayout, kPushStages, 0, sizeof(push), &push);
    vkCmdDispatch(v.commands, (copy.dest.count + kTileGroup - 1u) / kTileGroup, 1, 1);
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
    v.notePending(copy.sourceStart, copy.sourceEnd, false);
    v.notePending(copy.dest.destStart, copy.dest.destEnd, true);
}

void GSGpuAccelerator::SetClut(const uint16_t *entries512, const uint64_t *unknown)
{
    // Primitives already queued keep their CLUT: a change gets a new version slot.
    Impl &v = *m_impl;
    std::array<uint64_t, 8> mask{};
    if (unknown)
        std::copy(unknown, unknown + 8, mask.begin());
    if (mask == v.clutUnknown && std::equal(entries512, entries512 + 512, v.currentClut.begin()))
        return; // the game reloads the same CLUT before most draws
    std::copy(entries512, entries512 + 512, v.currentClut.begin());
    v.clutHash = 0;
    v.clutUnknown = mask;
    for (uint32_t i = 0; i < 512u; ++i)
        if (!((mask[i >> 6] >> (i & 63u)) & 1u))
            v.clutEntryId[i] = 0u;
    // Outside a submission only currentClut changes: the slot's first version is written
    // when the next one begins, never into a version an in-flight batch still reads.
    if (!v.recording)
        return;
    if (v.clutVersion + 1u >= v.clutLimit())
        Submit();
    else
    {
        ++v.clutVersion;
        v.writeClut(v.clutVersion);
    }
}

void GSGpuAccelerator::QueueClutLoad(const GSGpuClutLoad &load, const uint64_t *unknown)
{
    Impl &v = *m_impl;
    uint64_t id = v.sourceGeneration(load.sourceStart, load.sourceEnd);
    for (const uint32_t value : {load.cbp, load.cpsm, load.csm, load.sourceWidth, load.cou, load.cov,
                                 load.firstEntry, load.entryCount, load.destinationBase, uint32_t(load.csm1Suffix)})
        id = (id ^ value) * 0x9E3779B97F4A7C15ull, id ^= id >> 31;
    id |= 1u;
    const bool sixteenBit = load.cpsm == 0x02u || load.cpsm == 0x0Au;
    bool changes = false;
    for (uint32_t pass = 0; pass < 2u; ++pass) // first see whether it changes anything, then mark
        for (uint32_t entry = load.firstEntry; entry < load.entryCount; ++entry)
        {
            const uint32_t destination = (load.csm1Suffix ? entry : load.destinationBase + entry) & (sixteenBit ? 0x1FFu : 0x0FFu);
            for (uint32_t half = 0; half < (sixteenBit ? 1u : 2u); ++half)
            {
                uint64_t &entryId = v.clutEntryId[destination + half * 256u];
                if (pass == 0u)
                    changes = changes || entryId != id;
                else
                    entryId = id;
            }
            if (pass == 0u && changes)
                break;
        }
    if (!changes)
        return; // same source, unwritten since: same entries
    v.flushSprites(); // the load reads what earlier draws write
    if (v.recording && v.clutVersion + 1u >= v.clutLimit())
        Submit();
    v.beginIfNeeded();
    std::copy(unknown, unknown + 8, v.clutUnknown.begin());
    v.clutHash = 0;
    if (const auto done = v.clutLoadVersions.find(id); done != v.clutLoadVersions.end())
    {
        // Loaded already in this submission: point its entries at that result.
        for (uint32_t entry = load.firstEntry; entry < load.entryCount; ++entry)
        {
            const uint32_t destination = (load.csm1Suffix ? entry : load.destinationBase + entry) & (sixteenBit ? 0x1FFu : 0x0FFu);
            for (uint32_t half = 0; half < (sixteenBit ? 1u : 2u); ++half)
                v.clutEntryVersion[destination + half * 256u] = done->second;
        }
        ++v.clutVersion;
        v.writeClut(v.clutVersion);
        return;
    }
    v.emitTileBatch();
    TilesPush push{0u, 0u};
    push.upBlock = load.cbp;
    push.upWidth = load.sourceWidth;
    push.upPsm = load.cpsm;
    push.upX = load.csm;
    push.upY = load.cou;
    push.upRowWidth = load.cov;
    push.srcPsm = load.entryCount;
    push.srcX = load.firstEntry;
    push.srcY = load.destinationBase | (load.csm1Suffix ? 0x10000u : 0u);
    ++v.clutVersion;
    v.dispatchClut(v.clutVersion - 1u, v.clutVersion, push);
    v.clutEntryVersion.fill(v.clutVersion); // the pass resolved every entry into it
    v.clutLoadVersions[id] = v.clutVersion;
    v.notePending(load.sourceStart, load.sourceEnd, false);
}

void GSGpuAccelerator::ReadClut(uint16_t *entries512)
{
    Impl &v = *m_impl;
    if (v.clutHasUnknown())
    {
        // After Flush the last submission copied its CLUT to the carry version.
        const auto *words = static_cast<const uint32_t *>(v.clut.mapped) + Impl::kClutCarry * 512u;
        for (uint32_t i = 0; i < 512u; ++i)
            v.currentClut[i] = static_cast<uint16_t>(words[i]);
        v.clutUnknown.fill(0u);
        v.clutEntryId.fill(0u);
        v.clutHash = 0;
    }
    std::copy(v.currentClut.begin(), v.currentClut.end(), entries512);
}

void GSGpuAccelerator::HostWrote(uint32_t start, uint32_t end)
{
    Impl &v = *m_impl;
    v.dropTextures(start, end);
    v.noteSourceWrite(start, end);
    if (!v.mirror)
        return;
    if (!v.recording)
    {
        Impl::mergeRange(v.hostDirty, start, end);
        return;
    }
    // Queued work does not touch these blocks (the caller checked Touches), so the upload
    // can run now; later host writes there must flush first, like for any pending read.
    v.copyVram(v.vram.buffer, v.deviceVram.buffer, {{start, end}});
    v.notePending(start, end, false);
}

bool GSGpuAccelerator::Touches(uint32_t start, uint32_t end, bool forWrite) const
{
    const Impl &v = *m_impl;
    auto clash = [&](const std::vector<std::pair<uint32_t, uint32_t>> &ranges)
    {
        for (const auto &range : ranges)
            if (start < range.second && range.first < end)
                return true;
        return false;
    };
    for (int kind = forWrite ? 0 : 1; kind < 2; ++kind)
    {
        if (v.recording && clash(v.pendingRanges[kind]))
            return true;
        for (uint32_t index = 0; index < kSlots; ++index)
            if (v.inFlight[index] && clash(v.flightRanges[index][kind]))
                return true;
    }
    return false;
}

void GSGpuAccelerator::QueuePrimitive(const GSGpuPrimitive &primitive, const GSGpuAccess &access)
{
    Impl &v = *m_impl;
    v.flushSprites(); // queued sprites come first
    const uint32_t uvNeeded = static_cast<uint32_t>(primitive.columns.size() + primitive.rows.size());
    const int minX = static_cast<int>(primitive.words[1]), maxX = static_cast<int>(primitive.words[2]);
    const int minY = static_cast<int>(primitive.words[3]), maxY = static_cast<int>(primitive.words[4]);
    const bool covers = minX <= maxX && minY <= maxY && minX >= 0 && minY >= 0;
    const uint32_t tx0 = covers ? static_cast<uint32_t>(minX) / kTileSize : 1u, ty0 = covers ? static_cast<uint32_t>(minY) / kTileSize : 1u;
    const uint32_t tx1 = covers ? std::min<uint32_t>(static_cast<uint32_t>(maxX) / kTileSize, kTileGrid - 1u) : 0u;
    const uint32_t ty1 = covers ? std::min<uint32_t>(static_cast<uint32_t>(maxY) / kTileSize, kTileGrid - 1u) : 0u;
    const uint32_t newTiles = covers ? (tx1 - tx0 + 1u) * (ty1 - ty0 + 1u) : 0u;
    // Headers (4 words) plus one index per tile, for what is pending and this primitive.
    const uint64_t tileWordsNeeded = uint64_t(v.tileWordsUsed) + (v.activeTiles.size() + newTiles) * 4u + v.pendingListWords + newTiles;
    if (v.recording && (v.recordsUsed + 1u > v.recordLimit() || v.uvUsed + uvNeeded > v.uvLimit() ||
                        tileWordsNeeded > v.tileWordsLimit()))
        Submit();
    v.beginIfNeeded();

    // Inside one dispatch, pixels run in parallel: a primitive that reads memory an
    // earlier one wrote, or writes it through another layout, starts a new dispatch.
    auto overlaps = [](uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a0 < b1 && b0 < a1; };
    bool split = false;
    for (const BatchWrite &write : v.batchWrites)
    {
        if (access.textureEnd > access.textureStart && overlaps(access.textureStart, access.textureEnd, write.start, write.end))
            split = true;
        if (overlaps(access.frameStart, access.frameEnd, write.start, write.end) &&
            (write.kind != 0u || write.width != access.frameWidth || write.psm != access.framePsm))
            split = true;
        if (access.depthEnd > access.depthStart && overlaps(access.depthStart, access.depthEnd, write.start, write.end) &&
            (write.kind != 1u || write.width != access.frameWidth || write.psm != access.depthPsm))
            split = true;
    }
    // Writing memory an earlier primitive of the batch samples: its pixel may run first.
    for (const auto &read : v.batchReads)
        if (overlaps(access.frameStart, access.frameEnd, read.first, read.second) ||
            (access.depthEnd > access.depthStart && overlaps(access.depthStart, access.depthEnd, read.first, read.second)))
            split = true;
    if (split)
        v.emitTileBatch();
    if (access.textureEnd > access.textureStart &&
        std::find(v.batchReads.begin(), v.batchReads.end(), std::make_pair(access.textureStart, access.textureEnd)) == v.batchReads.end())
        v.batchReads.emplace_back(access.textureStart, access.textureEnd);

    struct { uint32_t words[64]; } copy;
    std::memcpy(copy.words, primitive.words, sizeof(copy.words));
    if (!primitive.columns.empty() || !primitive.rows.empty())
    {
        auto *uv = static_cast<uint32_t *>(v.uv.mapped) + v.uvUsed;
        streamCopy(uv, primitive.columns.data(), primitive.columns.size() * 4u);
        streamCopy(uv + primitive.columns.size(), primitive.rows.data(), primitive.rows.size() * 4u);
        copy.words[35] = v.uvUsed;
        copy.words[36] = v.uvUsed + static_cast<uint32_t>(primitive.columns.size());
        v.uvUsed += uvNeeded;
    }
    copy.words[58] = v.clutVersion;
    // Texture cache: words[59] = texel offset + 1 of the decoded texture, 0 = sample VRAM.
    // After the split above, no primitive still pending in the batch writes this texture,
    // so decoding now (ahead of the batch's dispatch) reads the memory it would see.
    copy.words[59] = 0u;
    bool decode = false;
    const uint32_t texels = copy.words[23] * copy.words[24];
    if (access.textureEnd > access.textureStart && texels != 0u && texels <= kMaxCachedTexels)
    {
        const uint32_t psm = copy.words[17];
        const bool paletted = psm == 0x13u || psm == 0x1Bu || psm == 0x14u || psm == 0x24u || psm == 0x2Cu;
        if (paletted && v.clutHash == 0u)
        {
            // Four entries per step: this hash ran on every CLUT change and cost ~1/5 of QueuePrimitive.
            uint64_t h = 14695981039346656037ull;
            const bool unknownEntries = v.clutHasUnknown();
            for (size_t i = 0; i < v.currentClut.size(); i += 4u)
            {
                uint64_t w;
                std::memcpy(&w, &v.currentClut[i], sizeof(w));
                if (unknownEntries) // a GPU-only entry: its load id, not the stale host value
                    for (uint32_t k = 0; k < 4u; ++k)
                        if ((v.clutUnknown[(i + k) >> 6] >> ((i + k) & 63u)) & 1u)
                            w = (w ^ v.clutEntryId[i + k]) * 0xC2B2AE3D27D4EB4Full;
                h = (h ^ w) * 0x9E3779B97F4A7C15ull;
                h ^= h >> 29;
            }
            v.clutHash = h | 1u;
        }
        // shortcut: CLUT matched by a 64-bit hash, not compared entry by entry; a collision
        // would sample the wrong palette. Keep the 512 entries in the key if that ever shows up.
        const TexKey key{{copy.words[15], copy.words[16], psm, paletted ? copy.words[20] : 0u, paletted ? copy.words[21] : 0u,
                          paletted ? copy.words[22] : 0u, copy.words[23], copy.words[24], copy.words[28]},
                         paletted ? v.clutHash : 0u};
        if (v.lastTexKey != nullptr && *v.lastTexKey == key)
            copy.words[59] = v.lastTexEntry->offset + 1u;
        else if (const auto found = v.texEntries.find(key); found != v.texEntries.end())
        {
            copy.words[59] = found->second.offset + 1u;
            v.lastTexKey = &found->first, v.lastTexEntry = &found->second;
        }
        else if (v.texCacheUsed + texels <= (v.slot + 1u) * kTexCacheTexels)
        {
            const auto inserted = v.texEntries.emplace(key, TexEntry{v.texCacheUsed, access.textureStart, access.textureEnd}).first;
            v.lastTexKey = &inserted->first, v.lastTexEntry = &inserted->second;
            v.texLo = std::min(v.texLo, access.textureStart), v.texHi = std::max(v.texHi, access.textureEnd);
            copy.words[59] = v.texCacheUsed + 1u;
            v.texCacheUsed += texels;
            decode = true;
        }
    }
    streamCopy(static_cast<uint32_t *>(v.records.mapped) + v.recordsUsed * kRecordWords, copy.words, sizeof(copy.words));
    if (decode)
    {
        TilesPush push{0u, v.recordsUsed};
        push.upFirstPixel = copy.words[59] - 1u;
        push.upCount = texels;
        vkCmdBindPipeline(v.commands, VK_PIPELINE_BIND_POINT_COMPUTE, v.decodePipeline);
        vkCmdPushConstants(v.commands, v.pipelineLayout, kPushStages, 0, sizeof(push), &push);
        vkCmdDispatch(v.commands, (texels + kTileGroup - 1u) / kTileGroup, 1, 1);
        v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT);
    }
    v.notePending(access.frameStart, access.frameEnd, true);
    v.notePending(access.depthStart, access.depthEnd, true);
    v.notePending(access.textureStart, access.textureEnd, false);
    v.notePending(access.depthReadStart, access.depthReadEnd, false);

    const uint32_t local = v.recordsUsed - v.batchRecordBase;
    if (covers && !v.raster) // the rasterizer bins in hardware
    {
        v.pendingListWords += newTiles;
        // Triangles: skip bounding-box tiles no pixel center of which the triangle reaches.
        // Barycentrics are linear, so each one's maximum over a tile is at a corner. The tile
        // is grown by a pixel and the bound loosened well past the shader's float error, so
        // a tile that might draw is never dropped.
        const bool triangle = copy.words[0] == 0u;
        float fx2 = 0.0f, fy2 = 0.0f, s0x = 0.0f, s0y = 0.0f, s1x = 0.0f, s1y = 0.0f;
        if (triangle)
        {
            std::memcpy(&fx2, &copy.words[30], 4); std::memcpy(&fy2, &copy.words[31], 4);
            std::memcpy(&s0x, &copy.words[32], 4); std::memcpy(&s0y, &copy.words[33], 4);
            std::memcpy(&s1x, &copy.words[34], 4); std::memcpy(&s1y, &copy.words[35], 4);
        }
        auto reaches = [&](uint32_t tx, uint32_t ty)
        {
            if (!triangle)
                return true;
            const double dx[2] = {double(tx * kTileSize) - 0.5 - fx2, double(tx * kTileSize + kTileSize) + 0.5 - fx2};
            const double dy[2] = {double(ty * kTileSize) - 0.5 - fy2, double(ty * kTileSize + kTileSize) + 0.5 - fy2};
            double max0 = -1e30, max1 = -1e30, max2 = -1e30, magnitude = 0.0;
            for (const double cx : dx)
                for (const double cy : dy)
                {
                    const double w0 = s0x * cx + s0y * cy, w1 = s1x * cx + s1y * cy, w2 = 1.0 - w0 - w1;
                    max0 = std::max(max0, w0);
                    max1 = std::max(max1, w1);
                    max2 = std::max(max2, w2);
                    magnitude = std::max({magnitude, std::fabs(w0), std::fabs(w1)});
                }
            const double bound = -(1e-3 + 1e-4 * magnitude);
            return max0 >= bound && max1 >= bound && max2 >= bound;
        };
        for (uint32_t ty = ty0; ty <= ty1; ++ty)
            for (uint32_t tx = tx0; tx <= tx1; ++tx)
            {
                if (!reaches(tx, ty))
                    continue;
                std::vector<uint32_t> &list = v.tileLists[ty * kTileGrid + tx];
                if (list.empty())
                    v.activeTiles.push_back(ty * kTileGrid + tx);
                list.push_back(local);
            }
    }
    ++v.recordsUsed;
    auto remember = [&](const BatchWrite &write)
    {
        for (const BatchWrite &known : v.batchWrites)
            if (known.start == write.start && known.end == write.end && known.width == write.width &&
                known.psm == write.psm && known.kind == write.kind)
                return;
        v.batchWrites.push_back(write);
    };
    remember({access.frameStart, access.frameEnd, access.frameWidth, access.framePsm, 0u});
    if (access.depthEnd > access.depthStart)
        remember({access.depthStart, access.depthEnd, access.frameWidth, access.depthPsm, 1u});
}

void GSGpuAccelerator::Flush()
{
    Submit();
    for (uint32_t index = 0; index < kSlots; ++index)
        m_impl->waitSlot(index);
}

void GSGpuAccelerator::QueueCapture()
{
    Impl &v = *m_impl;
    v.flushSprites(); // queued sprites come first
    // CPU draws since this submission began are only on the host: send them up first.
    if (v.mirror && v.recording && !v.hostDirty.empty())
        Submit();
    v.beginIfNeeded();
    v.emitTileBatch();
    const uint32_t captureSlot = v.slot;
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    // shortcut: the window shows MKSM's frame pages 0x00 or 0x46 (FBW 10, 224 lines = 70 pages
    // each), so pages 0-139 are enough; copy the whole VRAM again if a game displays elsewhere.
    const VkBufferCopy region{0u, 0u, std::min<VkDeviceSize>(v.vramSize, kCaptureBytes)};
    vkCmdCopyBuffer(v.commands, v.gpuVram(), v.capture[captureSlot].buffer, 1u, &region);
    Submit(); // ends with a transfer -> host barrier
    v.captureInSlot[captureSlot] = true;
    v.captureDone[captureSlot] = false;
    v.captureSeq[captureSlot] = ++v.nextCaptureSeq;
}

bool GSGpuAccelerator::TakeCapture(uint8_t *dst)
{
    Impl &v = *m_impl;
    int newest = -1;
    for (uint32_t s = 0; s < kSlots; ++s)
    {
        if (!v.captureInSlot[s])
            continue;
        if (!v.captureDone[s] && v.inFlight[s] && vkGetFenceStatus(v.device, v.slotFences[s]) == VK_SUCCESS)
            v.captureDone[s] = true;
        if (v.captureDone[s] && (newest < 0 || v.captureSeq[s] > v.captureSeq[newest]))
            newest = static_cast<int>(s);
    }
    if (newest < 0)
        return false;
    std::memcpy(dst, v.capture[newest].mapped, std::min<size_t>(v.vramSize, kCaptureBytes));
    for (uint32_t s = 0; s < kSlots; ++s) // an older finished capture is stale now
        if (v.captureDone[s] && v.captureSeq[s] <= v.captureSeq[newest])
            v.captureInSlot[s] = v.captureDone[s] = false;
    return true;
}

void GSGpuAccelerator::Submit()
{
    Impl &v = *m_impl;
    if (!v.recording)
        return;
    v.flushSprites();
    v.emitTileBatch();
    if (v.clutHasUnknown()) // the next submission starts from this CLUT
        v.dispatchClut(v.clutVersion, Impl::kClutCarry, TilesPush{0u, 0u});
    if (v.mirror)
        v.copyVram(v.deviceVram.buffer, v.vram.buffer, v.pendingRanges[1]);
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT);
    if (v.timeQueries)
        vkCmdWriteTimestamp(v.commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, v.timeQueries, v.slot * 2u + 1u);
    vkEndCommandBuffer(v.commands);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &v.commands;
    _mm_sfence(); // the streaming record stores must land before the GPU reads them
    vkQueueSubmit(v.queue, 1, &submit, v.slotFences[v.slot]);
    v.inFlight[v.slot] = true;
    v.flightRanges[v.slot][0].swap(v.pendingRanges[0]);
    v.flightRanges[v.slot][1].swap(v.pendingRanges[1]);
    v.pendingRanges[0].clear();
    v.pendingRanges[1].clear();
    v.recording = false;
    v.slot = (v.slot + 1u) % kSlots;
}

#else // !PS2X_HAVE_VULKAN

struct GSGpuAccelerator::Impl
{
};
GSGpuAccelerator::GSGpuAccelerator() = default;
GSGpuAccelerator::~GSGpuAccelerator() = default;
std::unique_ptr<GSGpuAccelerator> GSGpuAccelerator::CreateFromEnvironment(uint32_t) { return nullptr; }
bool GSGpuAccelerator::Initialize(uint32_t) { return false; }
uint8_t *GSGpuAccelerator::vram() const { return nullptr; }
void GSGpuAccelerator::QueueSprite(const GSGpuSpriteJob &) {}
void GSGpuAccelerator::QueuePrimitive(const GSGpuPrimitive &, const GSGpuAccess &) {}
bool GSGpuAccelerator::QueueUpload(const GSGpuUpload &, const uint8_t *, uint32_t, bool) { return false; }
void GSGpuAccelerator::QueueLocalCopy(const GSGpuCopy &) {}
void GSGpuAccelerator::SetClut(const uint16_t *, const uint64_t *) {}
void GSGpuAccelerator::QueueClutLoad(const GSGpuClutLoad &, const uint64_t *) {}
void GSGpuAccelerator::ReadClut(uint16_t *) {}
void GSGpuAccelerator::HostWrote(uint32_t, uint32_t) {}
bool GSGpuAccelerator::Touches(uint32_t, uint32_t, bool) const { return false; }
void GSGpuAccelerator::Flush() {}
void GSGpuAccelerator::QueueCapture() {}
bool GSGpuAccelerator::TakeCapture(uint8_t *) { return false; }
void GSGpuAccelerator::Submit() {}
bool GSGpuAccelerator::HasPendingWork() const { return false; }

#endif
