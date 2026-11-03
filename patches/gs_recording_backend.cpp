#include "runtime/gs/gs_recording_backend.h"
#include "runtime/gs/gs_cpu_backend.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

namespace
{
    constexpr char kMagic[8] = {'P', 'S', '2', 'X', 'G', 'S', 'R', '1'};

    enum RecordKind : uint8_t
    {
        kEnd = 0,
        kDraw,
        kLoadClut,
        kBeginTransfer,
        kUploadImage,
        kFrameEnd,
        kWriteVram,
        kClearFramebuffer,
        kTextureFlush,
        kConsumeLocalToHost,
        kReset,
    };

    struct LayoutHeader
    {
        uint32_t batch = sizeof(GSPrimitiveBatch);
        uint32_t tex0 = sizeof(GSTex0Reg);
        uint32_t texclut = sizeof(GSTexClutReg);
        uint32_t transfer = sizeof(GSTransferCommand);
        uint32_t context = sizeof(GSContext);
    };

    struct LoadClutRecord
    {
        GSTex0Reg tex0;
        GSTexClutReg texclut;
    };

    struct WriteVramRecord
    {
        uint32_t psm, base, bw, x, y, value;
    };

    struct ClearRecord
    {
        GSContext context;
        uint32_t rgba;
    };

    static_assert(std::is_trivially_copyable_v<GSPrimitiveBatch> && std::is_trivially_copyable_v<GSTransferCommand> &&
                      std::is_trivially_copyable_v<GSContext>,
                  "GS recordings store these structs raw");

    void writeBlob(std::FILE *file, const std::vector<uint8_t> &blob)
    {
        const uint32_t size = static_cast<uint32_t>(blob.size());
        std::fwrite(&size, sizeof(size), 1, file);
        if (size)
            std::fwrite(blob.data(), 1, size, file);
    }
}

GSRecordingBackend::GSRecordingBackend(std::unique_ptr<GSRasterBackend> inner, std::string path, double startSeconds, uint32_t frameCount)
    : m_inner(std::move(inner)), m_path(std::move(path)), m_created(std::chrono::steady_clock::now()),
      m_startSeconds(startSeconds), m_framesLeft(std::max<uint32_t>(frameCount, 1u))
{
}

GSRecordingBackend::~GSRecordingBackend()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file)
    {
        const uint8_t end = kEnd;
        std::fwrite(&end, 1, 1, m_file);
        std::fclose(m_file);
    }
}

std::unique_ptr<GSRasterBackend> GSRecordingBackend::WrapFromEnvironment(std::unique_ptr<GSRasterBackend> inner)
{
    const char *path = std::getenv("PS2X_GS_RECORD");
    if (!path || !*path)
        return inner;
    const char *at = std::getenv("PS2X_GS_RECORD_AT");
    const char *frames = std::getenv("PS2X_GS_RECORD_FRAMES");
    const double startSeconds = at ? std::atof(at) : 0.0;
    const uint32_t frameCount = frames ? static_cast<uint32_t>(std::atoi(frames)) : 30u;
    std::cerr << "[gs:record] " << path << " from " << startSeconds << " s, " << frameCount << " frames" << std::endl;
    return std::make_unique<GSRecordingBackend>(std::move(inner), path, startSeconds, frameCount);
}

void GSRecordingBackend::startRecording()
{
    std::vector<uint8_t> vram, state;
    m_inner->SnapshotVram(vram);
    if (auto *cpu = dynamic_cast<GSCpuBackend *>(m_inner.get()))
        cpu->SaveState(state);

    m_file = std::fopen(m_path.c_str(), "wb");
    if (!m_file)
    {
        std::cerr << "[gs:record] cannot open " << m_path << std::endl;
        m_done = true;
        return;
    }
    const LayoutHeader layout{};
    std::fwrite(kMagic, sizeof(kMagic), 1, m_file);
    std::fwrite(&layout, sizeof(layout), 1, m_file);
    writeBlob(m_file, vram);
    writeBlob(m_file, state);
    std::cerr << "[gs:record] started" << std::endl;
}

void GSRecordingBackend::writeRecord(uint8_t kind, const void *data, size_t size)
{
    if (!m_file)
        return;
    std::fwrite(&kind, 1, 1, m_file);
    if (size)
        std::fwrite(data, 1, size, m_file);
}

