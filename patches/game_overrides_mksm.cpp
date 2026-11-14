// =============================================================================
// Shaolin Monks (SLUS_210.87, USA v1.02) - game-specific runtime overrides
// =============================================================================
//
// WHY THIS FILE EXISTS
// --------------------
// The game's statically linked libcdvd (guest code) waits for the IOP `cdvdman`
// RPC server (SID 0x80000597) before doing any CD operation:
//
//     do { sceSifBindRpc(&client, 0x80000597, 0); } while (client.server == 0);
//
// The runtime's SifBindRpc() always returns 0, but only fills `client.server`
// when `canBindRpc(0x80000597)` is true. The HLE IOP subsystem only exposes
// MCSERV, LIBSD and DBCMAN, and no loaded IRX registers 0x80000597 (CDVDSTM.IRX
// is a *client* of cdvdman). Therefore `client.server` stays null forever and
// the guest spins at pc=0x385D80.
//
// Instead of implementing the full cdvdman RPC service (see PLANO.md, Etapa 4),
// this override replaces the guest libcdvd entry points with the runtime's
// already-tested HLE implementations from `Kernel/Stubs/CD.cpp`. The guest
// wrappers that remain (e.g. 0x385FE0) simply tail-call into the replaced
// addresses, so they keep working unchanged.
//
// WHAT THIS FILE DOES
// -------------------
// Registers one Game Override that binds a fixed table of guest addresses to
// HLE handlers *by name*. The names are resolved through PS2_STUB_LIST by
// `ps2_game_overrides::bindAddressHandler`, so a typo fails loudly at startup
// instead of silently doing nothing.
//
// The override matches only this exact game:
//   - ELF basename: SLUS_210.87
//   - ELF entry:    0x0011C070
//   - CRC32:        0 (0 disables the CRC check; see game_overrides.cpp)
//
// LINKING NOTE (important)
// ------------------------
// `ps2_runtime` is a STATIC library and the build does not use WHOLE_ARCHIVE.
// This translation unit only defines a static initializer, so the linker would
// drop it unless it is compiled directly into the `ps2EntryRunner` executable.
// That is why CMakeLists.txt adds this file with:
//
//     target_sources(ps2EntryRunner PRIVATE
//         "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/game_overrides_mksm.cpp")
//
// If you move this file into `ps2_runtime`, also add a symbol reference or a
// whole-archive link option, otherwise the override will never run.
//
// STAGE 2 (only if the log shows `sceCdRead unresolved LBN`)
// ----------------------------------------------------------
// Configure `PS2Runtime::getIoPaths().cdImage` to point at the ISO and raise
// `kCdPseudoLbnStart` in `Kernel/Stubs/Helpers/Support.h` (currently
// 0x00100000, below the ISO's 0x1A0B10 sectors). See PLANO.md, Etapa 2.
//
// REFERENCES
// ----------
// - PLANO.md, Etapa 1
// - ps2xRuntime/include/game_overrides.h
// - ps2xRuntime/src/lib/game_overrides.cpp (name resolution / apply logic)
// - ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp (HLE implementations)
// - ps2xIOP/src/iop_subsystem.cpp:188 (canBindRpc)
// - ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp:254 (SifBindRpc)
// =============================================================================

#include <cstdlib>
#include <cstring>
#include "game_overrides.h"
#include "ps2_log.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"

#include <cstdint>
#include <iostream>

void sub_001D32E0_0x1d32e0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

namespace
{
    // One guest function -> one runtime HLE handler.
    //
    // `guestFunction` is only used in logs; it documents which original
    // libcdvd symbol lives at `guestAddress` (taken from sm.toml entry_points).
    // `hleHandler` must be one of the names declared by PS2_STUB_LIST in
    // ps2xRuntime/include/ps2_call_list.h.
    struct GuestToHleBinding
    {
        uint32_t guestAddress;
        const char *guestFunction;
        const char *hleHandler;
    };

    // The public libcdvd 3.0 API used by this game. Binding the whole set in
    // one pass avoids "whack-a-mole": every one of these eventually performs
    // the same 0x80000597 bind and would otherwise hang.
    //
    // Addresses come from `sm.toml` (entry_points) and were verified against
    // the generated code in `output/sub_0038*.cpp` and
    // `output/sub_0046*.cpp`.
    constexpr GuestToHleBinding kLibCdvdBindings[] = {
        // sceCdLayerSearchFile(file, name, layer) -> sceCdSearchFile(file, name).
        // The layer argument ($a2) is ignored by the HLE, which is correct for
        // layer 0 (the guest wrapper at 0x385FE0 always passes 0).
        {0x00385CE0u, "sceCdLayerSearchFile", "sceCdSearchFile"},

        // Disk readiness / sync / mode.
        {0x00386158u, "sceCdNcmdDiskReady", "sceCdNcmdDiskReady"},
        {0x003861F0u, "sceCdSync", "sceCdSync"},
        {0x00386290u, "sceCdSyncS", "sceCdSyncS"},
        {0x00386458u, "sceCdInit", "sceCdInit"},
        {0x00386740u, "sceCdMmode", "sceCdMmode"},

        // Raw sector reads and error handling. These resolve pseudo-LBNs that
        // the HLE sceCdSearchFile registered in `g_cdFilesByKey`.
        {0x00467940u, "sceCdRead", "sceCdRead"},
        {0x00467B28u, "sceCdGetError", "sceCdGetError"},
        {0x00467BC0u, "sceCdBreak", "sceCdBreak"},

        // Clock query (used during boot/UI setup; harmless to serve from HLE).
        {0x00386818u, "sceCdReadClock", "sceCdReadClock"},
    };

