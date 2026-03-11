#include "stdafx.h"
#include "debug_menu.h"
#include "debug_menu_item.h"
#include "debug_menu_main.h"

#include <string.h>

// Xbox 360: Custom assert macro
#define ASSERT(x) if(!(x)) { __debugbreak(); }
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))

c_debug_menu::~c_debug_menu()
{
	clear();
}

void c_debug_menu::update()
{
	if (get_selection() >= 0 && get_selection() < get_num_items() && !get_item(get_selection())->get_active())
	{
		SHORT selection = get_selection();
		SHORT num_items = get_num_items();
		for (SHORT i = 1; i < get_num_items(); i++)
		{
			SHORT item_index = (selection + i) % num_items;
			if (get_item(item_index)->get_active())
			{
				set_selection(item_index);
				break;
			}
		}
	}

	for (SHORT item_index = 0; item_index < get_num_items(); item_index++)
		get_item(item_index)->update();

	const gamepad_state& state = debug_menu_get_gamepad_state();
	const gamepad_state& last_state = debug_menu_get_last_gamepad_state();
	
	// Xbox 360: Input state latching handled by game
	if (get_enabled())
	{
		if (state.button_frames[_gamepad_binary_button_dpad_up])
		{
			notify_up();
		}
		else if (state.button_frames[_gamepad_binary_button_dpad_down])
		{
			notify_down();
		}
		else if (state.button_frames[_gamepad_binary_button_dpad_left])
		{
			try_left();
		}
		else if (state.button_frames[_gamepad_binary_button_dpad_right])
		{
			try_right();
		}
		else
		{
			m_last_up = 0;
			m_last_down = 0;
			m_last_left = 0;
			m_last_right = 0;
		}
	}

	if (get_enabled()
		&& get_num_items() > 0
		&& get_item(get_selection())->get_active()
		&& (state.button_frames[_gamepad_binary_button_a] == 1))
	{
		get_item(get_selection())->notify_selected();
		notify_selected(get_selection());
	}
	else if (!state.button_frames[_gamepad_binary_button_b] &&
		last_state.button_frames[_gamepad_binary_button_b])
	{
		close(FALSE);
	}
	else
	{
		// Xbox 360: Numbered selection 1-9, 0 = 10
		SHORT selection = -1;
		
		// Check for number keys 1-9
		for (SHORT i = 0; i < 9; i++)
		{
			if (state.button_frames[_gamepad_binary_button_0 + i])
			{
				selection = i;
				break;
			}
		}
		
		if (selection != -1)
		{
			if (selection < get_num_items())
			{
				set_selection(selection);
				get_item(get_selection())->notify_selected();
				notify_selected(get_selection());
			}
		}
	}
}

void c_debug_menu::render(c_font_cache_base* font_cache, const point2d& point)
{
	render_background(font_cache, point);
	render_title(font_cache, point);
	render_caption(font_cache, point);
	render_global_caption(font_cache, point);
	render_items(font_cache, point, 0, get_num_items() - 1);
}

void c_debug_menu::game_render()
{
}

void c_debug_menu::notify_selected(SHORT selected_value)
{
}

void c_debug_menu::open()
{
	for (SHORT item_index = 0; item_index < get_num_items(); item_index++)
		get_item(item_index)->open();

	set_selection(0);
	set_enabled(TRUE);
}

const char* c_debug_menu::get_caption()
{
	return m_caption ? m_caption : "";
}

void c_debug_menu::notify_closed()
{
}

void c_debug_menu::notify_activated()
{
}

DWORD c_debug_menu::get_num_items_to_render()
{
	return get_num_items();
}

void c_debug_menu::close(BOOL closed)
{
	debug_menu_set_active_menu(get_parent(), FALSE);
	notify_closed();

	if (get_parent())
	{
		if (closed)
			get_parent()->notify_selection_closed();
		else
			get_parent()->notify_selection_exited();
	}
}

SHORT c_debug_menu::get_value_width()
{
	return 0;
}

void c_debug_menu::notify_selection_closed()
{
}

void c_debug_menu::notify_selection_exited()
{
}

