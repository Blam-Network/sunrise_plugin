#pragma once

#include "stdafx.h"
#include "debug_menu_item_numbered.h"
#include "debug_menu_value_hs_global_external.h"

class c_debug_menu_item_type :
	public c_debug_menu_item_numbered
{
public:
	virtual ~c_debug_menu_item_type() {}
	virtual void render(c_font_cache_base* font_cache, const point2d& position);
	virtual void to_string(char* buffer, DWORD buffer_size);
	virtual void render_value(c_font_cache_base* font_cache, const point2d& position);

	c_debug_menu_item_type(c_debug_menu* menu, const char* name, BOOL readonly);

	BOOL get_readonly();

protected:
	BOOL m_readonly;
};

class c_debug_menu_item_type_bool :
	public c_debug_menu_item_type
{
public:
	virtual ~c_debug_menu_item_type_bool() {}
	virtual void notify_left();
	virtual void notify_right();

protected:
	virtual void to_string(char* buffer, DWORD buffer_size);

public:
	c_debug_menu_item_type_bool(c_debug_menu* menu, const char* name, BOOL readonly, const char* variable);

protected:
	c_debug_menu_value_hs_global_external<BOOL> m_value;
};

class c_debug_menu_item_type_real :
	public c_debug_menu_item_type
{
public:
	virtual ~c_debug_menu_item_type_real() {}
	virtual void notify_left();
	virtual void notify_right();

protected:
	virtual void to_string(char* buffer, DWORD buffer_size);

public:
	c_debug_menu_item_type_real(c_debug_menu* menu, const char* name, BOOL readonly, const char* variable, FLOAT min, FLOAT max, FLOAT inc);

protected:
	c_debug_menu_value_hs_global_external<FLOAT> m_value;
	FLOAT m_min;
	FLOAT m_max;
	FLOAT m_inc;
};

class c_debug_menu_item_type_short :
	public c_debug_menu_item_type
{
public:
	virtual ~c_debug_menu_item_type_short() {}
	virtual void notify_left();
	virtual void notify_right();

protected:
	virtual void to_string(char* buffer, DWORD buffer_size);

public:
	c_debug_menu_item_type_short(c_debug_menu* menu, const char* name, BOOL readonly, const char* variable, SHORT min, SHORT max, SHORT inc);

protected:
	c_debug_menu_value_hs_global_external<SHORT> m_value;
	SHORT m_min;
	SHORT m_max;
	SHORT m_inc;
};

class c_debug_menu_item_type_long :
	public c_debug_menu_item_type
{
public:
	virtual ~c_debug_menu_item_type_long() {}
	virtual void notify_left();
	virtual void notify_right();

protected:
	virtual void to_string(char* buffer, DWORD buffer_size);

public:
	c_debug_menu_item_type_long(c_debug_menu* menu, const char* name, BOOL readonly, const char* variable, DWORD min, DWORD max, DWORD inc);

protected:
	c_debug_menu_value_hs_global_external<DWORD> m_value;
	DWORD m_min;
	DWORD m_max;
	DWORD m_inc;
};
