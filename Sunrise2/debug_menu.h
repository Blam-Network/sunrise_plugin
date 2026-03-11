#pragma once

#include "stdafx.h"

// Forward declarations
class c_font_cache_base;
struct point2d;

class c_debug_menu_item;
class c_debug_menu
{
	enum
	{
		// Halo 3
		k_max_items = 64

		// Reach
		//k_max_items = 128
	};

public:
	virtual ~c_debug_menu();
	virtual void update();
	virtual void render(c_font_cache_base* font_cache, const point2d& point);
	virtual void game_render();
	virtual void notify_selected(SHORT selected_value);
	virtual void open();
	virtual const char* get_caption();
	virtual void notify_closed();
	virtual void notify_activated();

protected:
	virtual DWORD get_num_items_to_render();
	virtual void close(BOOL closed); // closed ? m_parent->notify_selection_closed() : m_parent->notify_selection_exited()
	virtual SHORT get_value_width();
	virtual void notify_selection_closed();
	virtual void notify_selection_exited();
	virtual void notify_up();
	virtual void notify_down();
	virtual void notify_left();
	virtual void notify_right();

	BOOL is_active_menu();
	SHORT get_menu_rate();
	SHORT get_max_active_captions();

	void render_background(c_font_cache_base* font_cache, const point2d& point);
	void render_title(c_font_cache_base* font_cache, const point2d& point);
	void render_caption(c_font_cache_base* font_cache, const point2d& point);
	void render_global_caption(c_font_cache_base* font_cache, const point2d& point);
	void render_items(c_font_cache_base* font_cache, const point2d& point, SHORT start_index, SHORT end_index);

private:
	void try_left();
	void try_right();

public:
	c_debug_menu(c_debug_menu* parent, const char* name);

	void clear();

	SHORT get_num_items();
	void set_num_items(SHORT num_items);

	SHORT get_selection();
	void set_selection(SHORT selection);

	c_debug_menu_item* get_item(SHORT item_index);
	void add_item(c_debug_menu_item* item);

	const char* get_name();
	void set_name(const char* name);

	void set_caption(const char* caption);

	c_debug_menu* get_parent();

	BOOL get_enabled();
	void set_enabled(BOOL enable);

	SHORT get_title_height();
	SHORT get_item_indent();
	SHORT get_item_height();

protected:
	SHORT m_num_items;
	SHORT m_selection;
	DWORD m_last_up;
	DWORD m_last_down;
	DWORD m_last_left;
	DWORD m_last_right;
	c_debug_menu_item* m_items[k_max_items];
	char* m_name;
	char* m_caption;
	c_debug_menu* m_parent_ref;
	BOOL m_enabled;
};
