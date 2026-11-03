#include "gs_gpu_accelerator.h"

#if PS2X_HAVE_VULKAN

#include "runtime/gs/ps2_gs_memory.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace
{
    const uint32_t kSpriteShader[] = {
#include "gs_sprite.inc"
    };

    constexpr VkDeviceSize kPageBytes = 8192u;
    constexpr uint32_t kUvCapacity = 1u << 20; // int32 texel coordinates per submission

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
    };
    Buffer vram, snapshot, swizzle, uv;
    VkDeviceSize vramSize = 0;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline spritePipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    bool recording = false;
    uint32_t uvUsed = 0;

    bool createBuffer(Buffer &out, VkDeviceSize size, VkBufferUsageFlags usage)
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
        // Host-visible and coherent so the CPU backend can draw into the same memory;
        // device-local too when the GPU shares system memory (integrated).
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        int chosen = -1;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        {
            const VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
            if (!(requirements.memoryTypeBits & (1u << i)) || (flags & wanted) != wanted)
                continue;
            if (chosen < 0 || (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
                chosen = static_cast<int>(i);
        }
        if (chosen < 0)
            return false;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = static_cast<uint32_t>(chosen);
        if (vkAllocateMemory(device, &allocate, nullptr, &out.memory) != VK_SUCCESS)
            return false;
        vkBindBufferMemory(device, out.buffer, out.memory, 0);
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
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commands, &begin);
        // CPU draws since the last submission wrote this memory directly.
        memoryBarrier(VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, spritePipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        recording = true;
        uvUsed = 0;
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
    vkDestroyFence(v.device, v.fence, nullptr);
    vkDestroyCommandPool(v.device, v.commandPool, nullptr);
    vkDestroyDescriptorPool(v.device, v.descriptorPool, nullptr);
    vkDestroyPipeline(v.device, v.spritePipeline, nullptr);
    vkDestroyPipelineLayout(v.device, v.pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(v.device, v.setLayout, nullptr);
    v.destroyBuffer(v.vram);
    v.destroyBuffer(v.snapshot);
    v.destroyBuffer(v.swizzle);
    v.destroyBuffer(v.uv);
    vkDestroyDevice(v.device, nullptr);
    vkDestroyInstance(v.instance, nullptr);
}

std::unique_ptr<GSGpuAccelerator> GSGpuAccelerator::CreateFromEnvironment(uint32_t vramSize)
{
    const char *enabled = std::getenv("PS2X_GS_GPU");
    if (!enabled || enabled[0] != '1')
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
    // PS2X_GPU_DEVICE=<part of the name> picks one; otherwise the integrated GPU, whose
    // memory is the system RAM the CPU backend also draws into.
    const char *wanted = std::getenv("PS2X_GPU_DEVICE");
    int best = -1;
    for (uint32_t i = 0; i < count; ++i)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        const bool named = wanted && std::string(properties.deviceName).find(wanted) != std::string::npos;
        const bool integrated = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        if (named || (!wanted && (best < 0 || integrated)))
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
    if (!v.createBuffer(v.vram, vramSize, storage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
        !v.createBuffer(v.snapshot, vramSize, storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
        !v.createBuffer(v.swizzle, 2048u * 4u, storage) ||
        !v.createBuffer(v.uv, kUvCapacity * 4u, storage))
        return false;
    std::memset(v.vram.mapped, 0, vramSize);
    auto *swizzle = static_cast<uint32_t *>(v.swizzle.mapped);
    for (uint32_t y = 0; y < 32u; ++y)
        for (uint32_t x = 0; x < 64u; ++x)
            swizzle[y * 64u + x] = GSMem::PixelIndexCT32(0u, 1u, x, y); // block 0: BlockId * 64 + ColumnId

    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 4u; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 4;
    setInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(v.device, &setInfo, nullptr, &v.setLayout);
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SpritePush)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &v.setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    vkCreatePipelineLayout(v.device, &layoutInfo, nullptr, &v.pipelineLayout);

    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = sizeof(kSpriteShader);
    moduleInfo.pCode = kSpriteShader;
    VkShaderModule module;
    if (vkCreateShaderModule(v.device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
        return false;
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = v.pipelineLayout;
    const VkResult pipelineResult = vkCreateComputePipelines(v.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &v.spritePipeline);
    vkDestroyShaderModule(v.device, module, nullptr);
    if (pipelineResult != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
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
    VkDescriptorBufferInfo infos[4] = {{v.vram.buffer, 0, VK_WHOLE_SIZE}, {v.snapshot.buffer, 0, VK_WHOLE_SIZE},
                                       {v.swizzle.buffer, 0, VK_WHOLE_SIZE}, {v.uv.buffer, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet writes[4]{};
    for (uint32_t i = 0; i < 4u; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = v.descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(v.device, 4, writes, 0, nullptr);

    VkCommandPoolCreateInfo poolCreate{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCreate.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCreate.queueFamilyIndex = v.queueFamily;
    vkCreateCommandPool(v.device, &poolCreate, nullptr, &v.commandPool);
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = v.commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(v.device, &commandInfo, &v.commands);
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(v.device, &fenceInfo, nullptr, &v.fence);

    std::cerr << "[gs:gpu] " << properties.deviceName << ": sprites on the GPU" << std::endl;
    return true;
}

uint8_t *GSGpuAccelerator::vram() const
{
    return static_cast<uint8_t *>(m_impl->vram.mapped);
}

bool GSGpuAccelerator::HasPendingWork() const
{
    return m_impl->recording;
}

void GSGpuAccelerator::QueueSprite(const GSGpuSpriteJob &job)
{
    Impl &v = *m_impl;
    if (job.x0 > job.x1 || job.y0 > job.y1)
        return; // fully outside the scissor
    const uint32_t uvNeeded = static_cast<uint32_t>(job.columnU.size() + job.rowV.size());
    if (v.recording && v.uvUsed + uvNeeded > kUvCapacity)
        Flush();
    v.beginIfNeeded();

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
        vkCmdCopyBuffer(v.commands, v.vram.buffer, v.snapshot.buffer, static_cast<uint32_t>(regions.size()), regions.data());
        v.memoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    vkCmdPushConstants(v.commands, v.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(v.commands, static_cast<uint32_t>((job.x1 - job.x0 + 8) / 8), static_cast<uint32_t>((job.y1 - job.y0 + 8) / 8), 1);
    // Draws run in GS order: the next one may blend over or sample these pixels.
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
}

void GSGpuAccelerator::Flush()
{
    Impl &v = *m_impl;
    if (!v.recording)
        return;
    v.memoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT);
    vkEndCommandBuffer(v.commands);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &v.commands;
    vkQueueSubmit(v.queue, 1, &submit, v.fence);
    vkWaitForFences(v.device, 1, &v.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(v.device, 1, &v.fence);
    vkResetCommandBuffer(v.commands, 0);
    v.recording = false;
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
void GSGpuAccelerator::Flush() {}
bool GSGpuAccelerator::HasPendingWork() const { return false; }

#endif
