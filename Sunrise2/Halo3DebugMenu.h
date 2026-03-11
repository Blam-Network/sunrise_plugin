#pragma once

#include "stdafx.h"

// Halo 3 TU2 Debug Menu Hook Setup
// This file sets up hooks to integrate the ManagedDonkey debug menu with Halo 3 Xbox 360

// Initialize the debug menu system for Halo 3 TU2
void Halo3DebugMenu_Initialize();

// Cleanup and remove all debug menu hooks
void Halo3DebugMenu_Cleanup();

// Hook the debug menu rendering into Halo 3's rendering pipeline
void Halo3DebugMenu_SetupRenderHooks();

// Hook the debug menu update into Halo 3's game loop
void Halo3DebugMenu_SetupUpdateHooks();

// Setup function pointer hooks for game functions
void Halo3DebugMenu_SetupFunctionPointers();

// Halo 3 TU2 Function addresses (found via reverse engineering)

// Rendering functions
#define HALO3_RASTERIZER_QUAD_SCREENSPACE_ADDR 0x8219FBA8
#define HALO3_MAIN_RENDER_ADDR 0x82132d18

// Draw string functions  
#define HALO3_DRAW_STRING_GET_GLYPH_SCALING_ADDR 0x00000000 // Not found - will use stub
#define HALO3_C_DRAW_STRING_DRAW_ADDR 0x822a1960

// System functions
#define HALO3_SYSTEM_MILLISECONDS_ADDR 0x00000000 // Not found - will use GetTickCount
#define HALO3_INTERFACE_GET_DISPLAY_SETTINGS_ADDR 0x821995b8
#define HALO3_REAL_ARGB_COLOR_TO_PIXEL32_ADDR 0x8219b2f8

// Input functions
#define HALO3_INPUT_UPDATE_ADDR 0x82090320
#define HALO3_INPUT_XINPUT_UPDATE_GAMEPAD_ADDR 0x820903e0

// Scenario functions (for zone sets)
#define HALO3_GLOBAL_SCENARIO_GET_ADDR 0x00000000 // Not found - will stub for now
#define HALO3_SCENARIO_RESOURCES_SET_ACTIVE_ZONE_SET_ADDR 0x821fc270
#define HALO3_SCENARIO_GET_BSP_STRING_FROM_MASK_ADDR 0x00000000 // Not found - will stub for now

// HaloScript functions (for command execution)
#define HALO3_CONSOLE_PROCESS_COMMAND_ADDR 0x00000000 // Not found - may not exist on Xbox 360
#define HALO3_HS_EVALUATE_ADDR 0x820bad70
#define HALO3_HS_RUNTIME_UPDATE_ADDR 0x820e4240

// Game loop hooks - Hook input_update and main_render
#define HALO3_INPUT_UPDATE_HOOK_ADDR HALO3_INPUT_UPDATE_ADDR
#define HALO3_MAIN_RENDER_HOOK_ADDR HALO3_MAIN_RENDER_ADDR
