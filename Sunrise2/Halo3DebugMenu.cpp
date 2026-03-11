#include "stdafx.h"
#include "Halo3DebugMenu.h"
#include "DebugMenu.h"
#include "DebugMenuItem.h"
#include "Detour.h"
#include "Utilities.h"

// Detours for render and update hooks
static Detour MainRenderDetour;
static Detour InputUpdateDetour;

// Hook for main_render - this is where we'll draw the debug menu
typedef void (*main_render_t)();
static void main_render_hook() {
	MainRenderDetour.GetOriginal<main_render_t>()();
	
	render_debug_debug_menu();
}

// Hook for input_update - this is where we'll process menu input
typedef void (*input_update_t)();
static void input_update_hook() {
	debug_menu_update();
	
	InputUpdateDetour.GetOriginal<input_update_t>()();
}

// Create the debug menu structure for Halo 3
void CreateHalo3DebugMenus()
{
	debug_menu_initialize_for_new_map();
	
	c_debug_menu* root = debug_menu_get_root();
	if (!root)
	{
		Sunrise_Dbg("Failed to get root menu!");
		return;
	}
	
	root->set_caption("Press Back+Start to toggle menu");
	
	// Create Player submenu
	c_debug_menu* player_menu = DEBUG_MENU_MALLOC(c_debug_menu, root, "Player");
	player_menu->set_caption("Player controls and cheats");
	
	c_debug_menu_item* player_item1 = DEBUG_MENU_MALLOC(c_debug_menu_item, player_menu, "Give All Weapons", nullptr, true);
	player_menu->add_item(player_item1);
	
	c_debug_menu_item* player_item2 = DEBUG_MENU_MALLOC(c_debug_menu_item, player_menu, "Full Ammo", nullptr, true);
	player_menu->add_item(player_item2);
	
	c_debug_menu_item* player_item3 = DEBUG_MENU_MALLOC(c_debug_menu_item, player_menu, "Teleport to Start", nullptr, true);
	player_menu->add_item(player_item3);
	
	c_debug_menu_item* player_item4 = DEBUG_MENU_MALLOC(c_debug_menu_item, player_menu, "Invincibility", nullptr, true);
	player_menu->add_item(player_item4);
	
	c_debug_menu_item* player_item5 = DEBUG_MENU_MALLOC(c_debug_menu_item, player_menu, "Infinite Ammo", nullptr, true);
	player_menu->add_item(player_item5);
	
	c_debug_menu_item* player_submenu_item = DEBUG_MENU_MALLOC(c_debug_menu_item, root, "Player", player_menu, true);
	root->add_item(player_submenu_item);
	
	// Create Spawning submenu
	c_debug_menu* spawn_menu = DEBUG_MENU_MALLOC(c_debug_menu, root, "Spawning");
	spawn_menu->set_caption("Spawn vehicles and objects");
	
	c_debug_menu_item* spawn_item1 = DEBUG_MENU_MALLOC(c_debug_menu_item, spawn_menu, "Spawn Warthog", nullptr, true);
	spawn_menu->add_item(spawn_item1);
	
	c_debug_menu_item* spawn_item2 = DEBUG_MENU_MALLOC(c_debug_menu_item, spawn_menu, "Spawn Mongoose", nullptr, true);
	spawn_menu->add_item(spawn_item2);
	
	c_debug_menu_item* spawn_item3 = DEBUG_MENU_MALLOC(c_debug_menu_item, spawn_menu, "Spawn Ghost", nullptr, true);
	spawn_menu->add_item(spawn_item3);
	
	c_debug_menu_item* spawn_item4 = DEBUG_MENU_MALLOC(c_debug_menu_item, spawn_menu, "Spawn Banshee", nullptr, true);
	spawn_menu->add_item(spawn_item4);
	
	c_debug_menu_item* spawn_submenu_item = DEBUG_MENU_MALLOC(c_debug_menu_item, root, "Spawning", spawn_menu, true);
	root->add_item(spawn_submenu_item);
	
	// Create Game submenu
	c_debug_menu* game_menu = DEBUG_MENU_MALLOC(c_debug_menu, root, "Game");
	game_menu->set_caption("Game settings and options");
	
	c_debug_menu_item* game_item1 = DEBUG_MENU_MALLOC(c_debug_menu_item, game_menu, "Show FPS", nullptr, true);
	game_menu->add_item(game_item1);
	
	c_debug_menu_item* game_item2 = DEBUG_MENU_MALLOC(c_debug_menu_item, game_menu, "Show Coordinates", nullptr, true);
	game_menu->add_item(game_item2);
	
	c_debug_menu_item* game_submenu_item = DEBUG_MENU_MALLOC(c_debug_menu_item, root, "Game", game_menu, true);
	root->add_item(game_submenu_item);
	
	// Create Graphics submenu
	c_debug_menu* graphics_menu = DEBUG_MENU_MALLOC(c_debug_menu, root, "Graphics");
	graphics_menu->set_caption("Visual and rendering options");
	
	c_debug_menu_item* graphics_item1 = DEBUG_MENU_MALLOC(c_debug_menu_item, graphics_menu, "Show Hitboxes", nullptr, true);
	graphics_menu->add_item(graphics_item1);
	
	c_debug_menu_item* graphics_item2 = DEBUG_MENU_MALLOC(c_debug_menu_item, graphics_menu, "Wireframe Mode", nullptr, true);
	graphics_menu->add_item(graphics_item2);
	
	c_debug_menu_item* graphics_submenu_item = DEBUG_MENU_MALLOC(c_debug_menu_item, root, "Graphics", graphics_menu, true);
	root->add_item(graphics_submenu_item);
	
	// Create AI submenu
	c_debug_menu* ai_menu = DEBUG_MENU_MALLOC(c_debug_menu, root, "AI");
	ai_menu->set_caption("AI debugging and controls");
	
	c_debug_menu_item* ai_item1 = DEBUG_MENU_MALLOC(c_debug_menu_item, ai_menu, "Freeze AI", nullptr, true);
	ai_menu->add_item(ai_item1);
	
	c_debug_menu_item* ai_item2 = DEBUG_MENU_MALLOC(c_debug_menu_item, ai_menu, "AI Debug Info", nullptr, true);
	ai_menu->add_item(ai_item2);
	
	c_debug_menu_item* ai_submenu_item = DEBUG_MENU_MALLOC(c_debug_menu_item, root, "AI", ai_menu, true);
	root->add_item(ai_submenu_item);
	
	Sunrise_Dbg("Halo 3 debug menus created successfully");
}

// Setup hooks for Halo 3 TU2
VOID SetupHalo3DebugMenuHooks_TU2(DWORD render_addr, DWORD update_addr) {
	Sunrise_Dbg("Setting up Halo 3 TU2 debug menu hooks...");
	
	// Create the debug menus
	CreateHalo3DebugMenus();
	
	// Hook main_render at 0x82132d18
	if (render_addr != 0) {
		MainRenderDetour = Detour(
			reinterpret_cast<void*>(render_addr),
			main_render_hook
		);
		if (MainRenderDetour.Install()) {
			Sunrise_Dbg("main_render hook installed at 0x%08X", render_addr);
		} else {
			Sunrise_Dbg("Failed to install main_render hook!");
		}
	}
	
	// Hook input_update at 0x82090320
	if (update_addr != 0) {
		InputUpdateDetour = Detour(
			reinterpret_cast<void*>(update_addr),
			input_update_hook
		);
		if (InputUpdateDetour.Install()) {
			Sunrise_Dbg("input_update hook installed at 0x%08X", update_addr);
		} else {
			Sunrise_Dbg("Failed to install input_update hook!");
		}
	}
	
	Sunrise_Dbg("Halo 3 TU2 debug menu hooks setup complete");
}
