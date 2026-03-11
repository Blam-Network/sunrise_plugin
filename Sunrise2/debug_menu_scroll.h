#pragma once

#include "stdafx.h"
#include "debug_menu.h"

class c_debug_menu_scroll :
	public c_debug_menu
{
public:
	virtual ~c_debug_menu_scroll() {}
	virtual void update();
	virtual void render(c_font_cache_base* font_cache, const point2d& point);
	virtual void open();

protected:
	virtual DWORD get_num_items_to_render();

public:
	c_debug_menu_scroll(c_debug_menu* parent, SHORT num_visible, const char* name);

protected:
	SHORT get_num_visible();
	SHORT get_first();

private:
	void set_num_visible(SHORT num_visible);
	void set_first(SHORT first);

protected:
	SHORT m_num_visible;
	SHORT m_first;
};
