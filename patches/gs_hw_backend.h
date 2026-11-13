#pragma once
// Track F (PLANO_DESEMPENHO): a conventional GPU renderer that batches primitives into plain
// draws instead of emulating the GS pixel by pixel. Step F1: a pass-through wrapper that counts
// the batches and state changes such a renderer would issue, to size the work.
#include "runtime/gs/gs_backend.h"

#include <cstdint>
#include <memory>

class GSHwBackend final : public GSRasterBackend
{
public:
    explicit GSHwBackend(GSRasterBackend &inner);

    // PS2X_GS_HW=count wraps the live backend; anything else returns it unchanged.
    static std::unique_ptr<GSRasterBackend> WrapFromEnvironment(std::unique_ptr<GSRasterBackend> inner);

    struct Stats
    {
        uint64_t primitives = 0, batches = 0, frames = 0;
        uint64_t breaksTarget = 0, breaksTexture = 0, breaksBlend = 0, breaksTest = 0, breaksOther = 0;
        uint64_t feedback = 0; // primitives that sample their own target
    };
    const Stats &stats() const { return m_stats; }
    void report() const;

    void Initialize(uint8_t *vram, uint32_t vramSize) override { m_inner.Initialize(vram, vramSize); }
    void Reset() override { m_inner.Reset(); }
    void Submit(const GSPrimitiveBatch &batch) override;
    void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) override { m_inner.LoadClut(tex0, texclut); }
    void BeginTransfer(const GSTransferCommand &command) override;
    void UploadImage(const uint8_t *data, uint32_t sizeBytes) override { m_inner.UploadImage(data, sizeBytes); }
    void Flush() override { m_inner.Flush(); }
    void TextureFlush() override { m_inner.TextureFlush(); }
    void Sync(GSSyncReason reason) override { m_inner.Sync(reason); }
    PresentationFrame Present(const GSPresentationRequest &request) override;
    bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override { return m_inner.ClearFramebuffer(context, rgba); }
    uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override { return m_inner.ConsumeLocalToHostBytes(dst, maxBytes); }
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override { return m_inner.ReadVram(psm, base, bw, x, y); }
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override { m_inner.WriteVram(psm, base, bw, x, y, value); }
    void SnapshotVram(std::vector<uint8_t> &out) const override { m_inner.SnapshotVram(out); }
    GSTransferSnapshot GetTransferSnapshot() const override { return m_inner.GetTransferSnapshot(); }

private:
    struct Key
    {
        uint64_t target = 0, texture = 0, blend = 0, test = 0, other = 0;
    };

    GSRasterBackend &m_inner;
    std::unique_ptr<GSRasterBackend> m_owned;
    Key m_last{};
    bool m_open = false;
    Stats m_stats{};
};
