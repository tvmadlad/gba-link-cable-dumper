/*
 * Copyright (C) 2016 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include "storage/storage.h"
#include "storage/paths.h"

static char dump_dir[PATH_MAX] = PATHS_DEFAULT_DUMP_DIR;
static bool split_folders      = false;

static const char *kind_subdir[] = { "ROMs", "Saves", "BIOS" };
static const char *kind_ext[]    = { ".gba", ".sav", ".bin" };

void paths_set_dump_dir(const char *dir)
{
	snprintf(dump_dir, sizeof(dump_dir), "%s", dir);
	size_t len = strlen(dump_dir);
	//keep the slash of a device root like "sd:/"
	if(len > 1 && dump_dir[len - 1] == '/' && dump_dir[len - 2] != ':')
	{
		dump_dir[len - 1] = '\0';
	}
}

const char *paths_dump_dir(void)
{
	return dump_dir;
}

void paths_set_split_folders(bool split)
{
	split_folders = split;
}

void paths_kind_dir(char *out, size_t len, paths_kind kind)
{
	size_t      dirlen = strlen(dump_dir);
	const char *sep    = (dirlen > 0 && dump_dir[dirlen - 1] == '/') ? "" : "/";
	int         n;
	if(split_folders)
	{
		n = snprintf(out, len, "%s%s%s", dump_dir, sep, kind_subdir[kind]);
	}
	else
	{
		n = snprintf(out, len, "%s", dump_dir);
	}
	//a cut off path would point somewhere else, leave it empty so writes fail
	if(n < 0 || (size_t)n >= len)
	{
		out[0] = '\0';
	}
}

bool paths_create_dirs(void)
{
	if(!storage_mkdirs(dump_dir))
	{
		return false;
	}
	if(split_folders)
	{
		char dir[PATH_MAX];
		int  kind;
		for(kind = PATHS_KIND_ROM; kind <= PATHS_KIND_BIOS; kind++)
		{
			paths_kind_dir(dir, sizeof(dir), kind);
			if(!storage_mkdirs(dir))
			{
				return false;
			}
		}
	}
	return true;
}

// a path that doesn't fit is left empty, so a write to it fails
static bool cut_off(char *out)
{
	out[0] = '\0';
	return false;
}

// out = "<kind dir>/", returns its length or -1 if it doesn't fit
static int kind_dir_prefix(char *out, size_t len, paths_kind kind)
{
	paths_kind_dir(out, len, kind);
	size_t dirlen = strlen(out);
	if(dirlen == 0)
	{
		return -1;
	}
	if(out[dirlen - 1] != '/')
	{
		if(dirlen + 1 >= len)
		{
			return -1;
		}
		out[dirlen++] = '/';
		out[dirlen]   = '\0';
	}
	return dirlen;
}

// the rest of the path after the folder, written by snprintf
static bool name_fits(char *out, size_t len, int dirlen, int n)
{
	if(n < 0 || (size_t)n >= len - dirlen)
	{
		return cut_off(out);
	}
	return true;
}

// "<title> [<game code><maker code>]", sanitized
static void cart_base_name(char *out, size_t len, const gba_cart_info *cart)
{
	snprintf(out, len, "%.12s [%.4s%.2s]",
		GBA_CART_TITLE(cart), GBA_CART_GAME_CODE(cart), GBA_CART_MAKER_CODE(cart));
	paths_sanitize_filename(out);
}

bool paths_cart_file(char *out, size_t len, const gba_cart_info *cart, paths_kind kind)
{
	int dirlen = kind_dir_prefix(out, len, kind);
	if(dirlen < 0)
	{
		return cut_off(out);
	}
	char base[64];
	cart_base_name(base, sizeof(base), cart);
	int n = snprintf(out + dirlen, len - dirlen, "%s%s", base, kind_ext[kind]);
	return name_fits(out, len, dirlen, n);
}

bool paths_save_backup_file(char *out, size_t len, const gba_cart_info *cart, const struct tm *when)
{
	if(!when)
	{
		return paths_cart_file(out, len, cart, PATHS_KIND_SAVE);
	}
	int dirlen = kind_dir_prefix(out, len, PATHS_KIND_SAVE);
	if(dirlen < 0)
	{
		return cut_off(out);
	}
	char base[64];
	cart_base_name(base, sizeof(base), cart);
	//no colons on FAT, and this order sorts by date
	int n = snprintf(out + dirlen, len - dirlen, "%s %04d-%02d-%02d %02d-%02d-%02d%s", base,
		when->tm_year + 1900, when->tm_mon + 1, when->tm_mday,
		when->tm_hour, when->tm_min, when->tm_sec, kind_ext[PATHS_KIND_SAVE]);
	return name_fits(out, len, dirlen, n);
}

typedef struct
{
	const char *base;
	size_t      base_len;
	char        best[NAME_MAX + 1];
	time_t      best_mtime;
	bool        found;
} latest_ctx;

static bool has_save_ext(const char *name)
{
	size_t len = strlen(name);
	return len >= 4 && strcasecmp(name + len - 4, kind_ext[PATHS_KIND_SAVE]) == 0;
}

static void check_latest(const char *name, time_t mtime, void *user)
{
	latest_ctx *ctx = user;
	//"<base>.sav", "<base> <anything>.sav"
	if(strncmp(name, ctx->base, ctx->base_len) != 0 || !has_save_ext(name))
	{
		return;
	}
	char next = name[ctx->base_len];
	if(next != '.' && next != ' ')
	{
		return;
	}
	//too long to keep, it could not be opened reliably anyway
	if(strlen(name) >= sizeof(ctx->best))
	{
		return;
	}
	//newest wins, the name breaks ties so timestamped names still order
	if(!ctx->found || mtime > ctx->best_mtime ||
		(mtime == ctx->best_mtime && strcmp(name, ctx->best) > 0))
	{
		if(snprintf(ctx->best, sizeof(ctx->best), "%s", name) >= (int)sizeof(ctx->best))
		{
			return;
		}
		ctx->best_mtime = mtime;
		ctx->found      = true;
	}
}

bool paths_find_latest_save(char *out, size_t len, const gba_cart_info *cart)
{
	char dir[PATH_MAX];
	paths_kind_dir(dir, sizeof(dir), PATHS_KIND_SAVE);
	if(!dir[0])
	{
		return false;
	}
	char base[64];
	cart_base_name(base, sizeof(base), cart);
	latest_ctx ctx;
	ctx.base     = base;
	ctx.base_len = strlen(base);
	ctx.found    = false;
	storage_list_files(dir, check_latest, &ctx);
	if(!ctx.found)
	{
		return false;
	}
	size_t dirlen = strlen(dir);
	int    n      = snprintf(out, len, "%s%s%s", dir, (dirlen > 0 && dir[dirlen - 1] == '/') ? "" : "/", ctx.best);
	return n > 0 && (size_t)n < len;
}

bool paths_bios_file(char *out, size_t len)
{
	int dirlen = kind_dir_prefix(out, len, PATHS_KIND_BIOS);
	if(dirlen < 0)
	{
		return cut_off(out);
	}
	int n = snprintf(out + dirlen, len - dirlen, "gba_bios.bin");
	return name_fits(out, len, dirlen, n);
}

bool paths_resolve_existing(char *path, size_t len, paths_existing policy)
{
	//empty is a path that didn't fit, there's nowhere to write
	if(!path[0])
	{
		return false;
	}
	if(!storage_file_exists(path))
	{
		return true;
	}
	if(policy == PATHS_EXISTING_SKIP)
	{
		return false;
	}
	if(policy == PATHS_EXISTING_OVERWRITE)
	{
		return true;
	}
	//keep both, find a free "name (n).ext"
	char base[PATH_MAX];
	if(snprintf(base, sizeof(base), "%s", path) >= (int)sizeof(base))
	{
		return cut_off(path);
	}
	char *slash   = strrchr(base, '/');
	char *dot     = strrchr(base, '.');
	char  ext[16] = "";
	if(dot && (!slash || dot > slash))
	{
		//a cut off extension would name another file
		if(snprintf(ext, sizeof(ext), "%s", dot) >= (int)sizeof(ext))
		{
			return cut_off(path);
		}
		*dot = '\0';
	}
	int n;
	for(n = 1; n < 1000; n++)
	{
		int w = snprintf(path, len, "%s (%d)%s", base, n, ext);
		if(w < 0 || (size_t)w >= len)
		{
			return cut_off(path);
		}
		if(!storage_file_exists(path))
		{
			return true;
		}
	}
	return false;
}

void paths_sanitize_filename(char *str)
{
	for(; *str; ++str)
	{
		if(*str < 0x20 || *str > 0x7F)
		{
			*str = '_';
		}
		else
		{
			switch(*str)
			{
				case '\\':
				case '/':
				case ':':
				case '*':
				case '?':
				case '\"':
				case '<':
				case '>':
				case '|':
					*str = '_';
					break;
				default:
					break;
			}
		}
	}
}

bool paths_is_device_root(const char *path)
{
	size_t len = strlen(path);
	return len >= 2 && path[len - 1] == '/' && path[len - 2] == ':';
}

void paths_parent(char *path)
{
	char *slash = strrchr(path, '/');
	if(!slash)
	{
		return;
	}
	//keep the slash of a device root like "sd:/"
	if(slash > path && slash[-1] == ':')
	{
		slash[1] = '\0';
	}
	else
	{
		*slash = '\0';
	}
}

bool paths_join(char *path, size_t len, const char *name)
{
	size_t plen = strlen(path);
	int    n    = snprintf(path + plen, len - plen, "%s%s", paths_is_device_root(path) ? "" : "/", name);
	if(n < 0 || (size_t)n >= len - plen)
	{
		path[plen] = '\0';
		return false;
	}
	return true;
}
