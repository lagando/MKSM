#include "gs_gpu_accelerator.h"

#if PS2X_HAVE_VULKAN

#include "runtime/gs/ps2_gs_memory.h"

#include <vulkan/vulkan.h>

#include <algorithm>
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

    constexpr VkDeviceSize kPageBytes = 8192u;
    constexpr uint32_t kUvCapacity = 1u << 20; // int32 texel coordinates per submission
    constexpr uint32_t kRecordWords = 64u;
    constexpr uint32_t kRecordCapacity = 1u << 15; // primitives per submission
    constexpr uint32_t kTileWordsCapacity = 1u << 22;
    constexpr uint32_t kTileSize = 8u;              // must match kTileSize in gs_tiles.comp
    constexpr uint32_t kTileGroup = kTileSize * kTileSize;
    constexpr uint32_t kTileGrid = 2048u / kTileSize; // tiles over the 2048x2048 GS space
    constexpr uint32_t kTableWords = 45056u;
    constexpr uint32_t kBindings = 8u;
    constexpr uint32_t kClutVersions = 256u; // CLUT changes per submission
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

    struct SpritePush
    {
        int32_t drawRect[4];
        uint32_t fbBlock, fbWidth, source, alphaMode, alphaFix, flags, texBlock, texWidth, uvOffsetU, uvOffsetV;
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
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    // Two submission slots: the CPU fills one while the GPU runs the other. Each owns a
    // command buffer, a fence and its own half of the uv/record/tile/CLUT rings.
    VkCommandBuffer slotCommands[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkFence slotFences[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    bool inFlight[2] = {false, false};
    std::vector<std::pair<uint32_t, uint32_t>> flightRanges[2][2]; // [slot][read, written]
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

    void waitSlot(uint32_t index)
    {
        if (!inFlight[index])
            return;
        vkWaitForFences(device, 1, &slotFences[index], VK_TRUE, UINT64_MAX);
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
    // Decoded textures of the recording slot; any write to their blocks drops them.
    std::unordered_map<TexKey, TexEntry, TexKeyHash> texEntries;
    uint32_t texCacheUsed = 0;

    void dropTextures(uint32_t start, uint32_t end)
    {
        if (end <= start)
            return;
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
            dropTextures(start, end);
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

    void writeClut(uint32_t version)
    {
        auto *clutWords = static_cast<uint32_t *>(clut.mapped) + version * 512u;
        for (uint32_t i = 0; i < 512u; ++i)
            clutWords[i] = currentClut[i];
    }

    void emitTileBatch()
    {
        const uint32_t count = recordsUsed - batchRecordBase;
        if (count == 0u)
            return;
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
            vkCmdPushConstants(commands, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
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

    void memoryBarrier(VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
    {
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
        recording = true;
        pendingRanges[0].clear();
        pendingRanges[1].clear();
        clutVersion = slot * kClutVersions;
        writeClut(clutVersion);
        texEntries.clear();
        texCacheUsed = slot * kTexCacheTexels;
        uvUsed = slot * kUvCapacity;
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
    vkDestroyPipelineLayout(v.device, v.pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(v.device, v.setLayout, nullptr);
    v.destroyBuffer(v.vram);
    v.destroyBuffer(v.deviceVram);
    v.destroyBuffer(v.snapshot);
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

    vkGetPhysicalDeviceQueueFamilyProperties(v.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(v.physical, &count, families.data());
    bool found = false;
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
    if (vkCreateDevice(v.physical, &deviceInfo, nullptr, &v.device) != VK_SUCCESS)
        return false;
    vkGetDeviceQueue(v.device, v.queueFamily, 0, &v.queue);

    v.vramSize = vramSize;
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkBufferUsageFlags copies = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!v.createBuffer(v.vram, vramSize, storage | copies))
        return false;
    v.mirror = (v.vram.flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0u;
    if ((v.mirror && !v.createBuffer(v.deviceVram, vramSize, storage | copies, true)) ||
        !v.createBuffer(v.snapshot, vramSize, storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true) ||
        !v.createBuffer(v.swizzle, kTableWords * 4u, storage, true) ||
        !v.createBuffer(v.uv, 2u * kUvCapacity * 4u, storage, true) ||
        !v.createBuffer(v.records, 2u * kRecordCapacity * kRecordWords * 4u, storage, true) ||
        !v.createBuffer(v.tiles, 2u * kTileWordsCapacity * 4u, storage, true) ||
        !v.createBuffer(v.clut, 2u * kClutVersions * 512u * 4u, storage, true) ||
        !v.createBuffer(v.texCache, 2u * kTexCacheTexels * 4u, storage, false, true))
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
    std::memset(v.clut.mapped, 0, 2u * kClutVersions * 512u * 4u);

    VkDescriptorSetLayoutBinding bindings[kBindings]{};
    for (uint32_t i = 0; i < kBindings; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = kBindings;
    setInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(v.device, &setInfo, nullptr, &v.setLayout);
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, std::max<uint32_t>(sizeof(SpritePush), sizeof(TilesPush))};
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
        !createPipeline(kTilesShader, sizeof(kTilesShader), v.decodePipeline, 3u))
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
    commandInfo.commandBufferCount = 2;
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
    const uint32_t uvNeeded = static_cast<uint32_t>(job.columnU.size() + job.rowV.size());
    if (v.recording && v.uvUsed + uvNeeded > v.uvLimit())
        Submit();
    v.beginIfNeeded();
    v.emitTileBatch(); // earlier triangles come first
    vkCmdBindPipeline(v.commands, VK_PIPELINE_BIND_POINT_COMPUTE, v.spritePipeline);

    SpritePush push{{job.x0, job.y0, job.x1, job.y1}, job.fbBlock, job.fbWidth, job.source, job.alphaMode,
                    job.alphaFix, job.flags, job.texBlock, job.texWidth, 0u, 0u};
    if (uvNeeded)
    {
        auto *uv = static_cast<int32_t *>(v.uv.mapped) + v.uvUsed;
        std::memcpy(uv, job.columnU.data(), job.columnU.size() * 4u);
        std::memcpy(uv + job.columnU.size(), job.rowV.data(), job.rowV.size() * 4u);
        push.uvOffsetU = v.uvUsed;
        push.uvOffsetV = v.uvUsed + static_cast<uint32_t>(job.columnU.size());
        v.uvUsed += uvNeeded;
    }

    if (!job.snapshotPages.empty())
    {
        // The GS texture cache hands a primitive its source as it was before the draw.
        std::vector<VkBufferCopy> regions;
        regions.reserve(job.snapshotPages.size());
        for (const uint32_t page : job.snapshotPages)
            regions.push_back({page * kPageBytes, page * kPageBytes, kPageBytes});
        vkCmdCopyBuffer(v.commands, v.gpuVram(), v.snapshot.buffer, static_cast<uint32_t>(regions.size()), regions.data());
        v.memoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    vkCmdPushConstants(v.commands, v.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    v.notePending(job.fbBlock, job.fbBlock + job.fbWidth * (static_cast<uint32_t>(job.y1) / 32u + 1u) * 32u, true);
    if (job.flags & 2u)
    {
        const int32_t maxV = job.rowV.empty() ? 0 : *std::max_element(job.rowV.begin(), job.rowV.end());
        v.notePending(job.texBlock, job.texBlock + std::max<uint32_t>(job.texWidth, 1u) * (static_cast<uint32_t>(std::max(maxV, 0)) / 32u + 2u) * 32u, false);
    }
    vkCmdDispatch(v.commands, static_cast<uint32_t>((job.x1 - job.x0 + 8) / 8), static_cast<uint32_t>((job.y1 - job.y0 + 8) / 8), 1);
    // Draws run in GS order: the next one may blend over or sample these pixels.
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
}

bool GSGpuAccelerator::QueueUpload(const GSGpuUpload &upload, const uint8_t *data, uint32_t sizeBytes, bool mirrored)
{
    Impl &v = *m_impl;
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
    vkCmdPushConstants(v.commands, v.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
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
    if (copy.dest.count == 0u)
        return;
    v.beginIfNeeded();
    v.emitTileBatch(); // earlier triangles come first
    TilesPush push{0u, 0u, copy.dest.block, copy.dest.width, copy.dest.psm, copy.dest.x, copy.dest.y, copy.dest.rowWidth,
                   0u, copy.dest.count, copy.dir, copy.block, copy.width, copy.psm, copy.x, copy.y};
    vkCmdBindPipeline(v.commands, VK_PIPELINE_BIND_POINT_COMPUTE, v.copyPipeline);
    vkCmdPushConstants(v.commands, v.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(v.commands, (copy.dest.count + kTileGroup - 1u) / kTileGroup, 1, 1);
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
    v.notePending(copy.sourceStart, copy.sourceEnd, false);
    v.notePending(copy.dest.destStart, copy.dest.destEnd, true);
}

void GSGpuAccelerator::SetClut(const uint16_t *entries512)
{
    // Primitives already queued keep their CLUT: a change gets a new version slot.
    Impl &v = *m_impl;
    std::copy(entries512, entries512 + 512, v.currentClut.begin());
    v.clutHash = 0;
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

void GSGpuAccelerator::HostWrote(uint32_t start, uint32_t end)
{
    Impl &v = *m_impl;
    v.dropTextures(start, end);
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
        for (uint32_t index = 0; index < 2u; ++index)
            if (v.inFlight[index] && clash(v.flightRanges[index][kind]))
                return true;
    }
    return false;
}

void GSGpuAccelerator::QueuePrimitive(const GSGpuPrimitive &primitive, const GSGpuAccess &access)
{
    Impl &v = *m_impl;
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

    GSGpuPrimitive copy = primitive;
    if (!primitive.columns.empty() || !primitive.rows.empty())
    {
        auto *uv = static_cast<uint32_t *>(v.uv.mapped) + v.uvUsed;
        std::memcpy(uv, primitive.columns.data(), primitive.columns.size() * 4u);
        std::memcpy(uv + primitive.columns.size(), primitive.rows.data(), primitive.rows.size() * 4u);
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
            uint64_t h = 14695981039346656037ull;
            for (const uint16_t c : v.currentClut)
                h = (h ^ c) * 1099511628211ull;
            v.clutHash = h | 1u;
        }
        // shortcut: CLUT matched by a 64-bit hash, not compared entry by entry; a collision
        // would sample the wrong palette. Keep the 512 entries in the key if that ever shows up.
        const TexKey key{{copy.words[15], copy.words[16], psm, paletted ? copy.words[20] : 0u, paletted ? copy.words[21] : 0u,
                          paletted ? copy.words[22] : 0u, copy.words[23], copy.words[24], copy.words[28]},
                         paletted ? v.clutHash : 0u};
        const auto found = v.texEntries.find(key);
        if (found != v.texEntries.end())
            copy.words[59] = found->second.offset + 1u;
        else if (v.texCacheUsed + texels <= (v.slot + 1u) * kTexCacheTexels)
        {
            v.texEntries.emplace(key, TexEntry{v.texCacheUsed, access.textureStart, access.textureEnd});
            copy.words[59] = v.texCacheUsed + 1u;
            v.texCacheUsed += texels;
            decode = true;
        }
    }
    std::memcpy(static_cast<uint32_t *>(v.records.mapped) + v.recordsUsed * kRecordWords, copy.words, sizeof(copy.words));
    if (decode)
    {
        TilesPush push{0u, v.recordsUsed};
        push.upFirstPixel = copy.words[59] - 1u;
        push.upCount = texels;
        vkCmdBindPipeline(v.commands, VK_PIPELINE_BIND_POINT_COMPUTE, v.decodePipeline);
        vkCmdPushConstants(v.commands, v.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(v.commands, (texels + kTileGroup - 1u) / kTileGroup, 1, 1);
        v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT);
    }
    v.notePending(access.frameStart, access.frameEnd, true);
    v.notePending(access.depthStart, access.depthEnd, true);
    v.notePending(access.textureStart, access.textureEnd, false);
    v.notePending(access.depthReadStart, access.depthReadEnd, false);

    const uint32_t local = v.recordsUsed - v.batchRecordBase;
    if (covers)
    {
        v.pendingListWords += newTiles;
        for (uint32_t ty = ty0; ty <= ty1; ++ty)
            for (uint32_t tx = tx0; tx <= tx1; ++tx)
            {
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
    m_impl->waitSlot(0u);
    m_impl->waitSlot(1u);
}

void GSGpuAccelerator::Submit()
{
    Impl &v = *m_impl;
    if (!v.recording)
        return;
    v.emitTileBatch();
    if (v.mirror)
        v.copyVram(v.deviceVram.buffer, v.vram.buffer, v.pendingRanges[1]);
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT);
    vkEndCommandBuffer(v.commands);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &v.commands;
    vkQueueSubmit(v.queue, 1, &submit, v.slotFences[v.slot]);
    v.inFlight[v.slot] = true;
    v.flightRanges[v.slot][0].swap(v.pendingRanges[0]);
    v.flightRanges[v.slot][1].swap(v.pendingRanges[1]);
    v.pendingRanges[0].clear();
    v.pendingRanges[1].clear();
    v.recording = false;
    v.slot ^= 1u;
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
void GSGpuAccelerator::SetClut(const uint16_t *) {}
void GSGpuAccelerator::HostWrote(uint32_t, uint32_t) {}
bool GSGpuAccelerator::Touches(uint32_t, uint32_t, bool) const { return false; }
void GSGpuAccelerator::Flush() {}
void GSGpuAccelerator::Submit() {}
bool GSGpuAccelerator::HasPendingWork() const { return false; }

#endif
