// Debug save state, one slot (PLANO_CORRECOES.md, phase 1). See ps2_savestate.h.
// Every class it reads declares `friend struct SaveStateAccess`, so all of the (de)serialisation lives here.
#include "ps2_savestate.h"

#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "ps2x/iop/savestate_io.h"
#include "ps2_vu1_thread.h"
#include "gs/gs_async.h"
#include "gs/gs_hw_backend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_recording_backend.h"
#include "ps2_iop_host.h"
#include "ps2x/iop/iop_subsystem.h"
#include "Kernel/Syscalls/Helpers/State.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

extern std::atomic<bool> g_moviePlaying; // Kernel/Stubs/MPEG.cpp
namespace ps2_stubs // the stubs keep their state in anonymous namespaces: each file saves its own
{
    bool mcCommandPending(); // Kernel/Stubs/MemoryCard.cpp
    bool mcFilesOpen();
    void saveSifState(ps2x::savestate::Writer &), loadSifState(ps2x::savestate::Reader &);
    void saveMcState(ps2x::savestate::Writer &), loadMcState(ps2x::savestate::Reader &);
    void saveCdState(ps2x::savestate::Writer &), loadCdState(ps2x::savestate::Reader &);
    void saveDmaState(ps2x::savestate::Writer &), loadDmaState(ps2x::savestate::Reader &);
    void saveAudioStubState(ps2x::savestate::Writer &), loadAudioStubState(ps2x::savestate::Reader &);
    void savePadState(ps2x::savestate::Writer &), loadPadState(ps2x::savestate::Reader &);
}
namespace mksm_rpg
{
    void saveState(ps2x::savestate::Writer &), loadState(ps2x::savestate::Reader &);
}

using ps2x::savestate::Reader;
using ps2x::savestate::Writer;

namespace
{
    constexpr char kMagic[8] = {'M', 'K', 'S', 'M', 'S', 'S', '0', '1'};
    constexpr uint32_t kFormat = 1;
    constexpr int kScratchpadBytes = 16 * 1024;
    constexpr const char *kSections[] = {"EE.RAM  ", "EE.RUNT ", "EE.SCHD ", "EE.MEM  ", "EE.VU   ", "GS      ", "IOP     ", "STUBS   "};

    std::atomic<int> g_request{ps2_savestate::None};
    std::mutex g_noticeMutex;
    std::string g_notice;
    std::chrono::steady_clock::time_point g_noticeUntil{};
    std::chrono::steady_clock::time_point g_saveSince{};
    std::string g_firstBlocker;

    uint64_t fnv1a(const uint8_t *p, size_t n, uint64_t h = 1469598103934665603ull)
    {
        for (size_t i = 0; i < n; ++i)
            h = (h ^ p[i]) * 1099511628211ull;
        return h;
    }

    void say(const std::string &text)
    {
        std::fprintf(stderr, "[savestate] %s\n", text.c_str());
        std::lock_guard<std::mutex> lock(g_noticeMutex);
        g_notice = text;
        g_noticeUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }

    // FNV-1a of the running executable: a file from another build is refused (raw structs change layout).
    uint64_t executableId()
    {
        static const uint64_t id = [] {
            std::string path;
#if defined(_WIN32)
            char buf[MAX_PATH]{};
            GetModuleFileNameA(nullptr, buf, MAX_PATH);
            path = buf;
#else
            path = "/proc/self/exe";
#endif
            std::ifstream in(path, std::ios::binary);
            std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return fnv1a(reinterpret_cast<const uint8_t *>(data.data()), data.size());
        }();
        return id;
    }

