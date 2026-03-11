#include "stdafx.h"
#include "debug_menu_zone_sets.h"
#include "debug_menu_item_numbered.h"
#include "debug_menu_main.h"

// Xbox 360: Custom assert macro
#define ASSERT(x) if(!(x)) { __debugbreak(); }
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))

// Xbox 360: Stub structures for scenario zone sets
struct s_scenario_zone_set
{
	char name[32];
	DWORD flags;
};

struct s_scenario
{
	// Simplified - in reality this would be much larger
	// For now, just stub out zone set access
	s_scenario_zone_set* zone_sets;
	DWORD zone_set_count;
};

// Xbox 360: Function pointers for scenario access (to be set by hooks)
typedef s_scenario* (*global_scenario_get_t)();
typedef void (*main_switch_zone_set_t)(DWORD zone_set_index);
typedef void (*scenario_get_structure_bsp_string_from_mask_t)(DWORD mask, char* buffer, DWORD buffer_size);

global_scenario_get_t global_scenario_get = NULL;
main_switch_zone_set_t main_switch_zone_set = NULL;
scenario_get_structure_bsp_string_from_mask_t scenario_get_structure_bsp_string_from_mask = NULL;

void c_debug_menu_zone_sets::notify_selected(SHORT selected_value)
{
	if (!global_scenario_get || !main_switch_zone_set)
		return;
	
	s_scenario* scenario = global_scenario_get();
	if (!scenario)
		return;
	
	if (VALID_INDEX(get_selection(), scenario->zone_set_count))
	{
		main_switch_zone_set(selected_value);
	}
	else
	{
		OutputDebugStringA("ERROR: this should be a valid zone set index WTF???\n");
	}
}

void c_debug_menu_zone_sets::open()
{
	c_debug_menu_scroll::open();
	update_caption();
}

void c_debug_menu_zone_sets::notify_up()
{
	c_debug_menu::notify_up();
	update_caption();
}

void c_debug_menu_zone_sets::notify_down()
{
	c_debug_menu::notify_down();
	update_caption();
}

c_debug_menu_zone_sets::c_debug_menu_zone_sets(c_debug_menu* parent, SHORT num_visible, const char* name_ptr) :
	c_debug_menu_scroll(parent, num_visible, name_ptr)
{
	strncpy_s(m_caption, sizeof(m_caption), "", _TRUNCATE);

	// Xbox 360: Add zone set items if scenario is available
	if (global_scenario_get)
	{
		s_scenario* scenario = global_scenario_get();
		if (scenario && scenario->zone_sets)
		{
			for (DWORD i = 0; i < scenario->zone_set_count; i++)
			{
				add_item(DEBUG_MENU_MALLOC(c_debug_menu_item_numbered, this, scenario->zone_sets[i].name, NULL));
			}
		}
	}
}

void c_debug_menu_zone_sets::update_caption()
{
	char caption[1024];
	memset(caption, 0, sizeof(caption));

	if (!global_scenario_get || !scenario_get_structure_bsp_string_from_mask)
		return;
	
	s_scenario* scenario = global_scenario_get();
	if (!scenario || !scenario->zone_sets)
		return;
	
	if (VALID_INDEX(get_selection(), scenario->zone_set_count))
	{
		scenario_get_structure_bsp_string_from_mask(scenario->zone_sets[get_selection()].flags, caption, sizeof(caption));
	}

	set_caption(caption);
}
