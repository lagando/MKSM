#pragma once
// GS frontend on its own thread (ARQUITETURA.md). Runtime-only header: ps2_runtime.h must
// not include it, or every generated file rebuilds.
#include <cstdint>

class GS;

// Hands a GIF packet to the GS frontend thread (copies it). PS2X_GS_ASYNC=0 decodes it
// here, on the caller's thread, as before.
void gsAsyncSubmit(GS &gs, const uint8_t *data, uint32_t sizeBytes);

// Waits until every submitted packet is decoded. Call it before the EE can see GS state:
// privileged register reads and writes, direct GS calls, local-to-host transfers.
void gsAsyncDrain();

// CSR reads (the frame regulator polls FIELD) wait only for what was submitted before the last
// vblank: the GS may draw one frame while the EE builds the next, as on the console.
// PS2X_GS_CSR_LAG=0 makes them wait for everything (gsAsyncDrain), as before.
bool gsAsyncCsrLag();
void gsAsyncNoteVblank();
void gsAsyncWaitLastVblank();
