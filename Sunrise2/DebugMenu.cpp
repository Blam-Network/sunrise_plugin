#include "stdafx.h"
#include "DebugMenu.h"
#include "DebugMenuItem.h"
#include "Utilities.h"
#include <string.h>
#include <stdio.h>

// Global instance
s_debug_menu_globals g_debug_menu_globals = {};

// Stack allocator for debug menu memory
static long g_debug_menu_stack[k_debug_menu_stack_size / 4] = {};
static long g_debug_menu_stack_count = 0;

// Color constants
static real_argb_color const instance_debug_real_argb_grey       = { 1.0f, 0.3f, 0.42f, 0.33f };
static real_argb_color const instance_debug_real_argb_white      = { 1.0f, 0.9f, 0.9f,  0.8f  };
static real_argb_color const instance_debug_real_argb_tv_white   = { 1.0f, 0.8f, 0.8f,  0.75f };
static real_argb_color const instance_debug_real_argb_tv_blue    = { 1.0f, 0.2f, 0.2f,  0.45f };
static real_argb_color const instance_debug_real_argb_tv_magenta = { 1.0f, 0.7f, 0.05f, 0.7f  };
static real_argb_color const instance_debug_real_argb_tv_orange  = { 1.0f, 1.0f, 0.5f,  0.0f  };
static real_argb_color const instance_debug_real_argb_tv_green   = { 1.0f, 0.05f, 0.65f, 0.05f };
static real_argb_color const instance_global_real_argb_black     = { 1.0f, 0.0f, 0.0f,  0.0f  };

const real_argb_color* const debug_real_argb_grey       = &instance_debug_real_argb_grey;
const real_argb_color* const debug_real_argb_white      = &instance_debug_real_argb_white;
const real_argb_color* const debug_real_argb_tv_white   = &instance_debug_real_argb_tv_white;
const real_argb_color* const debug_real_argb_tv_blue    = &instance_debug_real_argb_tv_blue;
const real_argb_color* const debug_real_argb_tv_magenta = &instance_debug_real_argb_tv_magenta;
const real_argb_color* const debug_real_argb_tv_orange  = &instance_debug_real_argb_tv_orange;
const real_argb_color* const debug_real_argb_tv_green   = &instance_debug_real_argb_tv_green;
const real_argb_color* const global_real_argb_black     = &instance_global_real_argb_black;

// Memory allocator for debug menu
void* debug_menu_malloc(long size)
{
	long size_in_count = (size + 3) >> 2;
	if (g_debug_menu_stack_count + size_in_count >= k_debug_menu_stack_size / 4)
	{
		Sunrise_Dbg("Debug menu stack overflow!");
		return nullptr;
	}

	void* result = &g_debug_menu_stack[g_debug_menu_stack_count];
	g_debug_menu_stack_count += size_in_count;
	return result;
}

// Helper functions
long debug_menu_get_time()
{
	return long((GetTickCount() * 30.0f) / 1000.0f + 0.5f);
}

float debug_menu_get_item_margin()
{
	return 10.0f;
}

float debug_menu_get_item_width()
{
	return 650.0f;
}

float debug_menu_get_item_height()
{
	return 20.0f;
}

float debug_menu_get_title_height()
{
	return 20.0f;
}

float debug_menu_get_item_indent_x()
{
	return 40.0f;
}

float debug_menu_get_item_indent_y()
{
	return 2.0f;
}

c_debug_menu* debug_menu_get_root()
{
	return g_debug_menu_globals.m_main_menu;
}

c_debug_menu* debug_menu_get_active_menu()
{
	return g_debug_menu_globals.m_active_menu;
}

bool debug_menu_get_active()
{
	return g_debug_menu_globals.m_active_menu != nullptr;
}

const gamepad_state& debug_menu_get_gamepad_state()
{
	return g_debug_menu_globals.m_current_gamepad;
}

const gamepad_state& debug_menu_get_last_gamepad_state()
{
	return g_debug_menu_globals.m_last_gamepad;
}

void debug_menu_set_caption(short caption_index, const char* caption)
{
	if (caption_index < 0 || caption_index >= DEBUG_MENU_NUM_GLOBAL_CAPTIONS) return;
	strncpy(g_debug_menu_globals.m_caption[caption_index], caption, s_debug_menu_globals::k_str_length);
	g_debug_menu_globals.m_caption[caption_index][s_debug_menu_globals::k_str_length] = '\0';
}

