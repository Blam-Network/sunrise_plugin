#include "stdafx.h"
#include "debug_menu_parse.h"
#include "debug_menu.h"
#include "debug_menu_item.h"
#include "debug_menu_item_hs_command.h"
#include "debug_menu_item_numbered.h"
#include "debug_menu_item_type.h"
#include "debug_menu_main.h"
#include "debug_menu_scroll.h"
#include "debug_menu_zone_sets.h"

#include <climits>
#include <ctype.h>
#include <math.h>
#include <string.h>

// Xbox 360: Custom macros
#define ASSERT(x) if(!(x)) { __debugbreak(); }
#define UNREACHABLE() __debugbreak()
#define IN_RANGE_INCLUSIVE(value, min, max) ((value) >= (min) && (value) <= (max))
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#define k_real_max FLT_MAX
#define k_real_min -FLT_MAX

#define PARSER_ASSERT(STATEMENT) PARSER_ASSERT_WITH_MESSAGE(STATEMENT, #STATEMENT)
#define PARSER_ASSERT_WITH_MESSAGE(STATEMENT, MESSAGE) \
if (!(STATEMENT)) \
{ \
	sprintf_s(error_buffer, error_buffer_length, "ln %d: %s", *line_count, MESSAGE); \
	parse_error = error_buffer; \
	debug_menu_display_error(error_buffer, TRUE); \
	continue; \
} else 

#define TOKEN_CASE_PROPERTY(PROPERTY) \
case _token_##PROPERTY: \
{ \
	g_parser_state.m_current_property_type = _property_##PROPERTY; \
	PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_open_tag) \
	{ \
		parse_stack.push_back(_parse_state_reading_property); \
	} \
} \
break
#define TOKEN_CASE_PROPERTY_OWNER(PROPERTY_OWNER, CHECK_FORWORD_SLASH) \
case _token_##PROPERTY_OWNER: \
{ \
	if (CHECK_FORWORD_SLASH && *parse_stack.get_top() == _parse_state_reading_close_tag) \
		break; \
	g_parser_state.m_current_property_owner = _property_owner_##PROPERTY_OWNER; \
	PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_tag) \
	{ \
		parse_stack.push_back(_parse_state_reading_open_tag); \
	} \
} \
break
#define TOKEN_CASE_TYPE(TYPE) \
case _token_##TYPE: \
{ \
	PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_reading_property_found_eqauls, "unexpected token \"global\"") \
	{ \
		g_parser_state.m_has_item_type = TRUE; \
		g_parser_state.m_item_type = (e_item_types)((token - (_token_type + 1)) + 1); \
		PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_property_found_eqauls) \
		{ \
			parse_stack.pop(); \
			PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_property) \
			{ \
				parse_stack.pop(); \
				PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_open_tag) \
				{ \
					break; \
				} \
			} \
		} \
	} \
} \
break
#define TOKEN_CASE_NEW_LINE(TOKEN) case _token_##TOKEN: ++*line_count; break

s_parser_state g_parser_state = {0};

void s_parser_state::reset()
{
	m_has_name = FALSE;
	m_has_color = FALSE;
	m_has_variable = FALSE;
	m_has_caption = FALSE;
	m_has_min = FALSE;
	m_has_max = FALSE;
	m_has_inc = FALSE;
	m_item_type = _item_type_none;
	m_current_property_type = _property_none;
	m_current_property_owner = _property_owner_none;
}

const char* const g_token_names[k_token_count]
{
	"none",
	"min",
	"max",
	"inc",
	"menu",
	"zone_set_menu",
	"item",
	"caption",
	"name",
	"variable",
	"color",
	"type",
	"global",
	"command",
	"\r\n",
	"\n\r",
	"\r",
	"\n",
};

DWORD debug_menu_memory_available()
{
	return 4 * (k_debug_menu_stack_size - g_debug_menu_stack.count());
}

