#pragma once
// Recursive mutex for hot locks. On Windows (MinGW, posix thread model) std::recursive_mutex is winpthreads,
// ~5% of the GS threads in the profile; an SRWLOCK with an owner and a count is far cheaper. Elsewhere it is
// std::recursive_mutex. Lockable: works with std::lock_guard and std::unique_lock (try_to_lock too).
// The Windows calls live in gs_hw_backend.cpp: windows.h here would reach ps2_runtime.cpp, whose raylib names
// (CloseWindow, ShowCursor...) clash with it (ARQUITETURA.md, Windows).
#include <mutex>

#if defined(_WIN32)
#include <atomic>

class FastRecursiveMutex
{
public:
    void lock();
    bool try_lock();
    void unlock();

private:
    void *m_srw = nullptr;            // an SRWLOCK (one pointer; SRWLOCK_INIT is null)
    std::atomic<unsigned long> m_owner{0}; // thread id of the holder; only the holder sees its own id
    unsigned m_count = 0;
};
#else
using FastRecursiveMutex = std::recursive_mutex;
#endif
