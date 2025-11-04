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
extern char* BlamnetDomain;

VOID SpoofTitleVersion(PLDR_DATA_TABLE_ENTRY moduleTable);
VOID SetupHaloPatches();
VOID RegisterHaloServer();
BOOL IsHalo(DWORD titleId);

#endif