    std::filesystem::path statePath()
    {
        if (const char *ini = std::getenv("MKSM_INI"); ini && *ini)
            return std::filesystem::path(ini).parent_path() / "savestate.bin";
        return std::filesystem::current_path() / "savestate.bin";
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Sections. Each pair writes and reads the same fields in the same order.
struct SaveStateAccess
{
    // EE.RAM: RDRAM and scratchpad.
    static void saveRam(Writer &w, PS2Runtime &rt)
    {
        w.bytes(rt.m_memory.getRDRAM(), PS2_RAM_SIZE);
        w.bytes(rt.m_memory.getScratchpad(), kScratchpadBytes);
    }
    static void loadRam(Reader &r, PS2Runtime &rt)
    {
        r.bytes(rt.m_memory.getRDRAM(), PS2_RAM_SIZE);
        r.bytes(rt.m_memory.getScratchpad(), kScratchpadBytes);
    }

    // EE.RUNT: the runtime's own guest-visible state.
    static void saveRuntime(Writer &w, PS2Runtime &rt)
    {
        w.pod(rt.m_cpuContext);
        w.u64(rt.m_eeExitHandlers.size());
        for (const auto &[thread, list] : rt.m_eeExitHandlers)
            w.pod(thread), w.vec(list);
        w.map(rt.m_eeSyscallOverrides);
        w.u64(rt.m_eeSyscallMirrorAddresses.size());
        for (uint32_t a : rt.m_eeSyscallMirrorAddresses)
            w.pod(a);
        {
            std::lock_guard<std::mutex> lock(rt.m_guestHeapMutex);
            w.vec(rt.m_guestHeapBlocks);
            w.pod(rt.m_guestHeapBase), w.pod(rt.m_guestHeapEnd), w.pod(rt.m_guestHeapLimit);
            w.pod(rt.m_guestHeapSuggestedBase), w.pod(rt.m_guestHeapConfigured);
        }
        {
            std::lock_guard<std::mutex> lock(rt.m_asyncCallbackStackMutex);
            w.pod(rt.m_asyncCallbackStackFloor), w.pod(rt.m_asyncCallbackStackTop);
        }
        w.u64(rt.m_loadedModules.size());
        for (const auto &m : rt.m_loadedModules)
            w.str(m.name), w.pod(m.baseAddress), w.pod(m.size), w.pod(m.active);
    }
    static void loadRuntime(Reader &r, PS2Runtime &rt)
    {
        r.pod(rt.m_cpuContext);
        rt.m_eeExitHandlers.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            int thread = 0;
            r.pod(thread);
            r.vec(rt.m_eeExitHandlers[thread]);
        }
        r.map(rt.m_eeSyscallOverrides);
        rt.m_eeSyscallMirrorAddresses.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            uint32_t a = 0;
            r.pod(a);
            rt.m_eeSyscallMirrorAddresses.insert(a);
        }
        {
            std::lock_guard<std::mutex> lock(rt.m_guestHeapMutex);
            r.vec(rt.m_guestHeapBlocks);
            r.pod(rt.m_guestHeapBase), r.pod(rt.m_guestHeapEnd), r.pod(rt.m_guestHeapLimit);
            r.pod(rt.m_guestHeapSuggestedBase), r.pod(rt.m_guestHeapConfigured);
        }
        {
            std::lock_guard<std::mutex> lock(rt.m_asyncCallbackStackMutex);
            r.pod(rt.m_asyncCallbackStackFloor), r.pod(rt.m_asyncCallbackStackTop);
        }
        rt.m_loadedModules.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            PS2Runtime::LoadedModule m{};
            r.str(m.name), r.pod(m.baseAddress), r.pod(m.size), r.pod(m.active);
            rt.m_loadedModules.push_back(std::move(m));
        }
    }

    // EE.SCHD: threads, semaphores, event flags, alarms, handlers, timers and the event queue.
    template <typename T> static void saveDeque(Writer &w, const std::deque<T> &d)
    {
        w.u64(d.size());
        for (const T &v : d)
            w.pod(v);
    }
    template <typename T> static void loadDeque(Reader &r, std::deque<T> &d)
    {
        d.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            T v{};
            r.pod(v);
            d.push_back(v);
        }
    }
    static void saveThread(Writer &w, const GuestThread &t)
    {
        w.pod(t.id), w.pod(t.context), w.pod(t.entry), w.pod(t.stack), w.pod(t.stackSize), w.pod(t.gp);
        w.pod(t.attr), w.pod(t.option), w.pod(t.arg), w.pod(t.initialPriority), w.pod(t.currentPriority);
        w.pod(t.status), w.pod(t.suspendCount), w.pod(t.wakeupCount), w.pod(t.ownsStack), w.pod(t.tlsBase);
        w.pod(t.wait.reason), w.variant(t.wait.payload);
    }
    static void loadThread(Reader &r, GuestThread &t)
    {
        r.pod(t.id), r.pod(t.context), r.pod(t.entry), r.pod(t.stack), r.pod(t.stackSize), r.pod(t.gp);
        r.pod(t.attr), r.pod(t.option), r.pod(t.arg), r.pod(t.initialPriority), r.pod(t.currentPriority);
        r.pod(t.status), r.pod(t.suspendCount), r.pod(t.wakeupCount), r.pod(t.ownsStack), r.pod(t.tlsBase);
        r.pod(t.wait.reason), r.variant(t.wait.payload);
        t.wait.completion = {}, t.resumeCompletion = {}, t.invocations.clear();
    }

    static bool schedulerBlocker(const EeScheduler &s, std::string &why)
    {
        if (!s.m_pendingInvocations.empty())
            return why = "chamada do sistema pendente no EE", true;
        if (s.m_insideInterrupt)
            return why = "interrupção do EE em andamento", true;
        for (const auto &[id, t] : s.m_threads)
        {
            if (!t.invocations.empty() || t.resumeCompletion || t.wait.completion)
                return why = "thread " + std::to_string(id) + " no meio de uma chamada", true;
            switch (t.wait.reason)
            {
            case EeWaitReason::None: case EeWaitReason::Sleep: case EeWaitReason::Semaphore:
            case EeWaitReason::EventFlag: case EeWaitReason::VSync: break;
            default: return why = "thread " + std::to_string(id) + " esperando algo externo", true;
            }
        }
        return false;
    }

    static void saveScheduler(Writer &w, EeScheduler &s)
    {
        for (const auto &q : s.m_readyQueues)
            saveDeque(w, q);
        w.u64(s.m_threads.size());
        for (const auto &[id, t] : s.m_threads)
            w.pod(id), saveThread(w, t);
        w.u64(s.m_semaphores.size());
        for (const auto &[id, sem] : s.m_semaphores)
            w.pod(id), w.pod(sem.id), w.pod(sem.count), w.pod(sem.maxCount), w.pod(sem.initCount), w.pod(sem.attr),
                w.pod(sem.option), saveDeque(w, sem.waiters);
        w.u64(s.m_eventFlags.size());
        for (const auto &[id, ef] : s.m_eventFlags)
            w.pod(id), w.pod(ef.id), w.pod(ef.attr), w.pod(ef.option), w.pod(ef.initBits), w.pod(ef.bits),
                saveDeque(w, ef.waiters);
        w.map(s.m_alarms), w.map(s.m_intcHandlers), w.map(s.m_dmacHandlers);
        w.pod(s.m_nextThreadId), w.pod(s.m_nextInvocationThreadId), w.pod(s.m_nextSemaphoreId), w.pod(s.m_nextEventFlagId);
        w.pod(s.m_nextAlarmId), w.pod(s.m_nextIntcHandlerId), w.pod(s.m_nextDmacHandlerId);
        w.pod(s.m_intcHeadOrder), w.pod(s.m_intcTailOrder), w.pod(s.m_dmacHeadOrder), w.pod(s.m_dmacTailOrder);
        w.pod(s.m_enabledIntcMask), w.pod(s.m_enabledDmacMask), w.pod(s.m_currentThreadId);
        w.pod(s.m_rescheduleRequested), w.pod(s.m_timeSliceExpired), w.pod(s.m_insideInterrupt);
        w.pod(s.m_pendingEeTimerInterrupts), w.pod(s.m_eeTimerPendingCycles), w.pod(s.m_eeTimerBatchLimit);
        w.pod(s.m_eeCycle), w.pod(s.m_sliceEndCycle);
        {
            std::lock_guard<std::mutex> lock(s.m_eventMutex);
            saveDeque(w, s.m_events);
            const auto now = std::chrono::steady_clock::now();
            w.u64(s.m_deadlines.size());
            for (const auto &d : s.m_deadlines) // host deadlines as "ns still to go"
            {
                const int64_t left = d.hostDeadline == std::chrono::steady_clock::time_point{} ? INT64_MIN
                                     : std::chrono::duration_cast<std::chrono::nanoseconds>(d.hostDeadline - now).count();
                w.pod(d.deadlineCycle), w.pod(left), w.pod(d.event), w.pod(d.sequence);
            }
        }
        w.pod(s.m_eventSequence), w.pod(s.m_invocationSequence), w.pod(s.m_vsyncTick), w.pod(s.m_vsyncFlagAddress);
        w.pod(s.m_vsyncTickAddress), w.pod(s.m_gsVSyncCallback), w.pod(s.m_gsVSyncCallbackGp), w.pod(s.m_gsVSyncCallbackSp);
        w.map(s.m_invocationStackTops);
        w.pod(s.m_snapshotSequence);
    }
    static void loadScheduler(Reader &r, EeScheduler &s)
    {
        for (auto &q : s.m_readyQueues)
            loadDeque(r, q);
        s.m_threads.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            int id = 0;
            r.pod(id);
            loadThread(r, s.m_threads[id]);
        }
        s.m_semaphores.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            int id = 0;
            r.pod(id);
            EeSemaphore &sem = s.m_semaphores[id];
            r.pod(sem.id), r.pod(sem.count), r.pod(sem.maxCount), r.pod(sem.initCount), r.pod(sem.attr), r.pod(sem.option);
            loadDeque(r, sem.waiters);
        }
        s.m_eventFlags.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            int id = 0;
            r.pod(id);
            EeEventFlag &ef = s.m_eventFlags[id];
            r.pod(ef.id), r.pod(ef.attr), r.pod(ef.option), r.pod(ef.initBits), r.pod(ef.bits);
            loadDeque(r, ef.waiters);
        }
        r.map(s.m_alarms), r.map(s.m_intcHandlers), r.map(s.m_dmacHandlers);
        r.pod(s.m_nextThreadId), r.pod(s.m_nextInvocationThreadId), r.pod(s.m_nextSemaphoreId), r.pod(s.m_nextEventFlagId);
        r.pod(s.m_nextAlarmId), r.pod(s.m_nextIntcHandlerId), r.pod(s.m_nextDmacHandlerId);
        r.pod(s.m_intcHeadOrder), r.pod(s.m_intcTailOrder), r.pod(s.m_dmacHeadOrder), r.pod(s.m_dmacTailOrder);
        r.pod(s.m_enabledIntcMask), r.pod(s.m_enabledDmacMask), r.pod(s.m_currentThreadId);
        r.pod(s.m_rescheduleRequested), r.pod(s.m_timeSliceExpired), r.pod(s.m_insideInterrupt);
        r.pod(s.m_pendingEeTimerInterrupts), r.pod(s.m_eeTimerPendingCycles), r.pod(s.m_eeTimerBatchLimit);
        r.pod(s.m_eeCycle), r.pod(s.m_sliceEndCycle);
        {
            std::lock_guard<std::mutex> lock(s.m_eventMutex);
            loadDeque(r, s.m_events);
            const auto now = std::chrono::steady_clock::now();
            s.m_deadlines.clear();
            for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
            {
                EeScheduler::ScheduledEvent d{};
                int64_t left = 0;
                r.pod(d.deadlineCycle), r.pod(left), r.pod(d.event), r.pod(d.sequence);
                if (left != INT64_MIN)
                    d.hostDeadline = now + std::chrono::nanoseconds(left);
                s.m_deadlines.push_back(d);
            }
        }
        r.pod(s.m_eventSequence), r.pod(s.m_invocationSequence), r.pod(s.m_vsyncTick), r.pod(s.m_vsyncFlagAddress);
        r.pod(s.m_vsyncTickAddress), r.pod(s.m_gsVSyncCallback), r.pod(s.m_gsVSyncCallbackGp), r.pod(s.m_gsVSyncCallbackSp);
        r.map(s.m_invocationStackTops);
        r.pod(s.m_snapshotSequence);
    }
    static void afterSchedulerLoad(EeScheduler &s)
    {
        s.m_checkpointPending.store(true, std::memory_order_release);
        s.updateNextDeadline();
        s.renewTimeSlice();
        s.publishSnapshot();
    }

    // EE.MEM: VU memory, I/O registers, DMA/VIF/GIF queues and timers (PS2Memory).
    static void saveTransfers(Writer &w, const std::vector<PS2Memory::PendingTransfer> &v)
    {
        w.u64(v.size());
        for (const auto &t : v)
            w.pod(t.fromScratchpad), w.pod(t.srcAddr), w.pod(t.qwc), w.vec(t.chainData);
    }
    static void loadTransfers(Reader &r, std::vector<PS2Memory::PendingTransfer> &v)
    {
        v.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            PS2Memory::PendingTransfer t{};
            r.pod(t.fromScratchpad), r.pod(t.srcAddr), r.pod(t.qwc), r.vec(t.chainData);
            v.push_back(std::move(t));
        }
    }
    static void saveMem(Writer &w, PS2Memory &m)
    {
        w.bytes(m.m_vu0Code, PS2_VU0_CODE_SIZE), w.bytes(m.m_vu0Data, PS2_VU0_DATA_SIZE);
        w.bytes(m.m_vu1Code, PS2_VU1_CODE_SIZE), w.bytes(m.m_vu1Data, PS2_VU1_DATA_SIZE);
        w.bytes(m.iop_ram, 2u * 1024u * 1024u);
        w.vec(m.m_ioRegisters.m_flat), w.vec(m.m_ioRegisters.m_present), w.map(m.m_ioRegisters.m_other);
        w.pod(m.gs_regs), w.pod(m.vif0_regs), w.pod(m.vif1_regs), w.pod(m.dma_regs);
        w.vec(m.m_tlbEntries), w.pod(m.m_seenGifCopy);
        w.pod(m.m_path3Masked), w.pod(m.m_vif1PendingPath2ImageQwc), w.pod(m.m_vif1PendingPath2DirectHl);
        w.u64(m.m_path3MaskedFifo.size());
        for (const auto &packet : m.m_path3MaskedFifo)
            w.vec(packet);
        saveTransfers(w, m.m_pendingGifTransfers), saveTransfers(w, m.m_pendingVif0Transfers), saveTransfers(w, m.m_pendingVif1Transfers);
        {
            std::lock_guard<std::mutex> lock(m.m_completedDmacMutex);
            w.vec(m.m_completedDmacCauses);
        }
        w.u64(m.m_codeRegions.size());
        for (const auto &c : m.m_codeRegions)
        {
            w.pod(c.start), w.pod(c.end), w.u64(c.modified.size());
            for (bool b : c.modified)
                w.pod(static_cast<uint8_t>(b));
        }
        w.pod(m.m_eeTimers);
    }
    static void loadMem(Reader &r, PS2Memory &m)
    {
        r.bytes(m.m_vu0Code, PS2_VU0_CODE_SIZE), r.bytes(m.m_vu0Data, PS2_VU0_DATA_SIZE);
        r.bytes(m.m_vu1Code, PS2_VU1_CODE_SIZE), r.bytes(m.m_vu1Data, PS2_VU1_DATA_SIZE);
        r.bytes(m.iop_ram, 2u * 1024u * 1024u);
        r.vec(m.m_ioRegisters.m_flat), r.vec(m.m_ioRegisters.m_present), r.map(m.m_ioRegisters.m_other);
        r.pod(m.gs_regs), r.pod(m.vif0_regs), r.pod(m.vif1_regs), r.pod(m.dma_regs);
        r.vec(m.m_tlbEntries), r.pod(m.m_seenGifCopy);
        r.pod(m.m_path3Masked), r.pod(m.m_vif1PendingPath2ImageQwc), r.pod(m.m_vif1PendingPath2DirectHl);
        m.m_path3MaskedFifo.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
            r.vec(m.m_path3MaskedFifo.emplace_back());
        loadTransfers(r, m.m_pendingGifTransfers), loadTransfers(r, m.m_pendingVif0Transfers), loadTransfers(r, m.m_pendingVif1Transfers);
        {
            std::lock_guard<std::mutex> lock(m.m_completedDmacMutex);
            r.vec(m.m_completedDmacCauses);
        }
        m.m_codeRegions.clear();
        for (uint64_t i = 0, n = r.u64(); i < n && r.ok(); ++i)
        {
            PS2Memory::CodeRegion c{};
            r.pod(c.start), r.pod(c.end);
            const uint64_t bits = r.u64();
            if (bits > r.remaining())
                return r.fail();
            c.modified.resize(bits);
            for (uint64_t b = 0; b < bits; ++b)
            {
                uint8_t v = 0;
                r.pod(v);
                c.modified[b] = v != 0;
            }
            m.m_codeRegions.push_back(std::move(c));
        }
        r.pod(m.m_eeTimers);
        m.markVU0CodeModified(), m.markVU1CodeModified(); // the recompiled VU code and decode caches start over
    }

    // EE.VU: VU0 and VU1 (register file, pipelines, the XGKICK in flight). Decode caches and the JIT start over.
    template <typename F> static void eachVuField(VU1Interpreter &v, F &&f)
    {
        f(v.m_state), f(v.m_flagPipeline), f(v.m_fdiv), f(v.m_efu), f(v.m_storePipeline), f(v.m_xgkick);
        f(v.m_vfReady), f(v.m_viReady), f(v.m_accReady), f(v.m_cycle), f(v.m_efuResourceReady), f(v.m_nextPipelineCycle);
        f(v.m_workingClip), f(v.m_currentUpperInstruction), f(v.m_viBranchBackupValue), f(v.m_viBranchBackupReg);
        f(v.m_viBranchBackupValid), f(v.m_stopRequested), f(v.m_pendingHaltD), f(v.m_pendingHaltT);
    }
    static void saveVu(Writer &w, VU1Interpreter &v)
    {
        eachVuField(v, [&](const auto &x) { w.pod(x); });
    }
    static void loadVu(Reader &r, VU1Interpreter &v)
    {
        eachVuField(v, [&](auto &x) { r.pod(x); });
        v.m_decodedCodeCacheValid = false, v.m_cachedVuCode = nullptr, v.m_cachedMemory = nullptr, v.m_cachedCodeGeneration = 0;
    }

    // GS: frontend registers and vertex queue, then the CPU backend's VRAM (4 MB) and state.
    template <typename F> static void eachGsField(GS &g, F &&f)
    {
        f(g.m_ctx), f(g.m_prim), f(g.m_primRegister), f(g.m_prmodeRegister), f(g.m_curR), f(g.m_curG), f(g.m_curB);
        f(g.m_curA), f(g.m_curQ), f(g.m_curS), f(g.m_curT), f(g.m_curU), f(g.m_curV), f(g.m_curFog), f(g.m_fogR);
        f(g.m_fogG), f(g.m_fogB), f(g.m_prmodecont), f(g.m_pabe), f(g.m_scanmsk), f(g.m_dimx), f(g.m_dthe);
        f(g.m_colclamp), f(g.m_texa), f(g.m_texclut), f(g.m_bitbltbuf), f(g.m_trxpos), f(g.m_trxreg), f(g.m_trxdir);
        f(g.m_vtxQueue), f(g.m_vtxCount), f(g.m_vtxIndex), f(g.m_preferredDisplaySourceFrame);
        f(g.m_preferredDisplayDestFbp), f(g.m_hasPreferredDisplaySource);
    }
    // The renderer chain: GSRecordingBackend? -> GSHwBackend? -> GSCpuBackend (which owns VRAM and CLUT).
    static GSCpuBackend *cpuBackend(GS &g, GSHwBackend **hw)
    {
        GSRasterBackend *b = g.m_backend.get();
        *hw = nullptr;
        for (int i = 0; i < 3 && b; ++i)
        {
            if (auto *cpu = dynamic_cast<GSCpuBackend *>(b))
                return cpu;
            if (auto *rec = dynamic_cast<GSRecordingBackend *>(b))
                b = &rec->inner();
            else if (auto *h = dynamic_cast<GSHwBackend *>(b))
                *hw = h, b = &h->inner();
            else
                break;
        }
        return nullptr;
    }
    static void saveGs(Writer &w, GS &g)
    {
        std::lock_guard<std::recursive_mutex> lock(g.m_stateMutex);
        eachGsField(g, [&](const auto &x) { w.pod(x); });
        GSHwBackend *hw = nullptr;
        GSCpuBackend *cpu = cpuBackend(g, &hw);
        std::vector<uint8_t> vram, state;
        if (hw)
            hw->Flush(), hw->Sync(GSSyncReason::Finish); // the GPU's newer pixels into the mirror
        if (cpu)
            cpu->SnapshotVram(vram), cpu->SaveState(state);
        w.vec(vram), w.vec(state);
    }
    static void loadGs(Reader &r, GS &g)
    {
        std::lock_guard<std::recursive_mutex> lock(g.m_stateMutex);
        eachGsField(g, [&](auto &x) { r.pod(x); });
        std::vector<uint8_t> vram, state;
        r.vec(vram), r.vec(state);
        GSHwBackend *hw = nullptr;
        if (GSCpuBackend *cpu = cpuBackend(g, &hw); cpu && r.ok())
            cpu->LoadState(vram, state);
        if (hw)
            hw->InvalidateAll();
    }

    // IOP: everything the IOP emulator holds (ps2xIOP, iop_emulator.cpp). The host adapter's open files stay.
    static bool iopBlocker(PS2Runtime &rt, std::string &why)
    {
        if (rt.m_iopHost && rt.m_iopHost->m_activeToken != 0u)
            return why = "IOP no meio de uma chamada do host", true;
        return rt.m_iopSubsystem && rt.m_iopSubsystem->saveStateBlocker(why);
    }
    static void saveIop(Writer &w, PS2Runtime &rt)
    {
        if (rt.m_iopSubsystem)
            rt.m_iopSubsystem->saveState(w);
    }
    static void loadIop(Reader &r, PS2Runtime &rt)
    {
        if (rt.m_iopSubsystem)
            rt.m_iopSubsystem->loadState(r);
    }

    // STUBS: EE-side RPC, SIF, memory card, CD stream, DMA, audio stubs, pads and the RPG mode.
    static void saveStubs(Writer &w)
    {
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            w.map(g_rpc_servers), w.map(g_rpc_clients), w.pod(g_rpc_initialized), w.pod(g_rpc_next_id);
            w.pod(g_rpc_packet_index), w.pod(g_rpc_server_index), w.pod(g_rpc_active_queue);
        }
        ps2_stubs::saveSifState(w), ps2_stubs::saveMcState(w), ps2_stubs::saveCdState(w), ps2_stubs::saveDmaState(w);
        ps2_stubs::saveAudioStubState(w), ps2_stubs::savePadState(w), mksm_rpg::saveState(w);
    }
    static void loadStubs(Reader &r)
    {
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            r.map(g_rpc_servers), r.map(g_rpc_clients), r.pod(g_rpc_initialized), r.pod(g_rpc_next_id);
            r.pod(g_rpc_packet_index), r.pod(g_rpc_server_index), r.pod(g_rpc_active_queue);
        }
        ps2_stubs::loadSifState(r), ps2_stubs::loadMcState(r), ps2_stubs::loadCdState(r), ps2_stubs::loadDmaState(r);
        ps2_stubs::loadAudioStubState(r), ps2_stubs::loadPadState(r), mksm_rpg::loadState(r);
    }

    // The order of sections in the file; loading checks they are all present before applying any.
    static void writeAll(Writer &w, PS2Runtime &rt, EeScheduler &s)
    {
        w.beginSection("EE.RAM  "), saveRam(w, rt), w.endSection();
        w.beginSection("EE.RUNT "), saveRuntime(w, rt), w.endSection();
        w.beginSection("EE.SCHD "), saveScheduler(w, s), w.endSection();
        w.beginSection("EE.MEM  "), saveMem(w, rt.m_memory), w.endSection();
        w.beginSection("EE.VU   "), saveVu(w, rt.m_vu0), saveVu(w, rt.m_vu1), w.endSection();
        w.beginSection("GS      "), saveGs(w, rt.m_gs), w.endSection();
        w.beginSection("IOP     "), saveIop(w, rt), w.endSection();
        w.beginSection("STUBS   "), saveStubs(w), w.endSection();
    }
    static void readAll(Reader &r, PS2Runtime &rt, EeScheduler &s)
    {
        r.section("EE.RAM  "), loadRam(r, rt);
        r.section("EE.RUNT "), loadRuntime(r, rt);
        r.section("EE.SCHD "), loadScheduler(r, s);
        r.section("EE.MEM  "), loadMem(r, rt.m_memory);
        r.section("EE.VU   "), loadVu(r, rt.m_vu0), loadVu(r, rt.m_vu1);
        r.section("GS      "), loadGs(r, rt.m_gs);
        r.section("IOP     "), loadIop(r, rt);
        r.section("STUBS   "), loadStubs(r);
        afterSchedulerLoad(s);
    }
    static bool validate(const std::vector<uint8_t> &file, std::string &why)
    {
        Reader r(file.data(), file.size());
        char magic[8]{};
        r.bytes(magic, 8);
        uint32_t format = 0;
        r.pod(format);
        const uint64_t id = r.u64();
        if (!r.ok() || std::memcmp(magic, kMagic, 8) != 0 || format != kFormat)
            return why = "arquivo de outro formato", false;
        // MKSM_SAVESTATE_IGNORE_EXE=1: development, when the rebuild did not touch any saved struct.
        if (id != executableId() && !std::getenv("MKSM_SAVESTATE_IGNORE_EXE"))
            return why = "feito por outro executável", false;
        for (const char *name : kSections)
        {
            char got[8]{};
            r.bytes(got, 8);
            const uint64_t n = r.u64();
            if (!r.ok() || std::memcmp(got, name, 8) != 0 || n > r.remaining())
                return why = std::string("falta a seção ") + name, false;
            std::vector<uint8_t> skip(n);
            r.bytes(skip.data(), n);
        }
        return r.remaining() == 0 ? true : (why = "sobra no fim do arquivo", false);
    }
    static uint64_t ramHash(PS2Runtime &rt) { return fnv1a(rt.m_memory.getRDRAM(), PS2_RAM_SIZE); }
};

