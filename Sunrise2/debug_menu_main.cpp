#include "stdafx.h"
#include "debug_menu_main.h"
#include "debug_menu.h"
#include "debug_menu_parse.h"
#include "debug_menu_scroll.h"

// Xbox 360: Custom assert macro
#define ASSERT(x) if(!(x)) { __debugbreak(); }
#define COMPILE_ASSERT(x) static_assert(x, #x)

// Xbox 360: Main menu class
class c_main_menu :
	public c_debug_menu_scroll
{
public:
	c_main_menu() :
		c_debug_menu_scroll(NULL, 26, "Halo 3 TU2 Debug Menu")
	{
	}
};

// Xbox 360: Global debug menu state
struct s_debug_menu_globals
{
	enum
	{
		k_str_length = 127
	};

	c_debug_menu* m_main_menu;
	c_debug_menu* m_active_menu;
	char m_caption[DEBUG_MENU_NUM_GLOBAL_CAPTIONS][k_str_length + 1];
	BOOL m_do_render;
	gamepad_state m_current_gamepad;
	gamepad_state m_last_gamepad;
	DWORD open_menu_time;
};
COMPILE_ASSERT(sizeof(s_debug_menu_globals) == 0x488);

// Xbox 360: Color constants
real_argb_color const instance_debug_real_argb_grey       = { 1.0f, { 0.3f, 0.42f, 0.33f } };
real_argb_color const instance_debug_real_argb_white      = { 1.0f, { 0.9f, 0.9f,  0.8f  } };
real_argb_color const instance_debug_real_argb_tv_white   = { 1.0f, { 0.8f, 0.8f,  0.75f } };
real_argb_color const instance_debug_real_argb_tv_blue    = { 1.0f, { 0.2f, 0.2f,  0.45f } };
real_argb_color const instance_debug_real_argb_tv_magenta = { 1.0f, { 0.7f, 0.05f, 0.7f  } };
real_argb_color const instance_debug_real_argb_tv_orange  = { 1.0f, { 1.0f, 0.5f,  0.0f  } };
real_argb_color const instance_debug_real_argb_tv_green   = { 1.0f, { 0.05f, 0.65f, 0.05f } };
real_argb_color const instance_global_real_argb_black     = { 1.0f, { 0.0f, 0.0f,  0.0f  } };

const real_argb_color* const debug_real_argb_grey       = &instance_debug_real_argb_grey;
const real_argb_color* const debug_real_argb_white      = &instance_debug_real_argb_white;
const real_argb_color* const debug_real_argb_tv_white   = &instance_debug_real_argb_tv_white;
const real_argb_color* const debug_real_argb_tv_blue    = &instance_debug_real_argb_tv_blue;
const real_argb_color* const debug_real_argb_tv_magenta = &instance_debug_real_argb_tv_magenta;
const real_argb_color* const debug_real_argb_tv_orange  = &instance_debug_real_argb_tv_orange;
const real_argb_color* const debug_real_argb_tv_green   = &instance_debug_real_argb_tv_green;
const real_argb_color* const global_real_argb_black     = &instance_global_real_argb_black;

BOOL debug_menu_enabled = TRUE;
s_debug_menu_globals g_debug_menu_globals = {0};

BOOL g_debug_menu_rebuild_request = FALSE;

c_static_stack<DWORD, k_debug_menu_stack_size> g_debug_menu_stack;

// Xbox 360: Function pointers (to be set by hooks)
rasterizer_quad_screenspace_t rasterizer_quad_screenspace = NULL;
draw_string_get_glyph_scaling_for_display_settings_t draw_string_get_glyph_scaling_for_display_settings = NULL;
system_milliseconds_t system_milliseconds = NULL;
interface_get_current_display_settings_t interface_get_current_display_settings = NULL;
real_argb_color_to_pixel32_t real_argb_color_to_pixel32 = NULL;

// Xbox 360: Helper functions
void set_point2d(point2d* point, SHORT x, SHORT y)
{
	point->x = x;
	point->y = y;
}

void set_rectangle2d(rectangle2d* rect, SHORT x0, SHORT y0, SHORT x1, SHORT y1)
{
	rect->x0 = x0;
	rect->y0 = y0;
	rect->x1 = x1;
	rect->y1 = y1;
}

SHORT rectangle2d_width(const rectangle2d* rect)
{
	return rect->x1 - rect->x0;
}

SHORT rectangle2d_height(const rectangle2d* rect)
{
	return rect->y1 - rect->y0;
}

// c_rasterizer_draw_string implementation
void c_rasterizer_draw_string::set_bounds(const rectangle2d* bounds)
{
	m_bounds = *bounds;
}

