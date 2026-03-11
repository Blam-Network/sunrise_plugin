#include "stdafx.h"
#include "debug_menu_item.h"
#include "debug_menu.h"
#include "debug_menu_main.h"

#include <string.h>

// Xbox 360: Custom assert macro
#define ASSERT(x) if(!(x)) { __debugbreak(); }
#define UNREACHABLE() __debugbreak()
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))

c_debug_menu_item::c_debug_menu_item(c_debug_menu* menu, const char* name, c_debug_menu* child, BOOL active) :
	m_name(NULL),
	m_menu_ref(menu),
	m_child_ref(child)
{
	set_name(name ? name : "Item???");
	set_active(active);
	set_data(-1);
}

c_debug_menu_item::~c_debug_menu_item()
{
	if (m_child_ref)
	{
		m_child_ref->~c_debug_menu();
	}
	m_child_ref = NULL;
}

BOOL c_debug_menu_item::get_active()
{
    return m_active;
}

const real_argb_color* c_debug_menu_item::get_background_color()
{
	if (!get_is_selection())
	{
		return debug_real_argb_tv_white;
	}

	FLOAT v4 = (debug_menu_get_time() % 80) / 79.0f;
	FLOAT v5 = v4 + v4;
	if (v4 >= 0.5f)
	{
		v5 = 1.0f - ((v4 - 0.5f) + (v4 - 0.5f));
	}

	static real_argb_color background_color;
	background_color.alpha = 1.0f;
	for (DWORD i = 0; i < NUMBEROF(background_color.rgb.n); i++)
	{
		FLOAT v6 = (v5 * 0.4f) + 0.05f;
		background_color.rgb.n[i] = (debug_real_argb_tv_white->rgb.n[i] * v6) + (global_real_argb_black->rgb.n[i] * (1.0f - v6));
	}

	return &background_color;
}

c_debug_menu* c_debug_menu_item::get_child()
{
	return m_child_ref;
}

BOOL c_debug_menu_item::get_data()
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

SHORT c_debug_menu_item::get_index()
{
	for (SHORT item_index = 0; item_index < get_menu()->get_num_items(); item_index++)
	{
		if (get_menu()->get_item(item_index) == this)
		{
			return item_index;
		}
	}

	UNREACHABLE();
	return 0;
}

BOOL c_debug_menu_item::get_is_selection()
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
		debug_menu_set_active_menu(get_child(), FALSE);
	}
}

void c_debug_menu_item::open()
{
	set_active(TRUE);
}

void c_debug_menu_item::render(c_font_cache_base* font_cache, const point2d& position)
{
	const real_argb_color* color = global_real_argb_black;
	if (get_active() && get_menu()->get_enabled())
	{
		color = get_enabled_color();
	}

	c_rasterizer_draw_string draw_string;
	draw_string.initialize();

	rectangle2d bounds;
	interface_get_current_display_settings(NULL, NULL, NULL, &bounds);
	set_rectangle2d(&bounds, position.x, position.y, bounds.x1, bounds.y1);

	draw_string.set_bounds(&bounds);
	draw_string.set_color(color);
	draw_string.draw(font_cache, get_name());
}

void c_debug_menu_item::set_active(BOOL active)
{
	m_active = active;
}

void c_debug_menu_item::set_data(DWORD data)
{
	m_data = data;
}

void c_debug_menu_item::set_name(const char* name)
{
	ASSERT(name);

	DWORD name_size = (DWORD)strlen(name) + 1;

	ASSERT(name != NULL && m_name == NULL);
	m_name = (char*)debug_menu_malloc(name_size);
	strncpy_s(m_name, name_size, name, name_size - 1);
}

void c_debug_menu_item::update()
{
}