void debug_menu_look_ahead_read_token(FILE* file, DWORD file_char, char* token_buffer, DWORD token_buffer_count)
{
	DWORD file_size = ftell(file);
	*token_buffer = (char)file_char;
	DWORD characters_read = (DWORD)fread(token_buffer + 1, sizeof(char), token_buffer_count - 1, file);
	fseek(file, file_size, 0);

	ASSERT(IN_RANGE_INCLUSIVE(characters_read, 0, token_buffer_count - 1));

	DWORD token_buffer_end = characters_read + 1;
	if (characters_read + 1 > token_buffer_count - 1)
	{
		token_buffer_end = token_buffer_count - 1;
	}
	token_buffer[token_buffer_end] = 0;
}

BOOL string_in_string_case_insensitive(const char* source, const char* find)
{
	ASSERT(source && find);

	BOOL result = FALSE;

	DWORD source_length = 0;
	while (source[source_length])
	{
		source_length += 1;
	}

	DWORD find_length = 0;
	while (find[find_length])
	{
		find_length += 1;
	}

	if (find_length == 0)
	{
		result = TRUE;
	}
	else
	{
		BOOL match = TRUE;
		DWORD i = 0;
		while (i < source_length && i < find_length)
		{
			char source_char = (char)tolower(source[i]);
			char find_char = (char)tolower(find[i]);
			if (source_char != find_char)
			{
				match = FALSE;
				break;
			}
			i += 1;
		}
		if (match && i == find_length)
		{
			result = TRUE;
		}
	}

	return result;
}

void debug_menu_store_number_property(parse_stack_t* parse_stack)
{
	ASSERT(*parse_stack->get_top() == _parse_state_reading_number);

	parse_stack->pop();
	ASSERT(*parse_stack->get_top() == _parse_state_reading_property_found_eqauls);

	parse_stack->pop();
	ASSERT(*parse_stack->get_top() == _parse_state_reading_property);

	parse_stack->pop();
	ASSERT(*parse_stack->get_top() == _parse_state_reading_open_tag);

	ASSERT(VALID_INDEX(g_parser_state.m_number_buffer_index, s_parser_state::k_string_length));

	g_parser_state.m_number_buffer[g_parser_state.m_number_buffer_index] = 0;
	switch (g_parser_state.m_current_property_type.get())
	{
	case _property_min:
	{
		g_parser_state.m_has_min = TRUE;
		g_parser_state.m_min = (FLOAT)atof(g_parser_state.m_number_buffer);
	}
	break;
	case _property_max:
	{
		g_parser_state.m_has_max = TRUE;
		g_parser_state.m_max = (FLOAT)atof(g_parser_state.m_number_buffer);
	}
	break;
	case _property_inc:
	{
		g_parser_state.m_has_inc = TRUE;
		g_parser_state.m_inc = (FLOAT)atof(g_parser_state.m_number_buffer);
	}
	break;
	}
	g_parser_state.m_current_property_type = _property_none;
}

void debug_menu_store_string_property(parse_stack_t* parse_stack)
{
	ASSERT(*parse_stack->get_top() == _parse_state_reading_string);

	parse_stack->pop();
	ASSERT(*parse_stack->get_top() == _parse_state_reading_property_found_eqauls);

	parse_stack->pop();
	ASSERT(*parse_stack->get_top() == _parse_state_reading_property);

	parse_stack->pop();
	ASSERT(*parse_stack->get_top() == _parse_state_reading_open_tag);

	ASSERT(VALID_INDEX(g_parser_state.m_string_buffer_index, s_parser_state::k_string_length));

	g_parser_state.m_string_buffer[g_parser_state.m_string_buffer_index] = 0;
	switch (g_parser_state.m_current_property_type.get())
	{
	case _property_color:
	{
		g_parser_state.m_has_color = TRUE;
		strncpy_s(g_parser_state.m_color, s_parser_state::k_string_length, g_parser_state.m_string_buffer, _TRUNCATE);
	}
	break;
	case _property_caption:
	{
		g_parser_state.m_has_caption = TRUE;
		strncpy_s(g_parser_state.m_caption, s_parser_state::k_string_length, g_parser_state.m_string_buffer, _TRUNCATE);
	}
	break;
	case _property_name:
	{
		g_parser_state.m_has_name = TRUE;
		strncpy_s(g_parser_state.m_name, s_parser_state::k_string_length, g_parser_state.m_string_buffer, _TRUNCATE);
	}
	break;
	case _property_variable:
	{
		g_parser_state.m_has_variable = TRUE;
		strncpy_s(g_parser_state.m_variable, s_parser_state::k_string_length, g_parser_state.m_string_buffer, _TRUNCATE);
	}
	break;
	}
	g_parser_state.m_current_property_type = _property_none;
}

