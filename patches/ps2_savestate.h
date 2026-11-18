#pragma once
// Debug save state, one slot (PLANO_CORRECOES.md, phase 1). MKSM_SAVESTATE=1 turns it on: F5 saves, F6 loads.
// Requests are served by EeScheduler::run() at the top of its loop, where no recompiled code is on the stack.
// Included only by .cpp files (never by ps2_runtime.h).
#include <cstdint>
#include <string>

class PS2Runtime;
class EeScheduler;

namespace ps2_savestate
{
    enum Request : int
    {
        None = 0,
        Save = 1,
        Load = 2,
    };

    bool enabled();                // MKSM_SAVESTATE=1
    void request(Request what);    // window thread (F5/F6) or the MKSM_SAVESTATE_AUTO timer
    bool pending();                // cheap: one atomic load
    // Scheduler thread, at the quiet point. Returns true when a state was loaded (the loop must restart).
    bool service(PS2Runtime &runtime, EeScheduler &scheduler);
    void pollAuto();               // window thread: MKSM_SAVESTATE_AUTO="save@25,load@35"
    // The on-screen notice for the last result, empty after 2 s.
    std::string notice();
}