const char* debug_menu_get_caption(short caption_index)
{
	if (caption_index < 0 || caption_index >= DEBUG_MENU_NUM_GLOBAL_CAPTIONS) return "";
	return g_debug_menu_globals.m_caption[caption_index];
}

// Convert ARGB color to pixel32
static unsigned long real_argb_color_to_pixel32(const real_argb_color* color)
{
	if (!color) return 0xFFFFFFFF;
	
	unsigned char a = (unsigned char)(color->alpha * 255.0f);
	unsigned char r = (unsigned char)(color->red * 255.0f);
	unsigned char g = (unsigned char)(color->green * 255.0f);
	unsigned char b = (unsigned char)(color->blue * 255.0f);
	
	return (a << 24) | (r << 16) | (g << 8) | b;
}

// Draw rectangle using game's rasterizer
void debug_menu_draw_rect(short x0, short y0, short x1, short y1, float alpha, const real_argb_color* color)
{
	point2d points[4];
	
	set_point2d(&points[0], x0, y1);
	set_point2d(&points[1], x1, y1);
	set_point2d(&points[2], x1, y0);
	set_point2d(&points[3], x0, y0);
	
	extern void rasterizer_quad_screenspace(const point2d* points, unsigned long color);
	rasterizer_quad_screenspace(points, real_argb_color_to_pixel32(color));
}

// Simplified text drawing wrapper
void draw_string_simple(c_font_cache_base* font_cache, const char* text, const point2d& position, const real_argb_color* color)
{
	extern void draw_string_at_position(const char* text, short x, short y, unsigned long color);
	draw_string_at_position(text, position.x, position.y, real_argb_color_to_pixel32(color));
}

// Initialize the debug menu system
void debug_menu_initialize()
{
}

void debug_menu_dispose()
{
}

void debug_menu_initialize_for_new_map()
{
	memset(&g_debug_menu_globals, 0, sizeof(s_debug_menu_globals));
	g_debug_menu_stack_count = 0;
	
	g_debug_menu_globals.m_main_menu = DEBUG_MENU_MALLOC(c_main_menu);
	
	Sunrise_Dbg("Debug menu initialized for new map");
}

void debug_menu_dispose_from_old_map()
{
	if (g_debug_menu_globals.m_main_menu)
	{
		g_debug_menu_stack_count = 0;
		g_debug_menu_globals.m_main_menu = nullptr;
	}
	g_debug_menu_globals.m_active_menu = nullptr;
}

// Update current gamepad state from XINPUT
static void debug_menu_update_current_gamepad_state()
{
	memset(&g_debug_menu_globals.m_current_gamepad, 0, sizeof(gamepad_state));
	
	XINPUT_STATE state;
	if (XInputGetState(0, &state) == ERROR_SUCCESS)
	{
		for (int i = 0; i < 16; i++)
		{
			bool button_down = false;
			switch (i)
			{
				case 0: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0; break;
				case 1: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0; break;
				case 2: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0; break;
				case 3: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0; break;
				case 4: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0; break;
				case 5: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0; break;
				case 6: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0; break;
				case 7: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0; break;
				case 8: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0; break;
				case 9: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0; break;
				case 10: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0; break;
				case 11: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0; break;
				case 12: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0; break;
				case 13: button_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0; break;
			}
			
			if (button_down)
			{
				if (g_debug_menu_globals.m_current_gamepad.button_frames[i] < 255)
					g_debug_menu_globals.m_current_gamepad.button_frames[i]++;
			}
			else
			{
				g_debug_menu_globals.m_current_gamepad.button_frames[i] = 0;
			}
		}
	}
}

void debug_menu_update()
{
	debug_menu_update_current_gamepad_state();

	const gamepad_state& state = debug_menu_get_gamepad_state();
	
	if (state.button_frames[6])
	{
		g_debug_menu_globals.m_do_render = false;
	}
	else
	{
		bool should_toggle = (state.button_frames[5] == 1 && state.button_frames[4]) ||
		                     (state.button_frames[5] && state.button_frames[4] == 1);
		
		if (should_toggle)
		{
			if (debug_menu_get_active())
			{
				debug_menu_close();
			}
			else
			{
				debug_menu_open();
			}
		}

		if (debug_menu_get_active())
		{
			debug_menu_get_active_menu()->update();
		}

		g_debug_menu_globals.m_do_render = true;
	}

	g_debug_menu_globals.m_last_gamepad = g_debug_menu_globals.m_current_gamepad;
}

