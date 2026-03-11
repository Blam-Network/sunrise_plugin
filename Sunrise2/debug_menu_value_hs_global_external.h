#pragma once

#include "stdafx.h"

// Xbox 360 stub for HaloScript external global types
enum e_hs_type
{
	_hs_unparsed = 0,
	_hs_type_boolean,
	_hs_type_real,
	_hs_type_short_integer,
	_hs_type_long_integer,
	
	k_hs_type_count
};

// Xbox 360 stub for HaloScript external global descriptor
struct hs_global_external
{
	const char* name;
	DWORD type;
	void* pointer;
};

// Xbox 360: Stub array - will be populated by game hooks
#define k_hs_external_global_count 256
extern const hs_global_external* hs_external_globals[k_hs_external_global_count];

// Template class for managing HaloScript global variables
// Matches ManagedDonkey behavior exactly
template<typename t_type>
class c_debug_menu_value_hs_global_external
{
public:
	c_debug_menu_value_hs_global_external(const char* hs_global_name)
	{
		m_hs_global_external_index = -1;
		
		if (!hs_global_name)
			return;
		
		for (SHORT global_index = 0; global_index < k_hs_external_global_count && m_hs_global_external_index == -1; global_index++)
		{
			const hs_global_external* global_external = hs_external_globals[global_index];
			
			if (global_external && 
				global_external->name &&
				global_external->pointer &&
				_stricmp(hs_global_name, global_external->name) == 0 &&
				global_external->type >= _hs_type_boolean && 
				global_external->type <= _hs_type_long_integer)
			{
				m_hs_global_external_index = global_index;
			}
		}
	}
	
	t_type get()
	{
		BYTE bytes[sizeof(t_type)];
		memset(bytes, 0, sizeof(t_type));
		
		if (m_hs_global_external_index != -1)
		{
			if (hs_external_globals[m_hs_global_external_index]->pointer != NULL)
			{
				*(t_type*)bytes = *(t_type*)hs_external_globals[m_hs_global_external_index]->pointer;
			}
		}
		
		return *(t_type*)bytes;
	}
	
	void set(t_type value)
	{
		if (m_hs_global_external_index != -1)
		{
			if (hs_external_globals[m_hs_global_external_index]->pointer != NULL)
			{
				*(t_type*)hs_external_globals[m_hs_global_external_index]->pointer = value;
			}
		}
	}
	
protected:
	SHORT m_hs_global_external_index;
};
