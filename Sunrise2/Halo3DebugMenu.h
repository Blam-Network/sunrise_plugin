#pragma once
#ifndef HALO3_DEBUG_MENU_H
#define HALO3_DEBUG_MENU_H

#include "stdafx.h"

// Setup hooks for Halo 3 debug menu
VOID SetupHalo3DebugMenuHooks_TU2(DWORD render_addr, DWORD update_addr);

// Create Halo 3 specific debug menus
VOID CreateHalo3DebugMenus();

#endif // HALO3_DEBUG_MENU_H
