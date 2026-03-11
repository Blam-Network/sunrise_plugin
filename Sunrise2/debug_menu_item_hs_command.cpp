#include "stdafx.h"
#include "debug_menu_item_hs_command.h"
#include "debug_menu_main.h"

#include <string.h>

// Xbox 360: Custom assert macro
#define ASSERT(x) if(!(x)) { __debugbreak(); }

// Xbox 360: Stub function pointers for HaloScript command execution
// These will be hooked to the actual game functions
typedef void (*console_process_command_t)(const char* command, BOOL unknown);
typedef void (*hs_compile_and_evaluate_t)(DWORD event_type, const char* source, const char* script, BOOL unknown);

console_process_command_t console_process_command = NULL;
hs_compile_and_evaluate_t hs_compile_and_evaluate = NULL;

void c_debug_menu_item_hs_command::notify_selected()
{
	if (m_command)
	{
		// Xbox 360: Call game functions via function pointers if they're hooked
		if (console_process_command)
			console_process_command(m_command, TRUE);
		
		if (hs_compile_and_evaluate)
			hs_compile_and_evaluate(0, "debug_menu", m_command, TRUE);
	}
}

const real_argb_color* c_debug_menu_item_hs_command::get_enabled_color()
{
	return debug_real_argb_tv_orange;
}

c_debug_menu_item_hs_command::c_debug_menu_item_hs_command(c_debug_menu* menu, const char* name, const char* command) :
	c_debug_menu_item_numbered(menu, name, NULL)
{
	ASSERT(name != NULL && command != NULL && menu != NULL);

	DWORD command_length = (DWORD)strlen(command) + 1;
	ASSERT(command_length > 0);

	m_command = (char*)debug_menu_malloc(command_length);
	if (m_command)
		strncpy_s(m_command, command_length, command, command_length - 1);
}