void GSRecordingBackend::Submit(const GSPrimitiveBatch &batch)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Same frame boundary as the CPU backend's completed-frame copy: MKSM composes
        // into FBP 0 last, so leaving it ends a frame.
        const uint32_t fbp = batch.state.context.frame.fbp;
        const bool frameEnd = m_lastFbp == 0u && fbp != 0u;
        m_lastFbp = fbp;
        if (frameEnd && !m_done)
        {
            if (m_file)
            {
                writeRecord(kFrameEnd, nullptr, 0u);
                if (--m_framesLeft == 0u)
                {
                    const uint8_t end = kEnd;
                    std::fwrite(&end, 1, 1, m_file);
                    std::fclose(m_file);
                    m_file = nullptr;
                    m_done = true;
                    std::cerr << "[gs:record] finished " << m_path << std::endl;
                }
            }
            else if (std::chrono::duration<double>(std::chrono::steady_clock::now() - m_created).count() >= m_startSeconds)
            {
                startRecording();
            }
        }
        writeRecord(kDraw, &batch, sizeof(batch));
    }
    m_inner->Submit(batch);
}

void GSRecordingBackend::LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const LoadClutRecord record{tex0, texclut};
        writeRecord(kLoadClut, &record, sizeof(record));
    }
    m_inner->LoadClut(tex0, texclut);
}

void GSRecordingBackend::BeginTransfer(const GSTransferCommand &command)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        writeRecord(kBeginTransfer, &command, sizeof(command));
    }
    m_inner->BeginTransfer(command);
}

void GSRecordingBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file && data && sizeBytes)
        {
            writeRecord(kUploadImage, &sizeBytes, sizeof(sizeBytes));
            std::fwrite(data, 1, sizeBytes, m_file);
        }
    }
    m_inner->UploadImage(data, sizeBytes);
}

void GSRecordingBackend::TextureFlush()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        writeRecord(kTextureFlush, nullptr, 0u);
    }
    m_inner->TextureFlush();
}

bool GSRecordingBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const ClearRecord record{context, rgba};
        writeRecord(kClearFramebuffer, &record, sizeof(record));
    }
    return m_inner->ClearFramebuffer(context, rgba);
}

uint32_t GSRecordingBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        writeRecord(kConsumeLocalToHost, &maxBytes, sizeof(maxBytes));
    }
    return m_inner->ConsumeLocalToHostBytes(dst, maxBytes);
}

void GSRecordingBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const WriteVramRecord record{psm, base, bw, x, y, value};
        writeRecord(kWriteVram, &record, sizeof(record));
    }
    m_inner->WriteVram(psm, base, bw, x, y, value);
}

void GSRecordingBackend::Reset()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        writeRecord(kReset, nullptr, 0u);
    }
    m_inner->Reset();
}

void GSRecordingBackend::Initialize(uint8_t *vram, uint32_t vramSize) { m_inner->Initialize(vram, vramSize); }
void GSRecordingBackend::Flush() { m_inner->Flush(); }
void GSRecordingBackend::Sync(GSSyncReason reason) { m_inner->Sync(reason); }
PresentationFrame GSRecordingBackend::Present(const GSPresentationRequest &request) { return m_inner->Present(request); }
uint32_t GSRecordingBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const { return m_inner->ReadVram(psm, base, bw, x, y); }
void GSRecordingBackend::SnapshotVram(std::vector<uint8_t> &out) const { m_inner->SnapshotVram(out); }
GSTransferSnapshot GSRecordingBackend::GetTransferSnapshot() const { return m_inner->GetTransferSnapshot(); }