void c_rasterizer_draw_string::set_color(const real_argb_color* color)
{
	m_color = color;
}

void c_rasterizer_draw_string::draw(c_font_cache_base* font_cache, const char* text)
{
	// Xbox 360: This would call the game's draw string function
	// For now, stubbed - will be implemented via hooks
}

void debug_menu_draw_rect(SHORT x0, SHORT y0, SHORT x1, SHORT y1, FLOAT alpha, const real_argb_color* color)
{
	if (!rasterizer_quad_screenspace || !real_argb_color_to_pixel32)
		return;
	
	point2d points[4];

	set_point2d(&points[0], x0, y1);
	set_point2d(&points[1], x1, y1);
	set_point2d(&points[2], x1, y0);
	set_point2d(&points[3], x0, y0);

	rasterizer_quad_screenspace(points, real_argb_color_to_pixel32(color), NULL, 0, FALSE);
}

BOOL debug_menu_get_active()
{
	return g_debug_menu_globals.m_active_menu != NULL;
}

c_debug_menu* debug_menu_get_root()
{
	return g_debug_menu_globals.m_main_menu;
}

void debug_menu_initialize()
{
}

void debug_menu_dispose()
{
}

void debug_menu_initialize_for_new_map()
{
	g_debug_menu_globals.m_do_render = FALSE;
	g_debug_menu_globals.m_main_menu = NULL;
	g_debug_menu_globals.m_active_menu = NULL;

	g_debug_menu_globals.m_main_menu = DEBUG_MENU_MALLOC(c_main_menu);

	memset(&g_debug_menu_globals.m_last_gamepad, 0, sizeof(g_debug_menu_globals.m_last_gamepad));
	memset(&g_debug_menu_globals.m_current_gamepad, 0, sizeof(g_debug_menu_globals.m_current_gamepad));

	debug_menu_parse(debug_menu_get_root(), "debug_menu_init.txt");
	debug_menu_parse(debug_menu_get_root(), "debug_menu_user_init.txt");
}

void debug_menu_dispose_from_old_map()
{
	if (g_debug_menu_globals.m_main_menu)
	{
		g_debug_menu_stack.resize(0);
		g_debug_menu_globals.m_main_menu = NULL;
	}
	g_debug_menu_globals.m_active_menu = NULL;
}

void debug_menu_rebuild()
{
	g_debug_menu_rebuild_request = TRUE;
}

void debug_menu_update_current_gamepad_state()
{
	// Xbox 360: This should read XInput state and convert to our gamepad_state format
	// For now, stubbed - will be implemented via hooks
	memset(&g_debug_menu_globals.m_current_gamepad, 0, sizeof(g_debug_menu_globals.m_current_gamepad));
}

void debug_menu_update()
{
	debug_menu_update_current_gamepad_state();

	const gamepad_state& state = debug_menu_get_gamepad_state();
	
	if (state.button_frames[_gamepad_binary_button_left_thumb])
	{
		g_debug_menu_globals.m_do_render = FALSE;
	}
	else
	{
		BOOL should_toggle = FALSE;
		
		// Xbox 360: Check for menu toggle (Back + Start)
		should_toggle = state.button_frames[_gamepad_binary_button_back] == 1 && state.button_frames[_gamepad_binary_button_start] ||
		                state.button_frames[_gamepad_binary_button_back] && state.button_frames[_gamepad_binary_button_start] == 1;

		if (!debug_menu_enabled)
			should_toggle = FALSE;

		if (should_toggle)
		{
			if (debug_menu_get_active())
				debug_menu_close();
			else
				debug_menu_open();
		}

		if (debug_menu_get_active())
		{
			if (g_debug_menu_rebuild_request)
			{
				debug_menu_close();
				debug_menu_dispose_from_old_map();
				debug_menu_initialize_for_new_map();
				debug_menu_open();

				g_debug_menu_rebuild_request = FALSE;
			}

			debug_menu_get_active_menu()->update();
		}

		g_debug_menu_globals.m_do_render = TRUE;
	}

	g_debug_menu_globals.m_last_gamepad = g_debug_menu_globals.m_current_gamepad;
}

void debug_menu_open()
{
	if (debug_menu_get_root() && !debug_menu_get_active())
	{
		debug_menu_set_active_menu(debug_menu_get_root(), FALSE);
		if (system_milliseconds)
			g_debug_menu_globals.open_menu_time = system_milliseconds();
	}
}

void debug_menu_close()
{
	if (debug_menu_get_root() && debug_menu_get_active())
	{
		if (system_milliseconds)
		{
			if (system_milliseconds() - g_debug_menu_globals.open_menu_time > 100)
			{
				debug_menu_set_active_menu(NULL, FALSE);
			}
		}
		else
		{
			debug_menu_set_active_menu(NULL, FALSE);
		}
	}
}