namespace
{
    // Other threads hold or are about to change state: let them finish first (1.0.9).
    void stopWorkers()
    {
        ps2_vu1_thread::sync();
        gsAsyncDrain();
    }

    bool trySave(PS2Runtime &rt, EeScheduler &s, std::string &why)
    {
        if (g_moviePlaying.load(std::memory_order_relaxed))
            return why = "durante um filme", false;
        if (ps2_stubs::mcCommandPending() || ps2_stubs::mcFilesOpen())
            return why = "com o memory card ocupado", false;
        if (SaveStateAccess::schedulerBlocker(s, why) || SaveStateAccess::iopBlocker(rt, why))
            return false;
        stopWorkers();
        Writer w;
        w.bytes(kMagic, 8), w.pod(kFormat), w.u64(executableId());
        SaveStateAccess::writeAll(w, rt, s);
        const auto path = statePath(), tmp = std::filesystem::path(path.string() + ".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(w.data.data()), static_cast<std::streamsize>(w.data.size()));
            if (!out)
                return why = "não consegui gravar " + tmp.string(), false;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec)
            return why = "não consegui renomear para " + path.string(), false;
        std::ostringstream text;
        text << "salvo " << path.string() << " (" << w.data.size() / 1024 << " KB) ram=" << std::hex << SaveStateAccess::ramHash(rt);
        std::fprintf(stderr, "[savestate] %s\n", text.str().c_str());
        return true;
    }