bool GSRecording::Load(const std::string &path, std::string &error)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (!file)
    {
        error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> bytes;
    uint8_t buffer[1 << 16];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        bytes.insert(bytes.end(), buffer, buffer + got);
    std::fclose(file);

    size_t pos = 0u;
    auto take = [&](void *out, size_t size)
    {
        if (pos + size > bytes.size())
            return false;
        std::memcpy(out, bytes.data() + pos, size);
        pos += size;
        return true;
    };
    auto takeBlob = [&](std::vector<uint8_t> &out)
    {
        uint32_t size = 0u;
        if (!take(&size, sizeof(size)) || pos + size > bytes.size())
            return false;
        out.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.begin() + static_cast<std::ptrdiff_t>(pos + size));
        pos += size;
        return true;
    };

    char magic[8];
    LayoutHeader layout{}, expected{};
    if (!take(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(magic)) != 0)
    {
        error = "not a GS recording";
        return false;
    }
    if (!take(&layout, sizeof(layout)) || std::memcmp(&layout, &expected, sizeof(layout)) != 0)
    {
        error = "recorded with a different struct layout; record it again with this build";
        return false;
    }
    if (!takeBlob(m_vram) || !takeBlob(m_state))
    {
        error = "truncated header";
        return false;
    }
    m_records.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.end());

    // Validate the stream once and count frames, so Replay can trust it.
    m_frames = 0u;
    size_t at = 0u;
    while (at < m_records.size())
    {
        const uint8_t kind = m_records[at++];
        size_t size = 0u;
        switch (kind)
        {
        case kEnd: at = m_records.size(); continue;
        case kDraw: size = sizeof(GSPrimitiveBatch); break;
        case kLoadClut: size = sizeof(LoadClutRecord); break;
        case kBeginTransfer: size = sizeof(GSTransferCommand); break;
        case kUploadImage:
        {
            uint32_t n = 0u;
            if (at + sizeof(n) > m_records.size())
                break;
            std::memcpy(&n, m_records.data() + at, sizeof(n));
            size = sizeof(n) + n;
            break;
        }
        case kFrameEnd: ++m_frames; break;
        case kWriteVram: size = sizeof(WriteVramRecord); break;
        case kClearFramebuffer: size = sizeof(ClearRecord); break;
        case kTextureFlush: case kReset: break;
        case kConsumeLocalToHost: size = sizeof(uint32_t); break;
        default:
            error = "unknown record kind " + std::to_string(kind);
            return false;
        }
        if (at + size > m_records.size())
        {
            error = "truncated record";
            return false;
        }
        at += size;
    }
    return true;
}

void GSRecording::RestoreStart(GSRasterBackend &backend) const
{
    GSRasterBackend *target = &backend;
    if (auto *recorder = dynamic_cast<GSRecordingBackend *>(target))
        target = &recorder->inner();
    if (auto *cpu = dynamic_cast<GSCpuBackend *>(target))
        cpu->LoadState(m_vram, m_state);
}

void GSRecording::Replay(GSRasterBackend &backend, const std::function<void(uint32_t)> &onFrameEnd) const
{
    uint32_t frame = 0u;
    std::vector<uint8_t> scratch;
    size_t at = 0u;
    auto read = [&](void *out, size_t size)
    {
        std::memcpy(out, m_records.data() + at, size);
        at += size;
    };
    while (at < m_records.size())
    {
        const uint8_t kind = m_records[at++];
        switch (kind)
        {
        case kEnd:
            return;
        case kDraw:
        {
            GSPrimitiveBatch batch;
            read(&batch, sizeof(batch));
            backend.Submit(batch);
            break;
        }
        case kLoadClut:
        {
            LoadClutRecord record;
            read(&record, sizeof(record));
            backend.LoadClut(record.tex0, record.texclut);
            break;
        }
        case kBeginTransfer:
        {
            GSTransferCommand command;
            read(&command, sizeof(command));
            backend.BeginTransfer(command);
            break;
        }
        case kUploadImage:
        {
            uint32_t size = 0u;
            read(&size, sizeof(size));
            backend.UploadImage(m_records.data() + at, size);
            at += size;
            break;
        }
        case kFrameEnd:
            if (onFrameEnd)
                onFrameEnd(frame);
            ++frame;
            break;
        case kWriteVram:
        {
            WriteVramRecord record;
            read(&record, sizeof(record));
            backend.WriteVram(record.psm, record.base, record.bw, record.x, record.y, record.value);
            break;
        }
        case kClearFramebuffer:
        {
            ClearRecord record;
            read(&record, sizeof(record));
            backend.ClearFramebuffer(record.context, record.rgba);
            break;
        }
        case kTextureFlush:
            backend.TextureFlush();
            break;
        case kConsumeLocalToHost:
        {
            uint32_t maxBytes = 0u;
            read(&maxBytes, sizeof(maxBytes));
            scratch.resize(maxBytes);
            backend.ConsumeLocalToHostBytes(scratch.data(), maxBytes);
            break;
        }
        case kReset:
            backend.Reset();
            break;
        default:
            return; // Load already rejected unknown kinds.
        }
    }
}
