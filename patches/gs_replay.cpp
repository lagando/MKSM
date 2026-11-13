// ps2x_gs_replay: draws a GS recording (PS2X_GS_RECORD) without the game.
//   ps2x_gs_replay file.gsr [--repeat N] [--dump-vram out.bin] [--compare ref.bin] [--ppm prefix]
// Prints the time per frame and a hash of the final VRAM, so a renderer change can be
// timed and checked against the CPU backend in seconds.
#include "../../ps2xRuntime/src/lib/gs/gs_hw_backend.h"
#include <cstring>
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_recording_backend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    uint64_t fnv1a(const std::vector<uint8_t> &bytes)
    {
        uint64_t hash = 1469598103934665603ull;
        for (const uint8_t byte : bytes)
            hash = (hash ^ byte) * 1099511628211ull;
        return hash;
    }

    bool readFile(const std::string &path, std::vector<uint8_t> &out)
    {
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
            return false;
        std::fseek(file, 0, SEEK_END);
        out.resize(static_cast<size_t>(std::ftell(file)));
        std::fseek(file, 0, SEEK_SET);
        const bool ok = std::fread(out.data(), 1, out.size(), file) == out.size();
        std::fclose(file);
        return ok;
    }

    bool writeFile(const std::string &path, const void *data, size_t size)
    {
        std::FILE *file = std::fopen(path.c_str(), "wb");
        if (!file)
            return false;
        const bool ok = std::fwrite(data, 1, size, file) == size;
        std::fclose(file);
        return ok;
    }

    // MKSM draws 640x224 fields into frame pages 0x00 and 0x46 (CT32, width 640).
    void writeFramePpm(GSCpuBackend &backend, uint32_t page, const std::string &path)
    {
        constexpr uint32_t kWidth = 640u, kHeight = 224u;
        std::vector<uint8_t> rgb;
        rgb.reserve(kWidth * kHeight * 3u);
        for (uint32_t y = 0u; y < kHeight; ++y)
            for (uint32_t x = 0u; x < kWidth; ++x)
            {
                const uint32_t pixel = backend.ReadVram(GS_PSM_CT32, page * 32u, kWidth / 64u, x, y);
                rgb.push_back(static_cast<uint8_t>(pixel));
                rgb.push_back(static_cast<uint8_t>(pixel >> 8));
                rgb.push_back(static_cast<uint8_t>(pixel >> 16));
            }
        const std::string header = "P6\n" + std::to_string(kWidth) + " " + std::to_string(kHeight) + "\n255\n";
        std::vector<uint8_t> out(header.begin(), header.end());
        out.insert(out.end(), rgb.begin(), rgb.end());
        writeFile(path, out.data(), out.size());
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s file.gsr [--repeat N] [--dump-vram out] [--compare ref] [--ppm prefix] [--sync 0|1]\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    int repeat = 1;
    bool syncEachFrame = true; // 0: no wait per field, so the GPU overlaps the next one as in the game
    std::string dumpPath, comparePath, ppmPrefix;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const std::string option = argv[i];
        if (option == "--repeat")
            repeat = std::max(1, std::atoi(argv[i + 1]));
        else if (option == "--dump-vram")
            dumpPath = argv[i + 1];
        else if (option == "--compare")
            comparePath = argv[i + 1];
        else if (option == "--sync")
            syncEachFrame = std::atoi(argv[i + 1]) != 0;
        else if (option == "--ppm")
            ppmPrefix = argv[i + 1];
        else
        {
            std::fprintf(stderr, "unknown option %s\n", option.c_str());
            return 2;
        }
    }

    GSRecording recording;
    std::string error;
    if (!recording.Load(path, error))
    {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 1;
    }

    std::vector<uint8_t> vram(recording.startVram().size());
    GSCpuBackend cpuBackend;
    // PS2X_GS_HW=count: also report what the batching renderer (track F) would issue.
    GSHwBackend counter(cpuBackend);
    const char *hwMode = std::getenv("PS2X_GS_HW");
    const bool countBatches = hwMode && std::strcmp(hwMode, "count") == 0;
    GSRasterBackend &backend = countBatches ? static_cast<GSRasterBackend &>(counter) : cpuBackend;
    backend.Initialize(vram.data(), static_cast<uint32_t>(vram.size()));

    std::vector<double> frameMs;
    double totalMs = 0.0;
    for (int pass = 0; pass < repeat; ++pass)
    {
        recording.RestoreStart(backend);
        auto frameStart = std::chrono::steady_clock::now();
        const auto start = frameStart;
        recording.Replay(backend, [&](uint32_t)
                         {
            if (syncEachFrame)
                backend.Sync(GSSyncReason::DebugReadback);
            const auto now = std::chrono::steady_clock::now();
            frameMs.push_back(std::chrono::duration<double, std::milli>(now - frameStart).count());
            frameStart = now; });
        backend.Sync(GSSyncReason::DebugReadback);
        totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    if (countBatches)
        counter.report();
    std::vector<uint8_t> finalVram;
    backend.SnapshotVram(finalVram);
    std::sort(frameMs.begin(), frameMs.end());
    const double median = frameMs.empty() ? 0.0 : frameMs[frameMs.size() / 2];
    std::printf("%s: %u frames x %d, %.1f ms total, frame median %.2f ms (%.1f fps), worst %.2f ms\n",
                path.c_str(), recording.frameCount(), repeat, totalMs, median, median > 0.0 ? 1000.0 / median : 0.0,
                frameMs.empty() ? 0.0 : frameMs.back());
    std::printf("vram hash %016llx\n", static_cast<unsigned long long>(fnv1a(finalVram)));

    if (!dumpPath.empty() && !writeFile(dumpPath, finalVram.data(), finalVram.size()))
        std::fprintf(stderr, "cannot write %s\n", dumpPath.c_str());
    if (!ppmPrefix.empty())
    {
        writeFramePpm(cpuBackend, 0x00u, ppmPrefix + "_fb00.ppm");
        writeFramePpm(cpuBackend, 0x46u, ppmPrefix + "_fb46.ppm");
    }

    int status = 0;
    if (!comparePath.empty())
    {
        std::vector<uint8_t> reference;
        if (!readFile(comparePath, reference) || reference.size() != finalVram.size())
        {
            std::fprintf(stderr, "cannot read %s or size differs\n", comparePath.c_str());
            return 1;
        }
        std::map<size_t, size_t> pages; // 8 KB page -> differing bytes
        size_t differing = 0u;
        for (size_t i = 0; i < reference.size(); ++i)
            if (reference[i] != finalVram[i])
            {
                ++differing;
                ++pages[i / 8192u];
            }
        std::printf("compare %s: %zu bytes differ in %zu pages\n", comparePath.c_str(), differing, pages.size());
        size_t shown = 0u;
        for (const auto &[page, count] : pages)
        {
            if (shown++ == 16u)
                break;
            std::printf("  page 0x%zx: %zu bytes\n", page, count);
        }
        status = differing ? 3 : 0;
    }
    return status;
}