    bool tryLoad(PS2Runtime &rt, EeScheduler &s, std::string &why)
    {
        if (g_moviePlaying.load(std::memory_order_relaxed))
            return why = "durante um filme", false;
        if (ps2_stubs::mcCommandPending())
            return why = "com o memory card ocupado", false;
        const auto path = statePath();
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return why = "não há " + path.string(), false;
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!SaveStateAccess::validate(file, why))
            return false;
        stopWorkers();
        Reader r(file.data(), file.size());
        char magic[8];
        uint32_t format;
        r.bytes(magic, 8), r.pod(format), r.u64();
        SaveStateAccess::readAll(r, rt, s);
        if (!r.ok())
            return why = "arquivo corrompido no meio da leitura (estado misturado: feche o jogo)", false;
        std::ostringstream text;
        text << "carregado " << path.string() << " ram=" << std::hex << SaveStateAccess::ramHash(rt);
        std::fprintf(stderr, "[savestate] %s\n", text.str().c_str());
        return true;
    }
}

namespace ps2_savestate
{
    bool enabled()
    {
        static const bool on = [] {
            const char *v = std::getenv("MKSM_SAVESTATE");
            return v && v[0] == '1';
        }();
        return on;
    }

    void request(Request what)
    {
        if (enabled())
            g_request.store(what, std::memory_order_release), g_saveSince = std::chrono::steady_clock::now(), g_firstBlocker.clear();
    }

