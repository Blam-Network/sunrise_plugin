#pragma once

#ifndef CAPTUREOVERLAY_H
#define CAPTUREOVERLAY_H
#include "stdafx.h"

// On-screen capture overlay for Destiny 2.3.1: "capturing packets (n sent,
// n received)" top left and the pre-alpha "PRE-ALPHA BUILD ..." watermark,
// drawn by the CPU straight into the front buffer.
//
// No threads: all drawing runs synchronously inside the D3DDevice_Swap hook on
// the render thread. No runtime allocation: every buffer is static. The Swap
// and watermark hooks go in when a capture starts and stay as passthrough
// afterwards, like the BAP/XHttp hooks. See docs/capture-format.md.

// Hook targets and their trampolines, referenced by the capture hook table.
DWORD SwapHook(DWORD device, DWORD frontBuffer, DWORD params);
VOID WatermarkHook();
VOID SwapInterruptHook(DWORD packed);
extern void* o_swap;
extern void* o_watermark;
extern void* o_swapInterrupt;

// Start drawing with no capture running, so the combo hint shows before the
// first capture. Called once the title is up and rendering.
VOID OverlayArm(BOOL swapHooked, BOOL markHooked, BOOL isrHooked);

// Capture start/stop, from StartCapture/StopCapture. swapHooked and markHooked
// say which of the two overlay hooks actually installed this run.
VOID OverlayOnStart(BOOL swapHooked, BOOL markHooked, BOOL isrHooked);
VOID OverlayOnStop();

// Stops drawing outright, for a title that is going away. The swap interrupt
// must not paint into front buffers that belong to the outgoing image.
VOID OverlayShutdown();

// New packet counts and the number of capture files saved this session, for
// the status line (called once per capture tick).
VOID OverlaySetStatus(DWORD sent, DWORD received, DWORD saved);

// TRUE once the overlay gave up for this session (it logged why once). Capture
// keeps running and the counters fall back to XNotify toasts.
BOOL OverlayFailed();

#endif
