#pragma once

#include <cstdint>
#include <memory>
#include <vector>

// Everything the GPU needs to draw one sprite exactly like the CPU fast path. Filled
// by GSCpuBackend::PrepareGpuSprite, which owns the geometry and UV arithmetic.
struct GSGpuSpriteJob
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // scissored, inclusive
    uint32_t fbBlock = 0, fbWidth = 1;
    uint32_t source = 0;                 // RGBA of v1
    uint32_t alphaMode = 0, alphaFix = 0;
    uint32_t flags = 0;                  // 1 ABE, 2 TME, 4 TCC, 8 texture from snapshot
    uint32_t texBlock = 0, texWidth = 0;
    std::vector<int32_t> columnU;        // texel U for x0..x1
    std::vector<int32_t> rowV;           // texel V for y0..y1
    std::vector<uint32_t> snapshotPages; // 8 KB pages copied before the draw (feedback sprites)
};

// One triangle or general sprite for the tile rasterizer (gs_tiles.comp). The 64-word
// layout is shared with the shader; GSCpuBackend::PrepareGpuPrimitive fills it.
struct GSGpuPrimitive
{
    uint32_t words[64] = {};
    std::vector<uint32_t> columns, rows; // sprites: texel U/S per column and V/T per row
};

// Memory one primitive writes or reads, in 64-word blocks, so a batch can be split when
// a later primitive reads what an earlier one wrote.
struct GSGpuAccess
{
    uint32_t frameStart = 0, frameEnd = 0, frameWidth = 0, framePsm = 0;
    uint32_t depthStart = 0, depthEnd = 0, depthPsm = 0; // depthEnd == depthStart: no depth write
    uint32_t depthReadStart = 0, depthReadEnd = 0;       // Z read by the depth test with ZMSK set
    uint32_t textureStart = 0, textureEnd = 0;           // textureEnd == textureStart: untextured
};

// Runs GS work on the GPU through Vulkan compute (PS2X_GS_GPU=1). The GS local memory
// lives in a host-visible buffer, so the CPU backend keeps drawing whatever the GPU
// does not handle yet straight into the same memory. Draws queued here are recorded
// into one command buffer and run on Flush(); the caller flushes before any CPU access.
class GSGpuAccelerator
{
public:
    // Null when PS2X_GS_GPU is unset or Vulkan cannot start (the CPU path is used then).
    static std::unique_ptr<GSGpuAccelerator> CreateFromEnvironment(uint32_t vramSize);
    ~GSGpuAccelerator();

    uint8_t *vram() const;
    void QueueSprite(const GSGpuSpriteJob &job);
    void QueuePrimitive(const GSGpuPrimitive &primitive, const GSGpuAccess &access);
    // The CLUT for primitives queued from now on (earlier ones keep theirs).
    void SetClut(const uint16_t *entries512);
    // The CPU wrote local memory blocks [start, end) of vram(): the GPU copy needs them.
    void HostWrote(uint32_t start, uint32_t end);
    // True when queued GPU work reads or writes local memory blocks [start, end): CPU
    // writes there (uploads) or reads (CLUT loads, readback) must Flush() first.
    // Reads only clash with pending writes; writes clash with both.
    bool Touches(uint32_t start, uint32_t end, bool forWrite) const;
    void Flush();
    bool HasPendingWork() const;

private:
    GSGpuAccelerator();
    bool Initialize(uint32_t vramSize);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