void debug_menu_open()
{
	if (debug_menu_get_root() && !debug_menu_get_active())
	{
		debug_menu_set_active_menu(debug_menu_get_root(), false);
		g_debug_menu_globals.open_menu_time = GetTickCount();
	}
}

void debug_menu_close()
{
	if (debug_menu_get_root() && debug_menu_get_active() &&
		GetTickCount() - g_debug_menu_globals.open_menu_time > 100)
	{
		debug_menu_set_active_menu(nullptr, false);
	}
}

void debug_menu_set_active_menu(c_debug_menu* active_menu, bool dont_open)
{
	c_debug_menu* current_active_menu = g_debug_menu_globals.m_active_menu;

	bool menu_is_parent = false;
	if (g_debug_menu_globals.m_active_menu && active_menu)
	{
		if (c_debug_menu* parent = g_debug_menu_globals.m_active_menu->get_parent())
		{
			if (parent == active_menu)
				menu_is_parent = true;
		}
	}
	g_debug_menu_globals.m_active_menu = active_menu;

	if (active_menu)
	{
		if (!menu_is_parent && !dont_open)
		{
			active_menu->open();
		}
		active_menu->notify_activated();
	}
	else if (current_active_menu)
	{
		current_active_menu->notify_closed();
	}

	for (short caption_index = 0; caption_index < DEBUG_MENU_NUM_GLOBAL_CAPTIONS; caption_index++)
	{
		debug_menu_set_caption(caption_index, "");
	}
}

void render_debug_debug_menu()
{
	if (debug_menu_get_active() && g_debug_menu_globals.m_do_render)
	{
		c_font_cache_mt_safe font_cache;

		point2d position;
		set_point2d(&position, 180, 60);
		debug_menu_get_active_menu()->render(&font_cache, position);
	}
}

// Function pointer types for game rendering functions
typedef void (__fastcall *rasterizer_quad_screenspace_t)(const point2d points[4], unsigned long color, const void* tag_ref, byte param, bool param2);
typedef void* (__fastcall *c_draw_string_ctor_t)(void* draw_string_this);
typedef bool (__fastcall *c_draw_string_draw_t)(void* draw_string_this, void* font_cache, const char* text);
typedef void (__fastcall *c_draw_string_set_bounds_t)(void* draw_string_this, const rectangle2d* bounds);
typedef void (__fastcall *c_draw_string_set_color_t)(void* draw_string_this, const real_argb_color* color, bool deprecated);

// Game function pointers (addresses for Halo 3 TU2)
static rasterizer_quad_screenspace_t rasterizer_quad_screenspace_fn = (rasterizer_quad_screenspace_t)0x8219FBA8;
static c_draw_string_ctor_t c_draw_string_ctor_fn = (c_draw_string_ctor_t)0x821992c0;
static c_draw_string_draw_t c_draw_string_draw_fn = (c_draw_string_draw_t)0x822a1960;
static c_draw_string_set_bounds_t c_draw_string_set_bounds_fn = (c_draw_string_set_bounds_t)0x821996c8;
static c_draw_string_set_color_t c_draw_string_set_color_fn = (c_draw_string_set_color_t)0x822a1750;

// Wrapper for game's rasterizer function
void rasterizer_quad_screenspace(const point2d* points, unsigned long color)
{
	rasterizer_quad_screenspace_fn(points, color, nullptr, 0, false);
}

// c_draw_string state (actual size from decompilation is ~256 bytes)
struct c_draw_string_instance
{
	char data[256];
};

// Simplified text drawing using game's c_draw_string class
void draw_string_simple(c_font_cache_base* font_cache, const char* text, const point2d& position, const real_argb_color* color)
{
	if (!text || !text[0]) return;
	
	c_draw_string_instance draw_string;
	c_draw_string_ctor_fn(&draw_string);
	
	rectangle2d bounds;
	set_rectangle2d(&bounds, position.x, position.y, position.x + 1920, position.y + 1080);
	
	c_draw_string_set_color_fn(&draw_string, color, false);
	c_draw_string_set_bounds_fn(&draw_string, &bounds);
	c_draw_string_draw_fn(&draw_string, font_cache, text);
}

// c_debug_menu implementation

c_debug_menu::~c_debug_menu()
{
	clear();
}

