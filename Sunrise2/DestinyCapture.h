#pragma once

#ifndef DESTINYCAPTURE_H
#define DESTINYCAPTURE_H
#include "stdafx.h"

// Plaintext packet capture for Destiny retail 2.3.1 (default_ttk_231, xex
// timestamp 0x579810CC) on the release servers. Hold L3 + R3 + Y and tap LB to
// start, same again to stop. Frames go to packets\<date>_<time>.pcap next to
// the plugin. See docs/capture-format.md.

// Starts the capture worker for the 2.3.1 image that just loaded. Nothing is
// hooked in the game at load; hooks go in only when a capture starts on the
// pad combo, so a load with no Xbox Live sign-in touches nothing.
VOID SetupDestinyCapture();

// Called for every executable load, before it happens. Stops any running
// capture and restores its hooks in the outgoing image while it is still
// mapped. Returns TRUE when the feature was armed, so SetupHaloPatches runs
// again even for Destiny to Destiny.
BOOL DestinyCaptureNewExecutable();

// Called when the plugin is unloaded with the title still running. Stops any
// running capture and puts the hooked prologues back.
VOID DestinyCaptureShutdown();

#endif