const char* debug_menu_build_item_hs_variable_global(c_debug_menu* menu, char* error_buffer, DWORD error_buffer_size)
{
	if (!g_parser_state.m_has_variable)
	{
		sprintf_s(error_buffer, error_buffer_size, "global variable debug menu items must define a variable");
		return error_buffer;
	}

	const char* name = g_parser_state.m_has_name ? g_parser_state.m_name : g_parser_state.m_variable;

	e_hs_type type = _hs_unparsed;
	for (SHORT global_index = 0; global_index < k_hs_external_global_count; global_index++)
	{
		if (hs_external_globals[global_index] && 
			hs_external_globals[global_index]->name &&
			_stricmp(g_parser_state.m_variable, hs_external_globals[global_index]->name) == 0 && 
			hs_external_globals[global_index]->pointer)
		{
			type = (e_hs_type)hs_external_globals[global_index]->type;
			break;
		}
	}

	c_debug_menu_item* item = NULL;

	switch (type)
	{
	case _hs_type_boolean:
	{
		item = DEBUG_MENU_MALLOC(c_debug_menu_item_type_bool, menu, name, FALSE, g_parser_state.m_variable);
	}
	break;
	case _hs_type_real:
	{
		FLOAT inc_value = g_parser_state.m_has_inc ? g_parser_state.m_inc : 0.1f;
		FLOAT max_value = g_parser_state.m_has_max ? g_parser_state.m_max : k_real_max;
		FLOAT min_value = g_parser_state.m_has_min ? g_parser_state.m_min : k_real_min;

		item = DEBUG_MENU_MALLOC(c_debug_menu_item_type_real, menu, name, FALSE, g_parser_state.m_variable, min_value, max_value, inc_value);
	}
	break;
	case _hs_type_short_integer:
	{
		SHORT inc_value = g_parser_state.m_has_inc ? (SHORT)g_parser_state.m_inc : 1;
		SHORT max_value = g_parser_state.m_has_max ? (SHORT)g_parser_state.m_max : SHRT_MAX - 1;
		SHORT min_value = g_parser_state.m_has_min ? (SHORT)g_parser_state.m_min : SHRT_MIN + 1;

		item = DEBUG_MENU_MALLOC(c_debug_menu_item_type_short, menu, name, FALSE, g_parser_state.m_variable, min_value, max_value, inc_value);
	}
	break;
	case _hs_type_long_integer:
	{
		DWORD inc_value = g_parser_state.m_has_inc ? (DWORD)g_parser_state.m_inc : 1;
		DWORD max_value = g_parser_state.m_has_max ? (DWORD)g_parser_state.m_max : LONG_MAX - 1;
		DWORD min_value = g_parser_state.m_has_min ? (DWORD)g_parser_state.m_min : LONG_MIN + 1;

		item = DEBUG_MENU_MALLOC(c_debug_menu_item_type_long, menu, name, FALSE, g_parser_state.m_variable, min_value, max_value, inc_value);
	}
	break;
	}

	if (!item)
	{
		char undefined_name[1024];
		sprintf_s(undefined_name, sizeof(undefined_name), "UNDEFINED: %s", g_parser_state.m_variable);
		item = DEBUG_MENU_MALLOC(c_debug_menu_item, menu, undefined_name, NULL, FALSE);
	}
	ASSERT(item != NULL);

	menu->add_item(item);

	return NULL;
}

const char* debug_menu_build_item_command(c_debug_menu* menu, char* error_buffer, DWORD error_buffer_size)
{
	if (!g_parser_state.m_has_variable)
	{
		sprintf_s(error_buffer, error_buffer_size, "command menu items must define a variable");
		return error_buffer;
	}

	const char* name = g_parser_state.m_has_name ? g_parser_state.m_name : g_parser_state.m_variable;
	const char* command = g_parser_state.m_variable;
	menu->add_item(DEBUG_MENU_MALLOC(c_debug_menu_item_hs_command, menu, name, command));

	return NULL;
}

