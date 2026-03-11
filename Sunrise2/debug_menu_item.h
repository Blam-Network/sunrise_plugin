#pragma once

#include "stdafx.h"

// Forward declarations
class c_font_cache_base;
struct point2d;
struct real_argb_color;

class c_debug_menu;
class c_debug_menu_item
{
public:
	virtual ~c_debug_menu_item();
	virtual void update();
	virtual void render(c_font_cache_base* font_cache, const point2d& position);
	virtual void notify_selected();
	virtual void open();
	virtual void notify_left();
	virtual void notify_right();
	virtual const real_argb_color* get_enabled_color();
	virtual const real_argb_color* get_background_color();

	c_debug_menu_item(c_debug_menu* menu, const char* name, c_debug_menu* child, BOOL active);
	
	const char* get_name();
	void set_name(const char* name);

	c_debug_menu* get_menu();
	c_debug_menu* get_child();

	BOOL get_active();
	void set_active(BOOL active);

	BOOL get_data();
	void set_data(DWORD data);

	SHORT get_index();
	BOOL get_is_selection();

protected:
	char* m_name;
	c_debug_menu* m_menu_ref;
	c_debug_menu* m_child_ref;
	BOOL m_active;
	DWORD m_data;
};
