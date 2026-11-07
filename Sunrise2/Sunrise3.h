#pragma once

#ifndef SUNRISE3_H
#define SUNRISE3_H
#include "stdafx.h"

extern BOOL bIsDevkit;
extern BOOL bDisableXNotify;
extern BOOL bAllowRetailPlayers;
extern BOOL bIgnoreTrueskill;
extern BOOL bClearCacheOnLaunch;
extern BOOL bLogEventsToStdout;
extern BOOL bEnableDevkitSockpatch;
extern BOOL bPacketCapture;
// Per-group install switches for one-boot bisecting a crash.
extern BOOL bCaptureHooksBap;
extern BOOL bCaptureHooksXHttp;
extern BOOL bCaptureOverlay;
extern const char* SunriseVers;
extern char* BlamnetDomain;
extern DWORD LastTitleId;

VOID SpoofTitleVersion(PLDR_DATA_TABLE_ENTRY moduleTable);
VOID SetupHaloPatches();
VOID RegisterBungieServer();
BOOL IsHalo(DWORD titleId);

#endif