const char* debug_menu_build_item(c_debug_menu* menu, char* error_buffer, DWORD error_buffer_size)
{
	if (!g_parser_state.m_has_item_type)
	{
		sprintf_s(error_buffer, error_buffer_size, "menu items must supply a type");
		return error_buffer;
	}

	const char* result = NULL;
	switch (g_parser_state.m_item_type.get())
	{
	case _item_type_hs_variable_global:
	{
		result = debug_menu_build_item_hs_variable_global(menu, error_buffer, error_buffer_size);
	}
	break;
	case _item_type_command:
	{
		result = debug_menu_build_item_command(menu, error_buffer, error_buffer_size);
	}
	break;
	default:
	{
		UNREACHABLE();
	}
	break;
	}

	return result;
}

c_debug_menu* debug_menu_build_menu(e_property_owners property_owner, c_debug_menu* menu)
{
	const char* name = g_parser_state.m_has_name ? g_parser_state.m_name : "untitled menu";
	const char* caption = g_parser_state.m_has_caption ? g_parser_state.m_caption : "";
	c_debug_menu* child = NULL;

	for (DWORD i = 0; i < s_parser_state::k_string_length; i++)
	{
		if (g_parser_state.m_name[i] == '\t')
		{
			g_parser_state.m_name[i] = ',';
		}
	}

	switch (property_owner)
	{
	case _property_owner_menu:
	{
		child = DEBUG_MENU_MALLOC(c_debug_menu_scroll, menu, 26, name);
	}
	break;
	case _property_owner_zone_set_menu:
	{
		child = DEBUG_MENU_MALLOC(c_debug_menu_zone_sets, menu, 26, name);
	}
	break;
	default:
	{
		UNREACHABLE();
	}
	break;
	}

	child->set_caption(caption);
	menu->add_item(DEBUG_MENU_MALLOC(c_debug_menu_item_numbered, menu, name, child));

	return child;
}

void debug_menu_display_error(const char* error_text, BOOL fatal)
{
	// Xbox 360: Log error - could be extended to display on-screen
	OutputDebugStringA(fatal ? "DEBUG_MENU_ERROR: " : "DEBUG_MENU_WARNING: ");
	OutputDebugStringA(error_text);
	OutputDebugStringA("\n");
}

// Continue in next file part due to size...
const char* debug_menu_build_recursive(FILE* menu_file, DWORD& file_char, c_debug_menu* menu, DWORD* line_count, char* error_buffer, DWORD error_buffer_length);

void debug_menu_parse(c_debug_menu* root_menu, const char* file_name)
{
	ASSERT(file_name != NULL);
	ASSERT(root_menu != NULL);

	FILE* file = NULL;
	if (fopen_s(&file, file_name, "rb") == 0 && file)
	{
		char error_buffer[1024];
		memset(error_buffer, 0, sizeof(error_buffer));

		DWORD line_count = 1;
		DWORD file_char = (DWORD)fgetc(file);
		debug_menu_build_recursive(file, file_char, root_menu, &line_count, error_buffer, sizeof(error_buffer));
		fclose(file);
	}
}