void c_debug_menu::notify_up()
{
	if (!is_active_menu())
		return;

	if (debug_menu_get_time() - m_last_up < get_menu_rate())
		return;

	if (!get_num_items())
		return;

	SHORT selection = get_selection();
	m_last_up = debug_menu_get_time();
	
	c_debug_menu_item* item = NULL;
	do
	{
		SHORT current_selection = get_selection();
		if (get_selection() <= 0)
			current_selection = get_num_items();
		set_selection(current_selection - 1);
	
		item = get_item(get_selection());
	} while (!item->get_active());
	
	if (!get_item(get_selection())->get_active())
		set_selection(selection);
}

void c_debug_menu::notify_down()
{
	if (!is_active_menu())
		return;

	if (debug_menu_get_time() - m_last_down < get_menu_rate())
		return;

	if (!get_num_items())
		return;

	SHORT selection = get_selection();
	m_last_down = debug_menu_get_time();
	
	c_debug_menu_item* item = NULL;
	do
	{
		SHORT current_selection = get_selection();
		if (current_selection >= get_num_items() - 1)
			set_selection(0);
		else
			set_selection(get_selection() + 1);
	
		item = get_item(get_selection());
	} while (!item->get_active());
	
	if (!get_item(get_selection())->get_active())
		set_selection(selection);
}

void c_debug_menu::notify_left()
{
}

void c_debug_menu::notify_right()
{
}

BOOL c_debug_menu::is_active_menu()
{
	for (SHORT item_index = 0; item_index < get_num_items(); item_index++)
	{
		if (get_item(item_index)->get_active())
			return TRUE;
	}

	return FALSE;
};

SHORT c_debug_menu::get_menu_rate()
{
	return 5;
};

SHORT c_debug_menu::get_max_active_captions()
{
	SHORT max_active_captions = *get_caption() != 0;
	for (SHORT caption_index = 0; caption_index < DEBUG_MENU_NUM_GLOBAL_CAPTIONS; caption_index++)
	{
		if (*debug_menu_get_caption(caption_index))
			max_active_captions = caption_index + 2;
	}

	return max_active_captions;
}

void c_debug_menu::render_background(c_font_cache_base* font_cache, const point2d& point)
{
	FLOAT item_margin = get_value_width() ? debug_menu_get_item_margin() : 0.0f;

	SHORT x0 = (SHORT)(((point.x - debug_menu_get_item_margin()) - get_num_items_to_render()) - item_margin) - 60.0;
	SHORT y0 = point.y;
	SHORT x1 = (SHORT)((point.x + debug_menu_get_item_width()) + debug_menu_get_item_margin());
	SHORT y1 = (SHORT)((point.y + get_title_height()) + (get_num_items_to_render() + get_max_active_captions()) * get_item_height());
	FLOAT alpha = get_enabled() ? 0.7f : 0.1f;

	debug_menu_draw_rect(x0, y0, x1, y1, alpha, debug_real_argb_tv_blue);
}

void c_debug_menu::render_title(c_font_cache_base* font_cache, const point2d& point)
{
	c_rasterizer_draw_string draw_string;
	draw_string.initialize();

	rectangle2d bounds;
	interface_get_current_display_settings(NULL, NULL, NULL, &bounds);

	SHORT x0 = point.x;
	SHORT y0 = (SHORT)(point.y + debug_menu_get_item_indent_y());
	SHORT x1 = (SHORT)(point.x + debug_menu_get_item_width());
	SHORT y1 = (SHORT)((point.y + get_item_height()) - (2.0f * debug_menu_get_item_indent_y()));
	FLOAT alpha = get_enabled() ? 0.7f : 0.1f;

	debug_menu_draw_rect(x0, y0, x1, y1, alpha, debug_real_argb_grey);

	set_rectangle2d(&bounds, point.x, point.y, (SHORT)(point.x + debug_menu_get_item_width()), bounds.y1);
	draw_string.set_bounds(&bounds);
	draw_string.set_color(debug_real_argb_tv_magenta);
	draw_string.draw(font_cache, m_name);
}

