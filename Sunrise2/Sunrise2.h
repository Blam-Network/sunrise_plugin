#pragma once

#ifndef SUNRISE2_H
#define SUNRISE2_H
#include "stdafx.h"

extern BOOL bIsDevkit;
extern BOOL bAllowRetailPlayers;
extern BOOL bIgnoreTrueskill;
extern BOOL bClearCacheOnLaunch;
extern char* BlamnetDomain;

VOID SpoofTitleVersion(PLDR_DATA_TABLE_ENTRY moduleTable);
VOID SetupHaloPatches();
VOID RegisterHaloServer();
BOOL IsHalo(DWORD titleId);

#endif