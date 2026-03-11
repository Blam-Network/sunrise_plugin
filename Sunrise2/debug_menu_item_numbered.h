#pragma once

#include "stdafx.h"
#include "debug_menu_item.h"
#include "debug_menu_value_hs_global_external.h"

class c_font_cache_base;
class c_debug_menu_item_numbered :
	public c_debug_menu_item
{
public:
	virtual ~c_debug_menu_item_numbered() {}
	virtual void render(c_font_cache_base* font_cache, const point2d& position);

	c_debug_menu_item_numbered(c_debug_menu* menu, const char* name, c_debug_menu* child);

protected:
	void render_number(c_font_cache_base* font_cache, const point2d& position);

	SHORT get_indent();
};
