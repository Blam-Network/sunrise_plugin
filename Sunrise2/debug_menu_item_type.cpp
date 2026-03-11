#include "stdafx.h"
#include "debug_menu_item_type.h"
#include "debug_menu.h"
#include "debug_menu_main.h"

// Xbox 360: MIN/MAX macros
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

void c_debug_menu_item_type::render(c_font_cache_base* font_cache, const point2d& position)
{
	point2d value_point;
	point2d number_point;
	point2d next_point;

	set_point2d(&value_point, position.x - 66, position.y);
	set_point2d(&number_point, position.x, position.y);
	set_point2d(&next_point, position.x + get_indent(), position.y);

	render_value(font_cache, value_point);
	render_number(font_cache, number_point);
	c_debug_menu_item::render(font_cache, next_point);
}

void c_debug_menu_item_type::to_string(char* buffer, DWORD buffer_size)
{
	strncpy_s(buffer, buffer_size, "overload toString", buffer_size - 1);
}

void c_debug_menu_item_type::render_value(c_font_cache_base* font_cache, const point2d& position)
{
	c_rasterizer_draw_string draw_string;
	draw_string.initialize();

	char buffer[1024];
	memset(buffer, 0, sizeof(buffer));
	to_string(buffer, sizeof(buffer));

	rectangle2d bounds;
	set_rectangle2d(&bounds, position.x + 2, position.y, position.x + 60, position.y + get_menu()->get_item_height());

	if (get_active())
	{
		FLOAT alpha = get_menu()->get_enabled() ? 0.7f : 0.1f;

		SHORT x0 = position.x;
		SHORT y0 = (SHORT)(position.y + debug_menu_get_item_indent_y());
		SHORT x1 = (SHORT)(position.x + 60);
		SHORT y1 = (SHORT)((position.y + get_menu()->get_item_height()) - (2.0f * debug_menu_get_item_indent_y()));

		debug_menu_draw_rect(x0, y0, x1, y1, alpha, get_background_color());
	}

	const real_argb_color* color = debug_real_argb_grey;
	if (!get_active() || get_readonly())
		color = global_real_argb_black;

	draw_string.set_color(color);
	draw_string.set_bounds(&bounds);
	draw_string.draw(font_cache, buffer);
}

c_debug_menu_item_type::c_debug_menu_item_type(c_debug_menu* menu, const char* name, BOOL readonly) :
	c_debug_menu_item_numbered(menu, name, NULL),
	m_readonly(readonly)
{
}

BOOL c_debug_menu_item_type::get_readonly()
{
	return m_readonly;
}

void c_debug_menu_item_type_bool::notify_left()
{
	c_debug_menu_item::notify_left();

	if (!get_readonly())
		m_value.set(!m_value.get());
}

void c_debug_menu_item_type_bool::notify_right()
{
	c_debug_menu_item::notify_right();

	if (!get_readonly())
		m_value.set(!m_value.get());
}

void c_debug_menu_item_type_bool::to_string(char* buffer, DWORD buffer_size)
{
	sprintf_s(buffer, buffer_size, "%s", m_value.get() ? "True" : "False");
}

c_debug_menu_item_type_bool::c_debug_menu_item_type_bool(c_debug_menu* menu, const char* name, BOOL readonly, const char* hs_global_name) :
	c_debug_menu_item_type(menu, name, readonly),
	m_value(hs_global_name)
{
}

void c_debug_menu_item_type_real::notify_left()
{
	c_debug_menu_item::notify_left();

	if (!get_readonly())
		m_value.set(MIN(m_max, MAX(m_min, m_value.get() - m_inc)));
}

void c_debug_menu_item_type_real::notify_right()
{
	c_debug_menu_item::notify_right();

	if (!get_readonly())
		m_value.set(MIN(m_max, MAX(m_min, m_value.get() + m_inc)));
}

void c_debug_menu_item_type_real::to_string(char* buffer, DWORD buffer_size)
{
	sprintf_s(buffer, buffer_size, "%f", m_value.get());
}

c_debug_menu_item_type_real::c_debug_menu_item_type_real(c_debug_menu* menu, const char* name, BOOL readonly, const char* variable, FLOAT min, FLOAT max, FLOAT inc) :
	c_debug_menu_item_type(menu, name, readonly),
	m_value(variable),
	m_min(min),
	m_max(max),
	m_inc(inc)
{
}

void c_debug_menu_item_type_short::notify_left()
{
	c_debug_menu_item::notify_left();

	if (!get_readonly())
		m_value.set((SHORT)MIN(m_max, MAX(m_min, m_value.get() - m_inc)));
}

void c_debug_menu_item_type_short::notify_right()
{
	c_debug_menu_item::notify_right();

	if (!get_readonly())
		m_value.set((SHORT)MIN(m_max, MAX(m_min, m_value.get() + m_inc)));
}

void c_debug_menu_item_type_short::to_string(char* buffer, DWORD buffer_size)
{
	sprintf_s(buffer, buffer_size, "%d", m_value.get());
}

c_debug_menu_item_type_short::c_debug_menu_item_type_short(c_debug_menu* menu, const char* name, BOOL readonly, const char* variable, SHORT min, SHORT max, SHORT inc) :
	c_debug_menu_item_type(menu, name, readonly),
	m_value(variable),
	m_min(min),
	m_max(max),
	m_inc(inc)
{
}

void c_debug_menu_item_type_long::notify_left()
{
	c_debug_menu_item::notify_left();

	if (!get_readonly())
		m_value.set(MIN(m_max, MAX(m_min, m_value.get() - m_inc)));
}

void c_debug_menu_item_type_long::notify_right()
{
	c_debug_menu_item::notify_right();

	if (!get_readonly())
		m_value.set(MIN(m_max, MAX(m_min, m_value.get() + m_inc)));
}

void c_debug_menu_item_type_long::to_string(char* buffer, DWORD buffer_size)
{
	sprintf_s(buffer, buffer_size, "%d", m_value.get());
}

c_debug_menu_item_type_long::c_debug_menu_item_type_long(c_debug_menu* menu, const char* name, BOOL readonly, const char* variable, DWORD min, DWORD max, DWORD inc) :
	c_debug_menu_item_type(menu, name, readonly),
	m_value(variable),
	m_min(min),
	m_max(max),
	m_inc(inc)
{
}