void c_debug_menu::render_caption(c_font_cache_base* font_cache, const point2d& point)
{
	c_rasterizer_draw_string draw_string;
	draw_string.initialize();

	rectangle2d bounds;
	interface_get_current_display_settings(NULL, NULL, NULL, &bounds);
	if (*get_caption())
	{
		SHORT x0 = point.x;
		SHORT y0 = (SHORT)(point.y + debug_menu_get_item_indent_y());
		SHORT x1 = (SHORT)(point.x + debug_menu_get_item_width());
		SHORT y1 = (SHORT)(((point.y + get_title_height()) + get_num_items_to_render() * get_item_height()) - (2.0f * debug_menu_get_item_indent_y()));
		FLOAT alpha = get_enabled() ? 0.7f : 0.1f;

		debug_menu_draw_rect(x0, y0, x1, y1, alpha, debug_real_argb_grey);
	}

	set_rectangle2d(&bounds, point.x, (SHORT)((point.y + get_title_height()) + get_num_items_to_render() * get_item_height()), (SHORT)(point.x + debug_menu_get_item_width()), bounds.y1);
	draw_string.draw(font_cache, get_caption());
}

void c_debug_menu::render_global_caption(c_font_cache_base* font_cache, const point2d& point)
{
	c_rasterizer_draw_string draw_string;
	draw_string.initialize();

	rectangle2d bounds;
	interface_get_current_display_settings(NULL, NULL, NULL, &bounds);

	set_rectangle2d(&bounds, point.x, (SHORT)((point.y + get_title_height()) * (get_num_items_to_render() + 1) * get_item_height()), (SHORT)(point.x + debug_menu_get_item_width()), bounds.y1);
	draw_string.set_color(debug_real_argb_tv_magenta);

	for (SHORT caption_index = 0; caption_index < DEBUG_MENU_NUM_GLOBAL_CAPTIONS; caption_index++)
	{
		if (*debug_menu_get_caption(caption_index))
		{
			SHORT x0 = point.x;
			SHORT y0 = (SHORT)(((point.y + get_title_height()) + ((caption_index + 1) + get_num_items_to_render()) * get_item_height()) + debug_menu_get_item_indent_y());
			SHORT x1 = (SHORT)(point.x + debug_menu_get_item_width());
			SHORT y1 = (SHORT)(((point.y + get_title_height()) + ((caption_index + 2) + get_num_items_to_render()) * get_item_height()) - (2.0f * debug_menu_get_item_indent_y()));
			FLOAT alpha = get_enabled() ? 0.7f : 0.1f;

			debug_menu_draw_rect(x0, y0, x1, y1, alpha, debug_real_argb_grey);
		}

		if (rectangle2d_width(&bounds) > 0 && rectangle2d_height(&bounds) > 0)
		{
			draw_string.set_bounds(&bounds);
			draw_string.draw(font_cache, debug_menu_get_caption(caption_index));
		}

		debug_menu_set_caption(caption_index, "");
		bounds.y0 += get_item_height();
	}
}

void c_debug_menu::render_items(c_font_cache_base* font_cache, const point2d& point, SHORT start_index, SHORT end_index)
{
	ASSERT(start_index >= 0);
	ASSERT(start_index <= end_index);

	for (SHORT item_index = start_index; item_index <= end_index && item_index < get_num_items(); item_index++)
	{
		c_debug_menu_item* item = get_item(item_index);
		if (item->get_active())
		{
			SHORT x0 = point.x;
			SHORT y0 = (SHORT)(((point.y + get_title_height()) + (item_index - start_index) * get_item_height()) + debug_menu_get_item_indent_y());
			SHORT x1 = (SHORT)(point.x + debug_menu_get_item_width());
			SHORT y1 = (SHORT)((((point.y + get_title_height()) + (item_index - start_index + 1) * get_item_height())) - (2.0f * debug_menu_get_item_indent_y()));
			FLOAT alpha = get_enabled() ? 0.7f : 0.1f;

			debug_menu_draw_rect(x0, y0, x1, y1, alpha, item->get_background_color());
		}

		point2d item_point;
		set_point2d(&item_point, point.x, point.y + (get_title_height() + (item_index - start_index) * get_item_height()));
		item->render(font_cache, item_point);
	}
}

