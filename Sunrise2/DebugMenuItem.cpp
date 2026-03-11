#include "stdafx.h"
#include "DebugMenuItem.h"
#include "DebugMenu.h"
#include "Utilities.h"
#include <string.h>

extern const real_argb_color* const debug_real_argb_grey;
extern const real_argb_color* const debug_real_argb_tv_white;
extern const real_argb_color* const debug_real_argb_tv_green;
extern const real_argb_color* const global_real_argb_black;
extern long debug_menu_get_time();

c_debug_menu_item::c_debug_menu_item(c_debug_menu* menu, const char* name, c_debug_menu* child, bool active) :
	m_name(nullptr),
	m_menu_ref(menu),
	m_child_ref(child),
	m_active(active),
	m_data(-1)
{
	set_name(name ? name : "Item???");
}

c_debug_menu_item::~c_debug_menu_item()
{
	if (m_child_ref)
	{
		m_child_ref->~c_debug_menu();
	}
	m_child_ref = nullptr;
}

bool c_debug_menu_item::get_active()
{
	return m_active;
}

const real_argb_color* c_debug_menu_item::get_background_color()
{
	if (!get_is_selection())
	{
		return debug_real_argb_tv_white;
	}

	long time = debug_menu_get_time();
	float v4 = (time % 80) / 79.0f;
	float v5 = v4 + v4;
	if (v4 >= 0.5f)
	{
		v5 = 1.0f - ((v4 - 0.5f) + (v4 - 0.5f));
	}

	static real_argb_color background_color = {};
	background_color.alpha = 1.0f;
	for (int i = 0; i < 3; i++)
	{
		float v6 = (v5 * 0.4f) + 0.05f;
		background_color.components[i+1] = (debug_real_argb_tv_white->components[i+1] * v6) + (global_real_argb_black->components[i+1] * (1.0f - v6));
	}

	return &background_color;
}

c_debug_menu* c_debug_menu_item::get_child()
{
	return m_child_ref;
}

bool c_debug_menu_item::get_data()
{
	return m_data;
}

const real_argb_color* c_debug_menu_item::get_enabled_color()
{
	if (get_child())
	{
		return debug_real_argb_tv_green;
	}

	return debug_real_argb_grey;
}

short c_debug_menu_item::get_index()
{
	for (short item_index = 0; item_index < get_menu()->get_num_items(); item_index++)
	{
		if (get_menu()->get_item(item_index) == this)
		{
			return item_index;
		}
	}

	return 0;
}

bool c_debug_menu_item::get_is_selection()
{
	return get_index() == get_menu()->get_selection();
}

c_debug_menu* c_debug_menu_item::get_menu()
{
	return m_menu_ref;
}

const char* c_debug_menu_item::get_name()
{
	return m_name ? m_name : "";
}

void c_debug_menu_item::notify_left()
{
}

void c_debug_menu_item::notify_right()
{
}

void c_debug_menu_item::notify_selected()
{
	if (get_child())
	{
		extern void debug_menu_set_active_menu(c_debug_menu* active_menu, bool dont_open);
		debug_menu_set_active_menu(get_child(), false);
	}
}

void c_debug_menu_item::open()
{
	set_active(true);
}

void c_debug_menu_item::render(c_font_cache_base* font_cache, const point2d& position)
{
	const real_argb_color* color = global_real_argb_black;
	if (get_active() && get_menu()->get_enabled())
	{
		color = get_enabled_color();
	}

	extern void draw_string_simple(c_font_cache_base* font_cache, const char* text, const point2d& position, const real_argb_color* color);
	draw_string_simple(font_cache, get_name(), position, color);
}

void c_debug_menu_item::set_active(bool active)
{
	m_active = active;
}

void c_debug_menu_item::set_data(long data)
{
	m_data = data;
}

void c_debug_menu_item::set_name(const char* name)
{
	if (!name) return;

	long name_size = strlen(name) + 1;

	extern void* debug_menu_malloc(long size);
	m_name = static_cast<char*>(debug_menu_malloc(name_size));
	strncpy(m_name, name, name_size);
	m_name[name_size - 1] = '\0';
}

void c_debug_menu_item::update()
{
}
