#pragma once

#ifndef COREHOOKS_H
#define COREHOOKS_H
#include "stdafx.h"

VOID RegisterActiveServer(in_addr address, const char description[XTITLE_SERVER_MAX_SERVER_INFO_LEN]);
VOID RegisterActiveServerDomain(char* domain, const char description[XTITLE_SERVER_MAX_SERVER_INFO_LEN]);
VOID SetupLSPHooks();
VOID SetupSpoofHooks();
VOID SetupLoadHooks(PLDR_DATA_TABLE_ENTRY moduleHandle);
VOID SetupXUserReadStatsHook(DWORD Address);
VOID SetTitleId(DWORD title_id);
VOID SetupXMountUtilityDriveExHook(DWORD functionAddress);
VOID ApplyPrivHook();

#endif