void c_debug_menu::try_left()
{
	ASSERT(get_selection() >= 0);

	if (debug_menu_get_time() - m_last_left < get_menu_rate())
		return;

	if (!get_num_items())
		return;

	m_last_left = debug_menu_get_time();
	notify_left();

	if (get_selection() >= get_num_items())
		return;

	get_item(get_selection())->notify_left();
}

void c_debug_menu::try_right()
{
	ASSERT(get_selection() >= 0);

	if (debug_menu_get_time() - m_last_right < get_menu_rate())
		return;

	if (!get_num_items())
		return;

	m_last_right = debug_menu_get_time();
	notify_right();

	if (get_selection() >= get_num_items())
		return;

	get_item(get_selection())->notify_right();
}

c_debug_menu::c_debug_menu(c_debug_menu* parent, const char* name) :
	m_name(NULL),
	m_caption(NULL),
	m_parent_ref(parent)
{
	set_name(name ? name : "Menu???");
	set_num_items(0);
	set_selection(0);
	set_enabled(TRUE);
	
	m_last_up = 0;
	m_last_down = 0;
	m_last_left = 0;
	m_last_right = 0;
}

void c_debug_menu::clear()
{
	for (SHORT item_index = 0; item_index < get_num_items(); item_index++)
	{
		if (c_debug_menu_item* item = m_items[item_index])
			item->~c_debug_menu_item();
		m_items[item_index] = NULL;
	}

	set_num_items(0);
}

SHORT c_debug_menu::get_num_items()
{
	ASSERT(m_num_items >= 0 && m_num_items <= k_max_items);

	return m_num_items;
}

void c_debug_menu::set_num_items(SHORT num_items)
{
	ASSERT(num_items >= 0 && num_items <= k_max_items);

	m_num_items = num_items;
}

SHORT c_debug_menu::get_selection()
{
	return m_selection;
}

void c_debug_menu::set_selection(SHORT selection)
{
	ASSERT(selection >= 0 && (get_num_items() == 0 || selection < get_num_items()));

	m_selection = selection;
}

c_debug_menu_item* c_debug_menu::get_item(SHORT item_index)
{
	ASSERT(get_num_items() >= 0 && get_num_items() < k_max_items);
	ASSERT(m_items[item_index] != NULL);
	ASSERT(VALID_INDEX(item_index, get_num_items()));

	return m_items[item_index];
}

void c_debug_menu::add_item(c_debug_menu_item* item)
{
	ASSERT(get_num_items() < k_max_items);

	m_items[get_num_items()] = item;
	set_num_items(get_num_items() + 1);
}

const char* c_debug_menu::get_name()
{
	return m_name;
}

void c_debug_menu::set_name(const char* name)
{
	DWORD name_size = (DWORD)strlen(name) + 1;

	ASSERT(m_name == NULL);
	m_name = (char*)debug_menu_malloc(name_size);
	strncpy_s(m_name, name_size, name, name_size - 1);
}

void c_debug_menu::set_caption(const char* caption)
{
	DWORD caption_size = (DWORD)strlen(caption) + 1;

	m_caption = (char*)debug_menu_malloc(caption_size);
	strncpy_s(m_caption, caption_size, caption, caption_size - 1);
}

c_debug_menu* c_debug_menu::get_parent()
{
	return m_parent_ref;
}

BOOL c_debug_menu::get_enabled()
{
	return m_enabled;
}

void c_debug_menu::set_enabled(BOOL enable)
{
	m_enabled = enable;
}

SHORT c_debug_menu::get_title_height()
{
	return (SHORT)debug_menu_get_title_height();
}

SHORT c_debug_menu::get_item_indent()
{
	return (SHORT)debug_menu_get_item_indent_x();
}

SHORT c_debug_menu::get_item_height()
{
	return (SHORT)debug_menu_get_item_height();
}