    // Retail branch at 0x1D32FC: flag 0x513A04 == 1 skips the intro movies.
    void skipIntro(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t previous = 0u;
        const uint32_t enabled = 1u;
        std::memcpy(&previous, rdram + 0x513A04u, sizeof(previous));
        std::memcpy(rdram + 0x513A04u, &enabled, sizeof(enabled));
        sub_001D32E0_0x1d32e0(rdram, ctx, runtime);
        std::memcpy(rdram + 0x513A04u, &previous, sizeof(previous));
    }

    // MKSM_MOVIE=N (0..7) plays movie N of the table at 0x4D3BA0 first, in place of MIDWAY:
    // entries are 4-byte pointers (MIDWAY, OPENING, BLITZ, GAUNTLET, ESRB, END006, END011, FINAL).
    uint32_t g_movieIndex = 0u;
    void firstMovie(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        constexpr uint32_t kTable = 0x4D3BA0u;
        uint32_t first = 0u, chosen = 0u;
        std::memcpy(&first, rdram + kTable, 4);
        std::memcpy(&chosen, rdram + kTable + g_movieIndex * 4u, 4);
        std::memcpy(rdram + kTable, &chosen, 4);
        std::memcpy(rdram + kTable + g_movieIndex * 4u, &first, 4);
        sub_001D32E0_0x1d32e0(rdram, ctx, runtime);
        std::memcpy(rdram + kTable + g_movieIndex * 4u, &chosen, 4);
        std::memcpy(rdram + kTable, &first, 4);
    }

    // libmpeg sceMpegDelete (`jr ra; li v0, 1`) has no generated function, and the Sofdec
    // destroy path reaches it with a tail jump (0x44CC7C), so the game hung after every movie.
    // A jump keeps the target as pc: return to $ra like the generated stub wrappers do.
    void mpegDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = getRegU32(ctx, 31);
        ps2_stubs::sceMpegDelete(rdram, ctx, runtime);
    }

    void applyMksmOverrides(PS2Runtime &runtime)
    {
        runtime.replaceFunction(0x4688A0u, mpegDelete);
        if (const char *movie = std::getenv("MKSM_MOVIE"); movie != nullptr && std::atoi(movie) > 0 && std::atoi(movie) < 8)
        {
            g_movieIndex = static_cast<uint32_t>(std::atoi(movie));
            runtime.replaceFunction(0x1D32E0u, firstMovie);
            std::cerr << "[mksm] first movie: table entry " << g_movieIndex << std::endl;
        }
        else if (const char *skip = std::getenv("MKSM_SKIP_INTRO"); skip != nullptr && skip[0] == '1')
        {
            runtime.replaceFunction(0x1D32E0u, skipIntro);
            std::cerr << "[mksm] intro skip enabled" << std::endl;
        }

        RUNTIME_LOG("[mksm] applying libcdvd -> HLE overrides ("
                    << (sizeof(kLibCdvdBindings) / sizeof(kLibCdvdBindings[0]))
                    << " bindings)");

        for (const GuestToHleBinding &binding : kLibCdvdBindings)
        {
            if (ps2_game_overrides::bindAddressHandler(runtime,
                                                       binding.guestAddress,
                                                       binding.hleHandler))
            {
                RUNTIME_LOG("[mksm] bound " << binding.guestFunction
                                            << " @0x" << std::hex << binding.guestAddress
                                            << " -> HLE " << binding.hleHandler
                                            << std::dec);
                continue;
            }

            // bindAddressHandler already printed the reason (unknown handler or
            // address outside the generated dense function table). Keep a
            // game-tagged line so failures are easy to grep later.
            std::cerr << "[mksm] FAILED to bind " << binding.guestFunction
                      << " @0x" << std::hex << binding.guestAddress
                      << " -> HLE " << binding.hleHandler
                      << std::dec << " (see message above)" << std::endl;
        }
    }
} // namespace

PS2_REGISTER_GAME_OVERRIDE("mksm-cdvd",
                           "SLUS_210.87",
                           0x0011C070u,
                           0u,
                           applyMksmOverrides);