// Continuing the recursive parser - split due to size
const char* debug_menu_build_recursive(FILE* menu_file, DWORD& file_char, c_debug_menu* menu, DWORD* line_count, char* error_buffer, DWORD error_buffer_length)
{
	const char* parse_error = NULL;

	parse_stack_t parse_stack;
	parse_stack.push_back(_parse_state_none);

	g_parser_state.reset();

	ASSERT(menu_file != NULL);
	ASSERT(menu != NULL);
	ASSERT(line_count != NULL);
	ASSERT(error_buffer != NULL);

	while (file_char && file_char != -1 && !parse_error)
	{
		DWORD advance_distance = 0;
		e_advance_type advance_process_type = _advance_type_process_token;

		if (*parse_stack.get_top() == _parse_state_reading_number)
		{
			if (IN_RANGE_INCLUSIVE(file_char, '0', '9') || file_char == '.')
			{
				g_parser_state.m_number_buffer[g_parser_state.m_number_buffer_index++] = (char)file_char;
				g_parser_state.m_number_buffer[g_parser_state.m_number_buffer_index] = 0;

				advance_distance = 1;
				advance_process_type = _advance_type_process_distance;
			}
			else
			{
				debug_menu_store_number_property(&parse_stack);
				if (file_char)
				{
					file_char = fgetc(menu_file);
				}
			}
		}
		else
		{
			if (*parse_stack.get_top() == _parse_state_reading_escape_character)
			{
				parse_stack.pop();
				PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_string)
				{
					PARSER_ASSERT(VALID_INDEX(g_parser_state.m_string_buffer_index, s_parser_state::k_string_length))
					{
						g_parser_state.m_string_buffer[g_parser_state.m_string_buffer_index++] = (char)file_char;
						g_parser_state.m_string_buffer[g_parser_state.m_string_buffer_index] = 0;

						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;
					}
				}
			}
			else if (*parse_stack.get_top() == _parse_state_reading_string && file_char != '"')
			{
				if (file_char == '\\')
				{
					PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_reading_string, "can not use escape sequences outside of string declaration")
					{
						parse_stack.push_back(_parse_state_reading_escape_character);
					}
				}
				else
				{
					PARSER_ASSERT(VALID_INDEX(g_parser_state.m_string_buffer_index, s_parser_state::k_string_length))
					{
						g_parser_state.m_string_buffer[g_parser_state.m_string_buffer_index++] = (char)file_char;
						g_parser_state.m_string_buffer[g_parser_state.m_string_buffer_index] = 0;
					}
				}

				advance_distance = 1;
				advance_process_type = _advance_type_process_distance;
			}
			else
			{
				if (IN_RANGE_INCLUSIVE(file_char, '0', '9'))
				{
					PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_reading_property_found_eqauls, "losse number not assigned to property")
					{
						parse_stack.push_back(_parse_state_reading_number);
						g_parser_state.m_number_buffer_index = 0;
						g_parser_state.m_number_buffer[g_parser_state.m_number_buffer_index++] = (char)file_char;
						g_parser_state.m_number_buffer[g_parser_state.m_number_buffer_index] = 0;

						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;
					}
				}
				else
				{
					switch (file_char)
					{
					case _symbol_random_whitespace:
					case _symbol_tab:
					case _symbol_white_space:
					{
						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;
					}
					break;
					case _symbol_quote:
					{
						DWORD state = *parse_stack.get_top();
						if (state == _parse_state_reading_property_found_eqauls)
						{
							g_parser_state.m_string_buffer_index = 0;
							parse_stack.push_back(_parse_state_reading_string);
						}
						else
						{
							PARSER_ASSERT_WITH_MESSAGE(state == _parse_state_reading_string, "unexpected symbol \"")
							{
								debug_menu_store_string_property(&parse_stack);
							}
						}

						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;
					}
					break;
					case _symbol_minus:
					case _symbol_period:
					{
						PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_reading_property_found_eqauls, "losse number not assigned to property")
						{
							parse_stack.push_back(_parse_state_reading_number);
							g_parser_state.m_number_buffer_index = 0;
							g_parser_state.m_number_buffer[g_parser_state.m_number_buffer_index++] = (char)file_char;
							g_parser_state.m_number_buffer[g_parser_state.m_number_buffer_index] = 0;
						}

						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;
					}
					break;
					case _symbol_back_slash:
					{
						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;

						PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_reading_tag, "unexpected symbol back slash")
						{
							parse_stack.push_back(_parse_state_reading_close_tag);
						}
					}
					break;
					case _symbol_less_than:
					{
						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;

						PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_none, "unexpected symbol less than")
						{
							parse_stack.push_back(_parse_state_reading_tag);
							g_parser_state.reset();
						}
					}
					break;
					case _symbol_equals:
					{
						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;

						PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_reading_property, "= sign expected")
						{
							PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_property)
							{
								parse_stack.push_back(_parse_state_reading_property_found_eqauls);
							}
						}
					}
					break;
					case _symbol_greater_than:
					{
						advance_distance = 1;
						advance_process_type = _advance_type_process_distance;

						if (*parse_stack.get_top() == _parse_state_reading_open_tag)
						{
							parse_stack.pop();
							PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_tag)
							{
								parse_stack.pop();
								char build_property_error[1024];
								memset(build_property_error, 0, sizeof(build_property_error));
								if (g_parser_state.m_current_property_owner == _property_owner_item)
								{
									PARSER_ASSERT_WITH_MESSAGE(!debug_menu_build_item(menu, build_property_error, sizeof(build_property_error)), build_property_error);
								}
								else
								{
									c_debug_menu* built_menu = debug_menu_build_menu(g_parser_state.m_current_property_owner, menu);
									PARSER_ASSERT_WITH_MESSAGE(built_menu, build_property_error)
									{
										advance_process_type = _advance_type_process_nothing;

										file_char = fgetc(menu_file);
										const char* recursive_build_error = debug_menu_build_recursive(menu_file, file_char, built_menu, line_count, error_buffer, error_buffer_length);
										PARSER_ASSERT_WITH_MESSAGE(!recursive_build_error, recursive_build_error);
									}
								}
							}

							break;
						}

						PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_reading_close_tag, "unexpected symbol greater than")
						{
							parse_stack.pop();
							PARSER_ASSERT(*parse_stack.get_top() == _parse_state_reading_tag);
							{
								file_char = fgetc(menu_file);
								return NULL;
							}
						}
					}
					break;
					case _symbol_forward_slash:
					{
						PARSER_ASSERT_WITH_MESSAGE(*parse_stack.get_top() == _parse_state_reading_string, "can not use escape sequences outside of string declaration");
					}
					break;
					}
				}
			}
		}

		if (advance_process_type == _advance_type_process_token)
		{
			e_tokens token = _token_none;
			char token_buffer[1024];
			memset(token_buffer, 0, sizeof(token_buffer));

			DWORD maximum_token_name_length = 0;
			for (DWORD i = 0; i < NUMBEROF(g_token_names); i++)
			{
				DWORD token_name_length = (DWORD)strlen(g_token_names[i]);
				if (token_name_length > maximum_token_name_length)
				{
					maximum_token_name_length = token_name_length;
				}
			}
			ASSERT(maximum_token_name_length + 1 < NUMBEROF(token_buffer));

			debug_menu_look_ahead_read_token(menu_file, file_char, token_buffer, maximum_token_name_length + 1);
			for (DWORD i = 0; i < NUMBEROF(g_token_names); i++)
			{
				if (string_in_string_case_insensitive(token_buffer, g_token_names[i]))
				{
					token = (e_tokens)i;

					advance_distance = (DWORD)strlen(g_token_names[i]);
					advance_process_type = _advance_type_process_distance;
					break;
				}
			}

			switch (token)
			{
			TOKEN_CASE_PROPERTY(min);
			TOKEN_CASE_PROPERTY(max);
			TOKEN_CASE_PROPERTY(inc);
			TOKEN_CASE_PROPERTY_OWNER(menu, TRUE);
			TOKEN_CASE_PROPERTY_OWNER(zone_set_menu, TRUE);
			TOKEN_CASE_PROPERTY_OWNER(item, FALSE);
			TOKEN_CASE_PROPERTY(caption);
			TOKEN_CASE_PROPERTY(name);
			TOKEN_CASE_PROPERTY(variable);
			TOKEN_CASE_PROPERTY(color);
			TOKEN_CASE_PROPERTY(type);
			TOKEN_CASE_TYPE(global);
			TOKEN_CASE_TYPE(command);
			TOKEN_CASE_NEW_LINE(eol_0);
			TOKEN_CASE_NEW_LINE(eol_1);
			TOKEN_CASE_NEW_LINE(eol_2);
			TOKEN_CASE_NEW_LINE(eol_3);
			}
		}

		PARSER_ASSERT_WITH_MESSAGE(advance_process_type, "unexpected token")
		{
			if (advance_process_type == _advance_type_process_distance)
			{
				ASSERT(advance_distance != 0);
				while (advance_distance-- > 0)
				{
					file_char = fgetc(menu_file);
				}
			}
			else ASSERT(advance_process_type < k_advance_type_count);
		}
	}

	return parse_error ? parse_error : NULL;
}

#undef TOKEN_CASE_NEW_LINE
#undef TOKEN_CASE_TYPE
#undef TOKEN_CASE_PROPERTY_OWNER
#undef TOKEN_CASE_PROPERTY

#undef PARSER_ASSERT_WITH_MESSAGE
#undef PARSER_ASSERT
