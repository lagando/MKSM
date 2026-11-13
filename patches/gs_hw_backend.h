#pragma once
// Track F (PLANO_DESEMPENHO): a conventional GPU renderer that batches primitives into plain
// draws instead of emulating the GS pixel by pixel.
//   PS2X_GS_HW=count  pass-through that counts the batches such a renderer would issue (F1).
//   PS2X_GS_HW=1      draws on the GPU; a GSCpuBackend without its GPU path stays the VRAM
//                     mirror for transfers, CLUT, reads and presentation.
#include "runtime/gs/gs_backend.h"

#include <cstdint>
#include <memory>
#include <mutex>

class GSCpuBackend;

class GSHwBackend final : public GSRasterBackend
{
public:
    enum class Mode
    {
        Count,
        Render
    };

    // Count wraps any backend; Render needs a GSCpuBackend (its GPU path gets disabled).
    GSHwBackend(GSRasterBackend &inner, Mode mode);
    ~GSHwBackend() override;

    // Reads PS2X_GS_HW; returns inner unchanged when it is unset or the GPU is unavailable.
    static std::unique_ptr<GSRasterBackend> WrapFromEnvironment(std::unique_ptr<GSRasterBackend> inner);

    struct Stats
    {
        uint64_t primitives = 0, batches = 0, frames = 0;
        uint64_t breaksTarget = 0, breaksTexture = 0, breaksBlend = 0, breaksTest = 0, breaksOther = 0;
        uint64_t feedback = 0;  // primitives that sample their own target
        uint64_t fallback = 0;  // primitives the GPU path cannot draw (sent to the CPU mirror)
        uint64_t downloads = 0; // surface -> mirror copies (each waits for the GPU)
    };
    const Stats &stats() const { return m_stats; }
    void report() const;
    bool ready() const;
    // After the mirror was replaced behind our back (replay restore): drop all GPU copies.
    void InvalidateAll();

    void Initialize(uint8_t *vram, uint32_t vramSize) override;
    void Reset() override;
    void Submit(const GSPrimitiveBatch &batch) override;
    void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) override;
    void BeginTransfer(const GSTransferCommand &command) override;
    void UploadImage(const uint8_t *data, uint32_t sizeBytes) override;
    void Flush() override;
    void TextureFlush() override;
    void Sync(GSSyncReason reason) override;
    PresentationFrame Present(const GSPresentationRequest &request) override;
    bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override;
    uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override;
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override;
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override;
    void SnapshotVram(std::vector<uint8_t> &out) const override;
    GSTransferSnapshot GetTransferSnapshot() const override;

private:
    struct Key
    {
        uint64_t target = 0, texture = 0, blend = 0, test = 0, other = 0;
    };
    struct Renderer; // Vulkan state (gs_hw_backend.cpp)

    void countBatch(const GSPrimitiveBatch &batch);

    GSRasterBackend &m_inner;
    std::unique_ptr<GSRasterBackend> m_owned;
    Mode m_mode;
    Key m_last{};
    bool m_open = false;
    Stats m_stats{};
    std::unique_ptr<Renderer> m_renderer;
    mutable std::recursive_mutex m_lock;
};
