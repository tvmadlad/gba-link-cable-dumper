/*
 * Copyright (C) 2026 tvmadlad
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include "app/folder_browser.h"
#include "storage/storage.h"
#include "storage/paths.h"
#include "ui/ui.h"
#include "ui/input.h"

#define MAX_ENTRIES 256

typedef enum
{
	ENTRY_SELECT,
	ENTRY_NEW,
	ENTRY_UP,
	ENTRY_DIR,
	ENTRY_DEVICE,
} entry_type;

typedef struct
{
	entry_type type;
	char      *name; // directory name or device id
	char      *label;
} entry;

typedef struct
{
	entry items[MAX_ENTRIES];
	int   count;
} entry_list;

static void add_entry(entry_list *list, entry_type type, const char *name, const char *label)
{
	if(list->count >= MAX_ENTRIES)
	{
		return;
	}
	char *n = strdup(name), *l = strdup(label);
	if(!n || !l)
	{
		free(n);
		free(l);
		return;
	}
	entry *e = &list->items[list->count++];
	e->type  = type;
	e->name  = n;
	e->label = l;
}

static void add_dir(const char *name, void *user)
{
	char label[NAME_MAX + 2];
	snprintf(label, sizeof(label), "%s/", name);
	add_entry(user, ENTRY_DIR, name, label);
}

static int compare_entries(const void *a, const void *b)
{
	return strcasecmp(((const entry *)a)->name, ((const entry *)b)->name);
}

static void clear_entries(entry_list *list)
{
	int i;
	for(i = 0; i < list->count; i++)
	{
		free(list->items[i].name);
		free(list->items[i].label);
	}
	list->count = 0;
}

bool folder_browser_new_folder(char *path, size_t len)
{
	char test[PATH_MAX];
	int  n;
	for(n = 1; n < 100; n++)
	{
		char name[32];
		if(n == 1)
		{
			snprintf(name, sizeof(name), "New Folder");
		}
		else
		{
			snprintf(name, sizeof(name), "New Folder %d", n);
		}
		snprintf(test, sizeof(test), "%s", path);
		if(!paths_join(test, sizeof(test), name) || strlen(test) >= len)
		{
			return false;
		}
		if(!storage_dir_exists(test))
		{
			if(!storage_mkdirs(test))
			{
				return false;
			}
			snprintf(path, len, "%s", test);
			return true;
		}
	}
	return false;
}

static void build_list(entry_list *list, const char *path, bool allow_devices)
{
	clear_entries(list);
	if(!path[0])
	{
		//device list
		int i;
		for(i = 0; i < storage_device_count(); i++)
		{
			const storage_device *d = storage_device_at(i);
			if(!d->mounted)
			{
				continue;
			}
			char label[64];
			snprintf(label, sizeof(label), "%s (%s:/)", d->name, d->id);
			add_entry(list, ENTRY_DEVICE, d->id, label);
		}
		return;
	}
	add_entry(list, ENTRY_SELECT, "", "[ Use this folder ]");
	add_entry(list, ENTRY_NEW, "", "[ New folder ]");
	if(!paths_is_device_root(path) || allow_devices)
	{
		add_entry(list, ENTRY_UP, "..", "../");
	}
	int first_dir = list->count;
	storage_list_dirs(path, add_dir, list);
	qsort(list->items + first_dir, list->count - first_dir, sizeof(entry), compare_entries);
}

bool folder_browser_run(const char *title, const char *start, bool allow_devices, char *out, size_t len)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s", start);
	//fall back to the closest existing parent
	while(path[0] && !storage_dir_exists(path))
	{
		if(paths_is_device_root(path))
		{
			path[0] = '\0';
			break;
		}
		paths_parent(path);
	}
	if(!path[0] && !allow_devices)
	{
		return false;
	}

	entry_list *list = calloc(1, sizeof(entry_list));
	if(!list)
	{
		return false;
	}
	ui_menu_item *items = calloc(MAX_ENTRIES, sizeof(ui_menu_item));
	if(!items)
	{
		free(list);
		return false;
	}
	bool result  = false;
	bool refresh = true;
	bool redraw  = true;
	int  cursor  = 0;
	while(1)
	{
		if(refresh)
		{
			build_list(list, path, allow_devices);
			int i;
			for(i = 0; i < list->count; i++)
			{
				items[i].label = list->items[i].label;
				items[i].value = NULL;
			}
			cursor  = 0;
			refresh = false;
			redraw  = true;
		}
		if(redraw)
		{
			char heading[PATH_MAX + 64];
			snprintf(heading, sizeof(heading), "%s\n%s", title, path[0] ? path : "Choose a device");
			ui_draw_menu(heading, items, list->count, cursor,
				"A: open/select  B: cancel  X: use this folder");
			redraw = false;
		}
		input_scan();
		ui_frame();
		u32 btns = input_down();
		if(btns & INPUT_START)
		{
			ui_exit();
		}
		else if(btns & INPUT_B)
		{
			break;
		}
		else if((btns & INPUT_UP) && list->count > 0)
		{
			cursor = (cursor + list->count - 1) % list->count;
			redraw = true;
		}
		else if((btns & INPUT_DOWN) && list->count > 0)
		{
			cursor = (cursor + 1) % list->count;
			redraw = true;
		}
		else if((btns & INPUT_X) && path[0])
		{
			snprintf(out, len, "%s", path);
			result = true;
			break;
		}
		else if((btns & INPUT_A) && list->count > 0)
		{
			entry *e = &list->items[cursor];
			switch(e->type)
			{
				case ENTRY_SELECT:
					snprintf(out, len, "%s", path);
					result = true;
					break;
				case ENTRY_NEW:
					if(!folder_browser_new_folder(path, sizeof(path)))
					{
						ui_warn("ERROR: Could not create folder!");
					}
					refresh = true;
					break;
				case ENTRY_UP:
					if(paths_is_device_root(path))
					{
						path[0] = '\0';
					}
					else
					{
						paths_parent(path);
					}
					refresh = true;
					break;
				case ENTRY_DIR:
					if(!paths_join(path, sizeof(path), e->name))
					{
						ui_warn("ERROR: That folder's path is too long!");
					}
					refresh = true;
					break;
				case ENTRY_DEVICE:
					snprintf(path, sizeof(path), "%s:/", e->name);
					refresh = true;
					break;
			}
			if(result)
			{
				break;
			}
		}
	}
	clear_entries(list);
	free(list);
	free(items);
	return result;
}