void c_debug_menu::update()
{
	if (get_selection() >= 0 && get_selection() < get_num_items() && !get_item(get_selection())->get_active())
	{
		short selection = get_selection();
		short num_items = get_num_items();
		for (short i = 1; i < get_num_items(); i++)
		{
			short item_index = (selection + i) % num_items;
			if (get_item(item_index)->get_active())
			{
				set_selection(item_index);
				break;
			}
		}
	}

	for (short item_index = 0; item_index < get_num_items(); item_index++)
		get_item(item_index)->update();

	const gamepad_state& state = debug_menu_get_gamepad_state();
	
	if (get_enabled())
	{
		if (state.button_frames[0])
		{
			notify_up();
		}
		else if (state.button_frames[1])
		{
			notify_down();
		}
		else if (state.button_frames[2])
		{
			try_left();
		}
		else if (state.button_frames[3])
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

	if (get_enabled() && get_num_items() > 0 && get_item(get_selection())->get_active() && state.button_frames[10] == 1)
	{
		get_item(get_selection())->notify_selected();
		notify_selected(get_selection());
	}
	else if (!state.button_frames[11] && debug_menu_get_last_gamepad_state().button_frames[11])
	{
		close(false);
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

void c_debug_menu::notify_selected(short selected_value)
{
}

void c_debug_menu::open()
{
	for (short item_index = 0; item_index < get_num_items(); item_index++)
		get_item(item_index)->open();

	set_selection(0);
	set_enabled(true);
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

long c_debug_menu::get_num_items_to_render()
{
	return get_num_items();
}

void c_debug_menu::close(bool closed)
{
	debug_menu_set_active_menu(get_parent(), false);
	notify_closed();

	if (get_parent())
	{
		if (closed)
			get_parent()->notify_selection_closed();
		else
			get_parent()->notify_selection_exited();
	}
}

short c_debug_menu::get_value_width()
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

	short selection = get_selection();
	m_last_up = debug_menu_get_time();
	
	c_debug_menu_item* item = nullptr;
	do
	{
		short current_selection = get_selection();
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

	short selection = get_selection();
	m_last_down = debug_menu_get_time();
	
	c_debug_menu_item* item = nullptr;
	do
	{
		short current_selection = get_selection();
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

bool c_debug_menu::is_active_menu()
{
	for (short item_index = 0; item_index < get_num_items(); item_index++)
	{
		if (get_item(item_index)->get_active())
			return true;
	}

	return false;
}

short c_debug_menu::get_menu_rate()
{
	return 5;
}

short c_debug_menu::get_max_active_captions()
{
	short max_active_captions = *get_caption() != 0;
	for (short caption_index = 0; caption_index < DEBUG_MENU_NUM_GLOBAL_CAPTIONS; caption_index++)
	{
		if (*debug_menu_get_caption(caption_index))
			max_active_captions = caption_index + 2;
	}

	return max_active_captions;
}

void c_debug_menu::render_background(c_font_cache_base* font_cache, const point2d& point)
{
	float item_margin = get_value_width() ? debug_menu_get_item_margin() : 0.0f;

	short x0 = short((((point.x - debug_menu_get_item_margin()) - get_num_items_to_render()) - item_margin) - 60.0);
	short y0 = point.y;
	short x1 = short((point.x + debug_menu_get_item_width()) + debug_menu_get_item_margin());
	short y1 = short((point.y + get_title_height()) + (get_num_items_to_render() + get_max_active_captions()) * get_item_height());
	float alpha = get_enabled() ? 0.7f : 0.1f;

	debug_menu_draw_rect(x0, y0, x1, y1, alpha, debug_real_argb_tv_blue);
}

void c_debug_menu::render_title(c_font_cache_base* font_cache, const point2d& point)
{
	short x0 = point.x;
	short y0 = short(point.y + debug_menu_get_item_indent_y());
	short x1 = short(point.x + debug_menu_get_item_width());
	short y1 = short((point.y + get_item_height()) - (2.0f * debug_menu_get_item_indent_y()));
	float alpha = get_enabled() ? 0.7f : 0.1f;

	debug_menu_draw_rect(x0, y0, x1, y1, alpha, debug_real_argb_grey);

	point2d text_position;
	set_point2d(&text_position, point.x, point.y);
	draw_string_simple(font_cache, m_name, text_position, debug_real_argb_tv_magenta);
}

void c_debug_menu::render_caption(c_font_cache_base* font_cache, const point2d& point)
{
	if (*get_caption())
	{
		short x0 = point.x;
		short y0 = short(point.y + debug_menu_get_item_indent_y());
		short x1 = short(point.x + debug_menu_get_item_width());
		short y1 = short(((point.y + get_title_height()) + get_num_items_to_render() * get_item_height()) - (2.0f * debug_menu_get_item_indent_y()));
		float alpha = get_enabled() ? 0.7f : 0.1f;

		debug_menu_draw_rect(x0, y0, x1, y1, alpha, debug_real_argb_grey);
	}

	point2d text_position;
	set_point2d(&text_position, point.x, short((point.y + get_title_height()) + get_num_items_to_render() * get_item_height()));
	draw_string_simple(font_cache, get_caption(), text_position, debug_real_argb_white);
}

void c_debug_menu::render_global_caption(c_font_cache_base* font_cache, const point2d& point)
{
	for (short caption_index = 0; caption_index < DEBUG_MENU_NUM_GLOBAL_CAPTIONS; caption_index++)
	{
		if (*debug_menu_get_caption(caption_index))
		{
			short x0 = point.x;
			short y0 = short(((point.y + get_title_height()) + ((caption_index + 1) + get_num_items_to_render()) * get_item_height()) + debug_menu_get_item_indent_y());
			short x1 = short(point.x + debug_menu_get_item_width());
			short y1 = short(((point.y + get_title_height()) + ((caption_index + 2) + get_num_items_to_render()) * get_item_height()) - (2.0f * debug_menu_get_item_indent_y()));
			float alpha = get_enabled() ? 0.7f : 0.1f;

			debug_menu_draw_rect(x0, y0, x1, y1, alpha, debug_real_argb_grey);
		}

		point2d text_position;
		set_point2d(&text_position, point.x, short((point.y + get_title_height()) + ((caption_index + 1) + get_num_items_to_render()) * get_item_height()));
		draw_string_simple(font_cache, debug_menu_get_caption(caption_index), text_position, debug_real_argb_tv_magenta);

		debug_menu_set_caption(caption_index, "");
	}
}

void c_debug_menu::render_items(c_font_cache_base* font_cache, const point2d& point, short start_index, short end_index)
{
	for (short item_index = start_index; item_index <= end_index && item_index < get_num_items(); item_index++)
	{
		c_debug_menu_item* item = get_item(item_index);
		if (item->get_active())
		{
			short x0 = point.x;
			short y0 = short(((point.y + get_title_height()) + (item_index - start_index) * get_item_height()) + debug_menu_get_item_indent_y());
			short x1 = short(point.x + debug_menu_get_item_width());
			short y1 = short((((point.y + get_title_height()) + (item_index - start_index + 1) * get_item_height())) - (2.0f * debug_menu_get_item_indent_y()));
			float alpha = get_enabled() ? 0.7f : 0.1f;

			debug_menu_draw_rect(x0, y0, x1, y1, alpha, item->get_background_color());
		}

		point2d item_point;
		set_point2d(&item_point, point.x, short(point.y + (get_title_height() + (item_index - start_index) * get_item_height())));
		item->render(font_cache, item_point);
	}
}

void c_debug_menu::try_left()
{
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
	m_name(nullptr),
	m_caption(nullptr),
	m_parent_ref(parent),
	m_num_items(0),
	m_selection(0),
	m_enabled(true),
	m_last_up(0),
	m_last_down(0),
	m_last_left(0),
	m_last_right(0)
{
	memset(m_items, 0, sizeof(m_items));
	set_name(name ? name : "Menu???");
}

void c_debug_menu::clear()
{
	for (short item_index = 0; item_index < get_num_items(); item_index++)
	{
		if (c_debug_menu_item* item = m_items[item_index])
			item->~c_debug_menu_item();
		m_items[item_index] = nullptr;
	}

	set_num_items(0);
}

short c_debug_menu::get_num_items()
{
	return m_num_items;
}

void c_debug_menu::set_num_items(short num_items)
{
	m_num_items = num_items;
}

short c_debug_menu::get_selection()
{
	return m_selection;
}

void c_debug_menu::set_selection(short selection)
{
	m_selection = selection;
}

c_debug_menu_item* c_debug_menu::get_item(short item_index)
{
	if (item_index < 0 || item_index >= get_num_items())
		return nullptr;
	
	return m_items[item_index];
}

void c_debug_menu::add_item(c_debug_menu_item* item)
{
	if (get_num_items() >= k_max_items)
		return;

	m_items[get_num_items()] = item;
	set_num_items(get_num_items() + 1);
}

const char* c_debug_menu::get_name()
{
	return m_name;
}

void c_debug_menu::set_name(const char* name)
{
	if (!name) return;
	
	long name_size = strlen(name) + 1;

	m_name = static_cast<char*>(debug_menu_malloc(name_size));
	if (m_name)
	{
		strncpy(m_name, name, name_size);
		m_name[name_size - 1] = '\0';
	}
}

void c_debug_menu::set_caption(const char* caption)
{
	if (!caption) return;
	
	long caption_size = strlen(caption) + 1;

	m_caption = static_cast<char*>(debug_menu_malloc(caption_size));
	if (m_caption)
	{
		strncpy(m_caption, caption, caption_size);
		m_caption[caption_size - 1] = '\0';
	}
}

c_debug_menu* c_debug_menu::get_parent()
{
	return m_parent_ref;
}

bool c_debug_menu::get_enabled()
{
	return m_enabled;
}

void c_debug_menu::set_enabled(bool enable)
{
	m_enabled = enable;
}

short c_debug_menu::get_title_height()
{
	return static_cast<short>(debug_menu_get_title_height());
}

short c_debug_menu::get_item_indent()
{
	return static_cast<short>(debug_menu_get_item_indent_x());
}

short c_debug_menu::get_item_height()
{
	return static_cast<short>(debug_menu_get_item_height());
}

// c_debug_menu_scroll implementation

void c_debug_menu_scroll::update()
{
	c_debug_menu::update();

	if (get_selection() + 1 >= get_first() + get_num_visible())
	{
		short v3 = get_num_items();
		if (v3 <= get_num_visible())
			v3 = get_num_visible();

		if (get_first() + 1 >= v3 - get_num_visible())
		{
			short v5 = get_num_items();
			if (v5 <= get_num_visible())
				v5 = get_num_visible();

			set_first(v5 - get_num_visible());
		}
		else
			set_first(get_first() + 1);
	}

	if (get_selection() - 1 < get_first())
	{
		if (get_first() - 1 <= 0)
			set_first(0);
		else
			set_first(get_first() - 1);
	}
}

void c_debug_menu_scroll::render(c_font_cache_base* font_cache, const point2d& point)
{
	render_background(font_cache, point);
	render_title(font_cache, point);
	render_caption(font_cache, point);
	render_global_caption(font_cache, point);
	if (get_num_visible() > 0)
		render_items(font_cache, point, get_first(), get_first() + get_num_visible() - 1);

	if (get_first() > 0)
	{
		point2d arrow_pos;
		set_point2d(&arrow_pos, point.x, short(point.y + get_title_height()));
		draw_string_simple(font_cache, "^", arrow_pos, debug_real_argb_tv_magenta);

		set_point2d(&arrow_pos, point.x, short(point.y + get_title_height() + get_item_height()));
		draw_string_simple(font_cache, "|", arrow_pos, debug_real_argb_tv_magenta);
	}

	if (get_num_items() - get_first() > get_num_visible())
	{
		point2d arrow_pos;
		set_point2d(&arrow_pos, point.x, short((point.y + get_title_height()) + (get_num_visible() - 2) * get_item_height()));
		draw_string_simple(font_cache, "|", arrow_pos, debug_real_argb_tv_magenta);

		set_point2d(&arrow_pos, point.x, short((point.y + get_title_height()) + (get_num_visible() - 1) * get_item_height()));
		draw_string_simple(font_cache, "v", arrow_pos, debug_real_argb_tv_magenta);
	}
}

void c_debug_menu_scroll::open()
{
	c_debug_menu::open();
	set_first(0);
}

long c_debug_menu_scroll::get_num_items_to_render()
{
	if (get_num_items() <= get_num_visible())
		return get_num_items();

	return get_num_visible();
}

c_debug_menu_scroll::c_debug_menu_scroll(c_debug_menu* parent, short num_visible, const char* name) :
	c_debug_menu(parent, name),
	m_num_visible(num_visible),
	m_first(0)
{
}

short c_debug_menu_scroll::get_num_visible()
{
	return m_num_visible;
}

short c_debug_menu_scroll::get_first()
{
	return m_first;
}

void c_debug_menu_scroll::set_num_visible(short num_visible)
{
	m_num_visible = num_visible;
}

void c_debug_menu_scroll::set_first(short first)
{
	m_first = first;
}
