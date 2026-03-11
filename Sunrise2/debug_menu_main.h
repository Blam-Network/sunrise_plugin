#pragma once

#include "stdafx.h"

#include <new>
#define DEBUG_MENU_MALLOC(CLASS, ...) new (debug_menu_malloc(sizeof(CLASS))) CLASS(__VA_ARGS__)

enum
{
	k_debug_menu_stack_size = 262144
};

#define DEBUG_MENU_NUM_GLOBAL_CAPTIONS 8

// Forward declarations
struct real_argb_color;
struct gamepad_state;
class c_debug_menu;

// Xbox 360: Color constants
extern const real_argb_color* const debug_real_argb_grey;
extern const real_argb_color* const debug_real_argb_white;
extern const real_argb_color* const debug_real_argb_tv_white;
extern const real_argb_color* const debug_real_argb_tv_blue;
extern const real_argb_color* const debug_real_argb_tv_magenta;
extern const real_argb_color* const debug_real_argb_tv_orange;
extern const real_argb_color* const debug_real_argb_tv_green;
extern const real_argb_color* const global_real_argb_black;

// Xbox 360: Global state
extern BOOL debug_menu_enabled;
extern BOOL g_debug_menu_rebuild_request;

// Xbox 360: Stack for menu memory allocation
template<typename T, DWORD max_count>
class c_static_stack
{
public:
	c_static_stack() : m_count(0) {}
	
	void push_back(T value) { if (m_count < max_count) m_data[m_count++] = value; }
	void pop() { if (m_count > 0) m_count--; }
	T* get_top() { return m_count > 0 ? &m_data[m_count - 1] : NULL; }
	T* get(DWORD index) { return index < m_count ? &m_data[index] : NULL; }
	DWORD count() const { return m_count; }
	void resize(DWORD new_count) { if (new_count <= max_count) m_count = new_count; }
	
private:
	T m_data[max_count];
	DWORD m_count;
};

extern c_static_stack<DWORD, k_debug_menu_stack_size> g_debug_menu_stack;

// Xbox 360: Gamepad button enums
enum e_gamepad_binary_button
{
	_gamepad_binary_button_dpad_up = 0,
	_gamepad_binary_button_dpad_down,
	_gamepad_binary_button_dpad_left,
	_gamepad_binary_button_dpad_right,
	_gamepad_binary_button_start,
	_gamepad_binary_button_back,
	_gamepad_binary_button_left_thumb,
	_gamepad_binary_button_right_thumb,
	_gamepad_binary_button_left_shoulder,
	_gamepad_binary_button_right_shoulder,
	_gamepad_binary_button_a,
	_gamepad_binary_button_b,
	_gamepad_binary_button_x,
	_gamepad_binary_button_y,
	_gamepad_binary_button_0,
	_gamepad_binary_button_1,
	_gamepad_binary_button_2,
	_gamepad_binary_button_3,
	_gamepad_binary_button_4,
	_gamepad_binary_button_5,
	_gamepad_binary_button_6,
	_gamepad_binary_button_7,
	_gamepad_binary_button_8,
	_gamepad_binary_button_9,
	
	k_gamepad_binary_button_count
};

// Xbox 360: Gamepad state structure
struct gamepad_state
{
	BYTE button_frames[k_gamepad_binary_button_count];
	SHORT sticks[4];
	BYTE analog_buttons[2];
	BYTE analog_button_thresholds[2];
};

// Xbox 360: Color structure
struct real_rgb_color
{
	FLOAT n[3];
};

struct real_argb_color
{
	FLOAT alpha;
	real_rgb_color rgb;
};

// Xbox 360: Point and rectangle structures
struct point2d
{
	SHORT x;
	SHORT y;
};

struct rectangle2d
{
	SHORT x0;
	SHORT y0;
	SHORT x1;
	SHORT y1;
};

// Xbox 360: Drawing structures
class c_font_cache_base
{
public:
	// Base class for font rendering - stubbed for Xbox 360
	virtual ~c_font_cache_base() {}
};

class c_rasterizer_draw_string
{
public:
	void initialize() { memset(this, 0, sizeof(*this)); }
	void set_bounds(const rectangle2d* bounds);
	void set_color(const real_argb_color* color);
	void draw(c_font_cache_base* font_cache, const char* text);
	
private:
	rectangle2d m_bounds;
	const real_argb_color* m_color;
};

// Xbox 360: Function pointer types for game functions
typedef void (*rasterizer_quad_screenspace_t)(const point2d* points, DWORD color, void* unknown1, DWORD unknown2, BOOL unknown3);
typedef FLOAT (*draw_string_get_glyph_scaling_for_display_settings_t)();
typedef DWORD (*system_milliseconds_t)();
typedef void (*interface_get_current_display_settings_t)(void* unknown1, void* unknown2, void* unknown3, rectangle2d* bounds);
typedef DWORD (*real_argb_color_to_pixel32_t)(const real_argb_color* color);

// Xbox 360: Function pointers (will be set by hooks)
extern rasterizer_quad_screenspace_t rasterizer_quad_screenspace;
extern draw_string_get_glyph_scaling_for_display_settings_t draw_string_get_glyph_scaling_for_display_settings;
extern system_milliseconds_t system_milliseconds;
extern interface_get_current_display_settings_t interface_get_current_display_settings;
extern real_argb_color_to_pixel32_t real_argb_color_to_pixel32;

// Xbox 360: Helper functions
extern void set_point2d(point2d* point, SHORT x, SHORT y);
extern void set_rectangle2d(rectangle2d* rect, SHORT x0, SHORT y0, SHORT x1, SHORT y1);
extern SHORT rectangle2d_width(const rectangle2d* rect);
extern SHORT rectangle2d_height(const rectangle2d* rect);

// Main debug menu functions
extern void debug_menu_draw_rect(SHORT x0, SHORT y0, SHORT x1, SHORT y1, FLOAT alpha, const real_argb_color* color);
extern BOOL debug_menu_get_active();
extern void debug_menu_initialize();
extern void debug_menu_dispose();
extern void debug_menu_initialize_for_new_map();
extern void debug_menu_dispose_from_old_map();
extern void debug_menu_rebuild();
extern void debug_menu_update();
extern void debug_menu_open();
extern void debug_menu_close();
extern void render_debug_debug_menu_game();
extern void render_debug_debug_menu();
extern const gamepad_state& debug_menu_get_gamepad_state();
extern const gamepad_state& debug_menu_get_last_gamepad_state();
extern c_debug_menu* debug_menu_get_active_menu();
extern void debug_menu_set_active_menu(c_debug_menu* active_menu, BOOL dont_open);
extern void debug_menu_set_caption(SHORT caption_index, const char* caption);
extern const char* debug_menu_get_caption(SHORT caption_index);
extern DWORD debug_menu_get_time();
extern FLOAT debug_menu_get_item_margin();
extern FLOAT debug_menu_get_item_width();
extern FLOAT debug_menu_get_item_height();
extern FLOAT debug_menu_get_title_height();
extern FLOAT debug_menu_get_item_indent_x();
extern FLOAT debug_menu_get_item_indent_y();
extern void* debug_menu_malloc(DWORD size);
extern void xor_buffers(void* dest, const void* source, DWORD count);