    bool pending() { return g_request.load(std::memory_order_relaxed) != None; }

    bool service(PS2Runtime &runtime, EeScheduler &scheduler)
    {
        const int what = g_request.load(std::memory_order_acquire);
        if (what == None)
            return false;
        std::string why;
        if (what == Save)
        {
            if (trySave(runtime, scheduler, why))
                return g_request.store(None), say("Save state salvo"), false;
            if (g_firstBlocker.empty())
                g_firstBlocker = why;
            // Busy now: try again on the next turn of the loop, for up to 5 s.
            if (std::chrono::steady_clock::now() - g_saveSince < std::chrono::seconds(5))
                return false;
            return g_request.store(None), say("Save state recusado: " + g_firstBlocker), false;
        }
        g_request.store(None);
        if (tryLoad(runtime, scheduler, why))
            return say("Save state carregado"), true;
        say("Save state recusado: " + why);
        return false;
    }

    void pollAuto()
    {
        static const std::string script = [] {
            const char *v = std::getenv("MKSM_SAVESTATE_AUTO");
            return std::string(v ? v : "");
        }();
        if (script.empty() || !enabled())
            return;
        static const auto start = std::chrono::steady_clock::now();
        static std::vector<bool> done;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::stringstream list(script);
        std::string item;
        for (size_t i = 0; std::getline(list, item, ','); ++i)
        {
            if (done.size() <= i)
                done.resize(i + 1, false);
            const size_t at = item.find('@');
            if (done[i] || at == std::string::npos || now < std::atof(item.c_str() + at + 1))
                continue;
            done[i] = true;
            request(item.compare(0, at, "save") == 0 ? Save : Load);
        }
    }

    std::string notice()
    {
        std::lock_guard<std::mutex> lock(g_noticeMutex);
        return std::chrono::steady_clock::now() < g_noticeUntil ? g_notice : std::string();
    }
}
