#pragma once
#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H

#include "stdafx.h"

// Forward declarations
class c_font_cache_base;
union point2d;
union real_argb_color;
class c_debug_menu_item;

// Constants
enum
{
	k_max_items = 64,
	k_debug_menu_stack_size = 262144
};

#define DEBUG_MENU_NUM_GLOBAL_CAPTIONS 8
#define DEBUG_MENU_MALLOC(CLASS, ...) new (debug_menu_malloc(sizeof(CLASS))) CLASS(__VA_ARGS__)

// Color type
union real_argb_color
{
	struct
	{
		float alpha;
		float red;
		float green;
		float blue;
	};
	float components[4];
};

// Point type
union point2d
{
	struct
	{
		short x;
		short y;
	};
	short n[2];
};

// Rectangle type
struct rectangle2d
{
	short x0;
	short y0;
	short x1;
	short y1;
};

// Gamepad state structure
struct gamepad_state
{
	byte button_frames[16];
	byte analog_buttons[2];
	byte analog_button_thresholds[2];
	long button_msec[16];
	point2d sticks[2];
};

// Font cache base class (stub for Xbox 360)
class c_font_cache_base
{
public:
	c_font_cache_base() {}
	virtual ~c_font_cache_base() {}
};

// MT-safe font cache
class c_font_cache_mt_safe : public c_font_cache_base
{
public:
	c_font_cache_mt_safe() : c_font_cache_base() {}
	virtual ~c_font_cache_mt_safe() {}
};

// Debug menu base class
class c_debug_menu
{
public:
	virtual ~c_debug_menu();
	virtual void update();
	virtual void render(c_font_cache_base* font_cache, const point2d& point);
	virtual void game_render();
	virtual void notify_selected(short selected_value);
	virtual void open();
	virtual const char* get_caption();
	virtual void notify_closed();
	virtual void notify_activated();

protected:
	virtual long get_num_items_to_render();
	virtual void close(bool closed);
	virtual short get_value_width();
	virtual void notify_selection_closed();
	virtual void notify_selection_exited();
	virtual void notify_up();
	virtual void notify_down();
	virtual void notify_left();
	virtual void notify_right();

	bool is_active_menu();
	short get_menu_rate();
	short get_max_active_captions();

	void render_background(c_font_cache_base* font_cache, const point2d& point);
	void render_title(c_font_cache_base* font_cache, const point2d& point);
	void render_caption(c_font_cache_base* font_cache, const point2d& point);
	void render_global_caption(c_font_cache_base* font_cache, const point2d& point);
	void render_items(c_font_cache_base* font_cache, const point2d& point, short start_index, short end_index);

private:
	void try_left();
	void try_right();

public:
	c_debug_menu(c_debug_menu* parent, const char* name);

	void clear();

	short get_num_items();
	void set_num_items(short num_items);

	short get_selection();
	void set_selection(short selection);

	c_debug_menu_item* get_item(short item_index);
	void add_item(c_debug_menu_item* item);

	const char* get_name();
	void set_name(const char* name);

	void set_caption(const char* caption);

	c_debug_menu* get_parent();

	bool get_enabled();
	void set_enabled(bool enable);

	short get_title_height();
	short get_item_indent();
	short get_item_height();

protected:
	short m_num_items;
	short m_selection;
	long m_last_up;
	long m_last_down;
	long m_last_left;
	long m_last_right;
	c_debug_menu_item* m_items[k_max_items];
	char* m_name;
	char* m_caption;
	c_debug_menu* m_parent_ref;
	bool m_enabled;
};

// Scrollable debug menu (main menu type)
class c_debug_menu_scroll : public c_debug_menu
{
public:
	virtual ~c_debug_menu_scroll() {}
	virtual void update() override;
	virtual void render(c_font_cache_base* font_cache, const point2d& point) override;
	virtual void open() override;

protected:
	virtual long get_num_items_to_render() override;

public:
	c_debug_menu_scroll(c_debug_menu* parent, short num_visible, const char* name);

protected:
	short get_num_visible();
	short get_first();

private:
	void set_num_visible(short num_visible);
	void set_first(short first);

protected:
	short m_num_visible;
	short m_first;
};

// Main menu class
class c_main_menu : public c_debug_menu_scroll
{
public:
	c_main_menu() : c_debug_menu_scroll(nullptr, 26, "Main Menu") {}
};

// Global state
struct s_debug_menu_globals
{
	enum { k_str_length = 127 };

	c_debug_menu* m_main_menu;
	c_debug_menu* m_active_menu;
	char m_caption[DEBUG_MENU_NUM_GLOBAL_CAPTIONS][k_str_length + 1];
	bool m_do_render;
	gamepad_state m_current_gamepad;
	gamepad_state m_last_gamepad;
	long open_menu_time;
};

// External color constants
extern const real_argb_color* const debug_real_argb_grey;
extern const real_argb_color* const debug_real_argb_white;
extern const real_argb_color* const debug_real_argb_tv_white;
extern const real_argb_color* const debug_real_argb_tv_blue;
extern const real_argb_color* const debug_real_argb_tv_magenta;
extern const real_argb_color* const debug_real_argb_tv_orange;
extern const real_argb_color* const debug_real_argb_tv_green;
extern const real_argb_color* const global_real_argb_black;

// Global functions
extern void debug_menu_draw_rect(short x0, short y0, short x1, short y1, float alpha, const real_argb_color* color);
extern bool debug_menu_get_active();
extern void debug_menu_initialize();
extern void debug_menu_dispose();
extern void debug_menu_initialize_for_new_map();
extern void debug_menu_dispose_from_old_map();
extern void debug_menu_update();
extern void debug_menu_open();
extern void debug_menu_close();
extern void render_debug_debug_menu();
extern const gamepad_state& debug_menu_get_gamepad_state();
extern const gamepad_state& debug_menu_get_last_gamepad_state();
extern c_debug_menu* debug_menu_get_active_menu();
extern void debug_menu_set_active_menu(c_debug_menu* active_menu, bool dont_open);
extern void debug_menu_set_caption(short caption_index, const char* caption);
extern const char* debug_menu_get_caption(short caption_index);
extern long debug_menu_get_time();
extern float debug_menu_get_item_margin();
extern float debug_menu_get_item_width();
extern float debug_menu_get_item_height();
extern float debug_menu_get_title_height();
extern float debug_menu_get_item_indent_x();
extern float debug_menu_get_item_indent_y();
extern void* debug_menu_malloc(long size);
extern c_debug_menu* debug_menu_get_root();

// Global instance
extern s_debug_menu_globals g_debug_menu_globals;

// Helper inline functions
inline void set_point2d(point2d* point, short x, short y)
{
	point->x = x;
	point->y = y;
}

inline void set_rectangle2d(rectangle2d* rect, short x0, short y0, short x1, short y1)
{
	rect->x0 = x0;
	rect->y0 = y0;
	rect->x1 = x1;
	rect->y1 = y1;
}

inline short rectangle2d_width(const rectangle2d* rect)
{
	return rect->x1 - rect->x0;
}

inline short rectangle2d_height(const rectangle2d* rect)
{
	return rect->y1 - rect->y0;
}

#endif // DEBUG_MENU_H
