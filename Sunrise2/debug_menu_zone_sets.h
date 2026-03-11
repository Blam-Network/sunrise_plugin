#pragma once

#include "stdafx.h"
#include "debug_menu_scroll.h"

class c_debug_menu_zone_sets :
	public c_debug_menu_scroll
{
public:
	virtual ~c_debug_menu_zone_sets() {}
	virtual void notify_selected(SHORT selected_value);
	virtual void open();

protected:
	virtual void notify_up();
	virtual void notify_down();

public:
	c_debug_menu_zone_sets(c_debug_menu* parent, SHORT num_visible, const char* name);

private:
	void update_caption();

protected:
	char m_caption[128];
};
