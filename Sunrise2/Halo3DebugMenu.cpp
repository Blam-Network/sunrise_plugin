#include "stdafx.h"
#include "Halo3DebugMenu.h"
#include "debug_menu_main.h"
#include "debug_menu.h"
#include "Detour.h"
#include "Utilities.h"

// Xbox 360: Detour hooks
Detour* g_DebugMenuInputUpdateDetour = NULL;
Detour* g_DebugMenuMainRenderDetour = NULL;

// Original function pointers
typedef void (*input_update_t)();
typedef void (*main_render_t)();

input_update_t original_input_update = NULL;
main_render_t original_main_render = NULL;

// Hook implementations
void Hooked_InputUpdate()
{
	// Call original input update
	if (original_input_update)
		original_input_update();
	
	// Update debug menu (handles input and state)
	debug_menu_update();
}

void Hooked_MainRender()
{
	// Call original render
	if (original_main_render)
		original_main_render();
	
	// Render debug menu overlays
	render_debug_debug_menu_game();
	render_debug_debug_menu();
}

void Halo3DebugMenu_SetupFunctionPointers()
{
	// Xbox 360: Set up function pointers for game functions using addresses found via IDA
	
	// Rendering functions
	rasterizer_quad_screenspace = (rasterizer_quad_screenspace_t)HALO3_RASTERIZER_QUAD_SCREENSPACE_ADDR;
	
	// System functions - use actual game addresses
	if (HALO3_INTERFACE_GET_DISPLAY_SETTINGS_ADDR != 0)
	{
		interface_get_current_display_settings = (interface_get_current_display_settings_t)HALO3_INTERFACE_GET_DISPLAY_SETTINGS_ADDR;
		Sunrise_Dbg("Halo3DebugMenu: interface_get_current_display_settings = 0x%08X", HALO3_INTERFACE_GET_DISPLAY_SETTINGS_ADDR);
	}
	
	if (HALO3_REAL_ARGB_COLOR_TO_PIXEL32_ADDR != 0)
	{
		real_argb_color_to_pixel32 = (real_argb_color_to_pixel32_t)HALO3_REAL_ARGB_COLOR_TO_PIXEL32_ADDR;
		Sunrise_Dbg("Halo3DebugMenu: real_argb_color_to_pixel32 = 0x%08X", HALO3_REAL_ARGB_COLOR_TO_PIXEL32_ADDR);
	}
	
	// Stub implementations for missing functions
	if (!draw_string_get_glyph_scaling_for_display_settings)
	{
		// Default scaling - function may be inlined in retail build
		draw_string_get_glyph_scaling_for_display_settings = []() -> FLOAT { return 1.0f; };
		Sunrise_Dbg("Halo3DebugMenu: draw_string_get_glyph_scaling_for_display_settings = STUB (1.0f)");
	}
	
	if (!system_milliseconds)
	{
		// Use Xbox kernel function GetTickCount
		system_milliseconds = []() -> DWORD { return GetTickCount(); };
		Sunrise_Dbg("Halo3DebugMenu: system_milliseconds = STUB (GetTickCount)");
	}
	
	Sunrise_Dbg("Halo3DebugMenu: Function pointers configured successfully");
}

void Halo3DebugMenu_SetupUpdateHooks()
{
	// Hook into Halo 3's input_update function for debug menu updates
	if (HALO3_INPUT_UPDATE_HOOK_ADDR != 0)
	{
		g_DebugMenuInputUpdateDetour = new Detour();
		g_DebugMenuInputUpdateDetour->SetupDetour((DWORD)HALO3_INPUT_UPDATE_HOOK_ADDR, (DWORD)Hooked_InputUpdate);
		g_DebugMenuInputUpdateDetour->InstallDetour();
		original_input_update = (input_update_t)g_DebugMenuInputUpdateDetour->GetOriginalAddress();
		
		Sunrise_Dbg("Halo3DebugMenu: Input update hook installed at 0x%08X", HALO3_INPUT_UPDATE_HOOK_ADDR);
	}
	else
	{
		Sunrise_Dbg("Halo3DebugMenu: WARNING - Input update hook address not configured!");
	}
}

void Halo3DebugMenu_SetupRenderHooks()
{
	// Hook into Halo 3's main_render function for debug menu rendering
	if (HALO3_MAIN_RENDER_HOOK_ADDR != 0)
	{
		g_DebugMenuMainRenderDetour = new Detour();
		g_DebugMenuMainRenderDetour->SetupDetour((DWORD)HALO3_MAIN_RENDER_HOOK_ADDR, (DWORD)Hooked_MainRender);
		g_DebugMenuMainRenderDetour->InstallDetour();
		original_main_render = (main_render_t)g_DebugMenuMainRenderDetour->GetOriginalAddress();
		
		Sunrise_Dbg("Halo3DebugMenu: Main render hook installed at 0x%08X", HALO3_MAIN_RENDER_HOOK_ADDR);
	}
	else
	{
		Sunrise_Dbg("Halo3DebugMenu: WARNING - Main render hook address not configured!");
	}
}

void Halo3DebugMenu_Initialize()
{
	Sunrise_Dbg("Halo3DebugMenu: Initializing ManagedDonkey debug menu for Halo 3 TU2...");
	
	// Setup function pointers first
	Halo3DebugMenu_SetupFunctionPointers();
	
	// Initialize debug menu system
	debug_menu_initialize();
	debug_menu_initialize_for_new_map();
	
	// Setup hooks
	Halo3DebugMenu_SetupUpdateHooks();
	Halo3DebugMenu_SetupRenderHooks();
	
	Sunrise_Dbg("Halo3DebugMenu: Initialization complete!");
	Sunrise_Dbg("Halo3DebugMenu: Press Back+Start to toggle debug menu");
}

// Cleanup function (call when plugin unloads)
void Halo3DebugMenu_Cleanup()
{
	Sunrise_Dbg("Halo3DebugMenu: Cleaning up...");
	
	// Remove hooks
	if (g_DebugMenuInputUpdateDetour)
	{
		g_DebugMenuInputUpdateDetour->UninstallDetour();
		delete g_DebugMenuInputUpdateDetour;
		g_DebugMenuInputUpdateDetour = NULL;
		Sunrise_Dbg("Halo3DebugMenu: Input update hook removed");
	}
	
	if (g_DebugMenuMainRenderDetour)
	{
		g_DebugMenuMainRenderDetour->UninstallDetour();
		delete g_DebugMenuMainRenderDetour;
		g_DebugMenuMainRenderDetour = NULL;
		Sunrise_Dbg("Halo3DebugMenu: Main render hook removed");
	}
	
	// Dispose debug menu
	debug_menu_dispose_from_old_map();
	debug_menu_dispose();
	
	Sunrise_Dbg("Halo3DebugMenu: Cleanup complete");
}
