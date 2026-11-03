#pragma once

#include "runtime/gs/gs_backend.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Records every command a GS backend receives, so a few frames of the real game can
// be replayed later without booting it (ps2x_gs_replay). The file holds the backend
// state at the first recorded frame, then the commands in order. Raw structs: a file
// is only valid for the build layout that wrote it (the header checks the sizes).
class GSRecordingBackend final : public GSRasterBackend
{
public:
    // Starts at the first frame end after startSeconds and stops after frameCount frames.
    GSRecordingBackend(std::unique_ptr<GSRasterBackend> inner, std::string path, double startSeconds, uint32_t frameCount);
    ~GSRecordingBackend() override;

    // PS2X_GS_RECORD=<file> [PS2X_GS_RECORD_AT=<s>] [PS2X_GS_RECORD_FRAMES=<n>] wraps the backend.
    static std::unique_ptr<GSRasterBackend> WrapFromEnvironment(std::unique_ptr<GSRasterBackend> inner);

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

    GSRasterBackend &inner() { return *m_inner; }

private:
    void startRecording();
    void writeRecord(uint8_t kind, const void *data, size_t size);

    std::unique_ptr<GSRasterBackend> m_inner;
    std::string m_path;
    std::chrono::steady_clock::time_point m_created;
    double m_startSeconds;
    uint32_t m_framesLeft;
    uint32_t m_lastFbp = 0u;
    std::FILE *m_file = nullptr;
    bool m_done = false;
    std::mutex m_mutex;
};

// A recording loaded in memory, replayable into any backend.
class GSRecording
{
public:
    bool Load(const std::string &path, std::string &error);

    // Puts the recorded starting VRAM and backend state into backend, which must
    // already be initialized over a VRAM of the recorded size.
    void RestoreStart(GSRasterBackend &backend) const;

    // Sends every command to backend; onFrameEnd(index) runs at each recorded frame end.
    void Replay(GSRasterBackend &backend, const std::function<void(uint32_t)> &onFrameEnd) const;

    const std::vector<uint8_t> &startVram() const { return m_vram; }
    uint32_t frameCount() const { return m_frames; }

private:
    std::vector<uint8_t> m_vram;
    std::vector<uint8_t> m_state;
    std::vector<uint8_t> m_records;
    uint32_t m_frames = 0u;
};