void render_debug_debug_menu_game()
{
	if (debug_menu_get_active())
	{
		debug_menu_get_active_menu()->game_render();
	}
}

void render_debug_debug_menu()
{
	if (debug_menu_get_active() && g_debug_menu_globals.m_do_render)
	{
		c_font_cache_base font_cache;

		point2d position;
		set_point2d(&position, 180, 60);
		debug_menu_get_active_menu()->render(&font_cache, position);
	}
}

const gamepad_state& debug_menu_get_gamepad_state()
{
	return g_debug_menu_globals.m_current_gamepad;
}

const gamepad_state& debug_menu_get_last_gamepad_state()
{
	return g_debug_menu_globals.m_last_gamepad;
}

FLOAT debug_menu_get_item_margin()
{
	if (draw_string_get_glyph_scaling_for_display_settings)
		return draw_string_get_glyph_scaling_for_display_settings() * 10.0f;
	return 10.0f;
}

FLOAT debug_menu_get_item_width()
{
	if (draw_string_get_glyph_scaling_for_display_settings)
		return draw_string_get_glyph_scaling_for_display_settings() * 650.0f;
	return 650.0f;
}

FLOAT debug_menu_get_item_height()
{
	if (draw_string_get_glyph_scaling_for_display_settings)
		return draw_string_get_glyph_scaling_for_display_settings() * 20.0f;
	return 20.0f;
}

FLOAT debug_menu_get_title_height()
{
	if (draw_string_get_glyph_scaling_for_display_settings)
		return draw_string_get_glyph_scaling_for_display_settings() * 20.0f;
	return 20.0f;
}

FLOAT debug_menu_get_item_indent_x()
{
	if (draw_string_get_glyph_scaling_for_display_settings)
		return draw_string_get_glyph_scaling_for_display_settings() * 40.0f;
	return 40.0f;
}

FLOAT debug_menu_get_item_indent_y()
{
	if (draw_string_get_glyph_scaling_for_display_settings)
		return draw_string_get_glyph_scaling_for_display_settings() * 2.0f;
	return 2.0f;
}

c_debug_menu* debug_menu_get_active_menu()
{
	return g_debug_menu_globals.m_active_menu;
}

void debug_menu_set_active_menu(c_debug_menu* active_menu, BOOL dont_open)
{
	c_debug_menu* current_active_menu = g_debug_menu_globals.m_active_menu;

	BOOL menu_is_parent = FALSE;
	if (g_debug_menu_globals.m_active_menu && active_menu)
	{
		if (c_debug_menu* parent = g_debug_menu_globals.m_active_menu->get_parent())
		{
			if (parent == active_menu)
				menu_is_parent = TRUE;
		}
	}
	g_debug_menu_globals.m_active_menu = active_menu;

	if (active_menu)
	{
		if (!menu_is_parent && !dont_open)
		{
			active_menu->open();
			active_menu = g_debug_menu_globals.m_active_menu;
		}
		active_menu->notify_activated();
	}
	else if (current_active_menu)
	{
		current_active_menu->notify_closed();
	}

	for (SHORT caption_index = 0; caption_index < DEBUG_MENU_NUM_GLOBAL_CAPTIONS; caption_index++)
	{
		debug_menu_set_caption(caption_index, "");
	}
}

void debug_menu_set_caption(SHORT caption_index, const char* caption)
{
	ASSERT(caption_index >= 0 && caption_index < DEBUG_MENU_NUM_GLOBAL_CAPTIONS);

	strncpy_s(g_debug_menu_globals.m_caption[caption_index], sizeof(*g_debug_menu_globals.m_caption), caption, _TRUNCATE);
}

const char* debug_menu_get_caption(SHORT caption_index)
{
	ASSERT(caption_index >= 0 && caption_index < DEBUG_MENU_NUM_GLOBAL_CAPTIONS);

	return g_debug_menu_globals.m_caption[caption_index];
}

DWORD debug_menu_get_time()
{
	if (system_milliseconds)
		return (DWORD)(((system_milliseconds() * 30.0f) / 1000.0f) + 0.5f);
	return 0;
}

void* debug_menu_malloc(DWORD size)
{
	DWORD size_in_count = (size + 3) >> 2;
	g_debug_menu_stack.resize(g_debug_menu_stack.count() + size_in_count);
	void* result = g_debug_menu_stack.get(g_debug_menu_stack.count() - size_in_count);

	return result;
}

void xor_buffers(void* dest, const void* source, DWORD count)
{
	for (DWORD i = 0; i < count; i++)
	{
		((BYTE*)dest)[i] ^= ((const BYTE*)source)[i];
	}
}
