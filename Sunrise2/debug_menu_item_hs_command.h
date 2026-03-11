#pragma once

#include "stdafx.h"
#include "debug_menu_item_numbered.h"

class c_debug_menu_item_hs_command :
	public c_debug_menu_item_numbered
{
public:
	virtual void notify_selected();
	virtual const real_argb_color* get_enabled_color();

	c_debug_menu_item_hs_command(c_debug_menu* menu, const char* name, const char* command);

protected:
	char* m_command;
};
