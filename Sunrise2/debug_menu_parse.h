#pragma once

#include "stdafx.h"

// Xbox 360: Custom macros
#define COMPILE_ASSERT(x) static_assert(x, #x)

enum e_parse_state
{
	_parse_state_none = 0,
	_parse_state_reading_tag,
	_parse_state_reading_open_tag,
	_parse_state_reading_close_tag,
	_parse_state_reading_property,
	_parse_state_reading_property_found_eqauls,
	_parse_state_reading_menu_items,
	_parse_state_reading_string,
	_parse_state_reading_escape_character,
	_parse_state_reading_number,

	k_parse_state_count
};

template<typename T, DWORD max_count>
class c_parse_stack
{
public:
	c_parse_stack() : m_count(0) {}
	
	void push_back(T value) { if (m_count < max_count) m_data[m_count++] = value; }
	void pop() { if (m_count > 0) m_count--; }
	T* get_top() { return m_count > 0 ? &m_data[m_count - 1] : NULL; }
	DWORD count() const { return m_count; }
	
private:
	T m_data[max_count];
	DWORD m_count;
};

typedef c_parse_stack<e_parse_state, k_parse_state_count> parse_stack_t;

enum e_advance_type
{
	_advance_type_process_token = 0,
	_advance_type_process_distance,
	_advance_type_process_nothing,

	k_advance_type_count
};

enum e_symbols
{
	_symbol_none = 0x0,
	_symbol_less_than = 0x3C,
	_symbol_greater_than = 0x3E,
	_symbol_back_slash = 0x2F,
	_symbol_forward_slash = 0x5C,
	_symbol_white_space = 0x20,
	_symbol_equals = 0x3D,
	_symbol_quote = 0x22,
	_symbol_tab = 0x9,
	_symbol_random_whitespace = 0xFFFFFFCD,
	_symbol_period = 0x2E,
	_symbol_minus = 0x2D,
};

enum e_tokens
{
	_token_none = 0,
	
	_token_min,
	_token_max,
	_token_inc,

	_token_menu,
	_token_zone_set_menu,

	_token_item,

	_token_caption,
	_token_name,
	_token_variable,
	_token_color,

	_token_type,
	_token_global,
	_token_command,

	_token_eol_0,
	_token_eol_1,
	_token_eol_2,
	_token_eol_3,

	k_token_count
};

enum e_property
{
	_property_none = 0,
	_property_type,
	_property_color,
	_property_caption,
	_property_name,
	_property_variable,
	_property_min,
	_property_max,
	_property_inc,

	k_property_count
};

enum e_property_owners
{
	_property_owner_none = 0,
	_property_owner_item,
	_property_owner_menu,
	_property_owner_zone_set_menu,

	k_property_owner_count
};

enum e_item_types
{
	_item_type_none = 0,
	_item_type_hs_variable_global,
	_item_type_command,

	k_item_type_count
};

// Xbox 360: Simple enum wrapper for type safety
template<typename T, typename StorageT, T min_val, T max_val>
class c_enum
{
public:
	c_enum() : m_value((StorageT)min_val) {}
	c_enum(T value) : m_value((StorageT)value) {}
	
	T get() const { return (T)m_value; }
	void set(T value) { m_value = (StorageT)value; }
	
	operator T() const { return get(); }
	c_enum& operator=(T value) { set(value); return *this; }
	
private:
	StorageT m_value;
};

struct s_parser_state
{
	void reset();

	enum
	{
		k_string_length = 2048
	};

	c_enum<e_property, SHORT, _property_none, k_property_count> m_current_property_type;
	c_enum<e_property_owners, DWORD, _property_owner_none, k_property_owner_count> m_current_property_owner;

	char m_string_buffer[k_string_length];
	SHORT m_string_buffer_index;

	char m_number_buffer[k_string_length];
	SHORT m_number_buffer_index;

	BOOL m_has_name;
	char m_name[k_string_length];

	BOOL m_has_color;
	char m_color[k_string_length];

	BOOL m_has_variable;
	char m_variable[k_string_length];

	BOOL m_has_caption;
	char m_caption[k_string_length];

	BOOL m_has_min;
	FLOAT m_min;

	BOOL m_has_max;
	FLOAT m_max;

	BOOL m_has_inc;
	FLOAT m_inc;

	BOOL m_has_item_type;
	c_enum<e_item_types, DWORD, _item_type_none, k_item_type_count> m_item_type;
};
COMPILE_ASSERT(sizeof(s_parser_state) == 0x3030);

extern const char* const g_token_names[k_token_count];

extern s_parser_state g_parser_state;

class c_debug_menu;
extern void debug_menu_parse(c_debug_menu* root_menu, const char* file_name);
