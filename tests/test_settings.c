/*
 * Copyright (C) 2026 tvmadlad
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
/*
 * Host tests for settings, ini, paths and storage.
 * Builds the real sources against the stub libogc headers in tests/stub.
 * Device roots like "sd2:/" are plain folders, in a folder of their own for
 * each test, so every test starts with empty cards.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <time.h>
#include "unity.h"
#include "settings/settings.h"
#include "settings/ini.h"
#include "storage/storage.h"
#include "storage/paths.h"

bool stub_present_sd2 = true, stub_present_sda = true;

static settings_t   *s;
static char          p[1024];
static gba_cart_info cart;
static char          run_dir[1024]; // where the test folders are made

typedef struct
{
	int  count;
	char keys[8][16];
	char values[8][16];
} ini_result;

static void ini_collect(const char *k, const char *v, void *u)
{
	ini_result *r = u;
	if(r->count < 8)
	{
		snprintf(r->keys[r->count], 16, "%s", k);
		snprintf(r->values[r->count], 16, "%s", v);
		r->count++;
	}
}

static void write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
	fputs(text, f);
	fclose(f);
}

static bool file_has(const char *path, const char *text)
{
	char   buf[4096];
	FILE  *f = fopen(path, "r");
	size_t r = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
	buf[r]   = 0;
	if(f)
	{
		fclose(f);
	}
	return strstr(buf, text) != NULL;
}

static const char *active_id(void)
{
	return storage_active() ? storage_active()->id : "(none)";
}

// each test gets a folder named after it, left there afterwards to look at,
// with empty cards in sd2 and sda, and starts like a first run
void setUp(void)
{
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, mkdir(Unity.CurrentTestName, 0777), "run the tests in an empty folder");
	TEST_ASSERT_EQUAL_INT(0, chdir(Unity.CurrentTestName));
	mkdir("sd2:", 0777);
	mkdir("sda:", 0777);
	storage_init();
	s = settings_get();
	settings_load(NULL);
	settings_apply();
	memset(&cart, 0, sizeof(cart));
	memcpy(cart.header + 0xA0, "POKEMON:EMER", 12);
	memcpy(cart.header + 0xAC, "BPEE", 4);
	memcpy(cart.header + 0xB0, "01", 2);
}

void tearDown(void)
{
	TEST_ASSERT_EQUAL_INT(0, chdir(run_dir));
}

// settings with split folders on, saved in the default folder and loaded from there
static void save_default_settings(void)
{
	s->split_folders = true;
	TEST_ASSERT_TRUE(settings_save());
	TEST_ASSERT_TRUE(settings_load(NULL));
}

// comments, sections, whitespace, CRLF, malformed lines, no trailing newline
static void test_ini_parser(void)
{
	char       ini[] = "; comment\n[section]\n  device = sd2  \r\ndump_dir=/my dumps\n#x=y\nnoequals\n=novalue\nempty=\nlast=nolf";
	ini_result r     = { 0 };
	ini_parse_buffer(ini, ini_collect, &r);
	TEST_ASSERT_EQUAL_INT(4, r.count);
	TEST_ASSERT_EQUAL_STRING("device", r.keys[0]);
	TEST_ASSERT_EQUAL_STRING("sd2", r.values[0]);
	TEST_ASSERT_EQUAL_STRING("/my dumps", r.values[1]);
	TEST_ASSERT_EQUAL_STRING("empty", r.keys[2]);
	TEST_ASSERT_EQUAL_STRING("", r.values[2]);
	TEST_ASSERT_EQUAL_STRING("last", r.keys[3]);
	TEST_ASSERT_EQUAL_STRING("nolf", r.values[3]);
}

// a UTF-8 byte order mark from a Windows editor, and old Mac line endings
static void test_ini_byte_order_mark_and_cr_line_endings(void)
{
	char       bom[] = "\xEF\xBB\xBF"
					   "device=sd2\rsplit_folders=1\r";
	ini_result r     = { 0 };
	ini_parse_buffer(bom, ini_collect, &r);
	TEST_ASSERT_EQUAL_INT(2, r.count);
	TEST_ASSERT_EQUAL_STRING("device", r.keys[0]);
	TEST_ASSERT_EQUAL_STRING("split_folders", r.keys[1]);
}

// missing, empty, and too big to be a settings file
static void test_ini_files(void)
{
	ini_result r = { 0 };
	TEST_ASSERT_FALSE(ini_parse_file("sd2:/missing.ini", ini_collect, &r));
	write_file("sd2:/empty.ini", "");
	TEST_ASSERT_TRUE(ini_parse_file("sd2:/empty.ini", ini_collect, &r));
	TEST_ASSERT_EQUAL_INT(0, r.count);
	FILE *f = fopen("sd2:/big.ini", "w");
	for(int k = 0; k < 0x4001; k++)
	{
		fputc(k % 64 ? 'x' : '\n', f);
	}
	fclose(f);
	TEST_ASSERT_FALSE(ini_parse_file("sd2:/big.ini", ini_collect, &r));
	TEST_ASSERT_EQUAL_INT(0, r.count);
}

// nothing found: defaults, and auto picks sd2 (first in order)
static void test_first_run(void)
{
	TEST_ASSERT_FALSE(settings_load("sda:/apps/gbadumper/boot.dol"));
	TEST_ASSERT_TRUE(settings_apply());
	TEST_ASSERT_EQUAL_STRING("sd2", active_id());
	TEST_ASSERT_EQUAL_STRING("sd2:/dumps", paths_dump_dir());
	TEST_ASSERT_EQUAL_STRING("sd2:/gbadumper/settings.ini", settings_file());
}

static void test_save_and_load(void)
{
	s->split_folders = true;
	s->existing      = PATHS_EXISTING_KEEP_BOTH;
	strcpy(s->dump_dir, "/gba/dumps");
	TEST_ASSERT_TRUE(settings_save());
	settings_defaults(s);
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_TRUE(s->split_folders);
	TEST_ASSERT_EQUAL_INT(PATHS_EXISTING_KEEP_BOTH, s->existing);
	TEST_ASSERT_EQUAL_STRING("/gba/dumps", s->dump_dir);
	settings_apply();
	paths_kind_dir(p, sizeof(p), PATHS_KIND_SAVE);
	TEST_ASSERT_EQUAL_STRING("sd2:/gba/dumps/Saves", p);
	TEST_ASSERT_TRUE(paths_create_dirs());
	TEST_ASSERT_TRUE(storage_dir_exists("sd2:/gba/dumps/ROMs"));
}

// settings next to the dol win over the default folder
static void test_settings_next_to_the_dol_win(void)
{
	TEST_ASSERT_TRUE(settings_save());
	storage_mkdirs("sda:/apps/gbadumper");
	write_file("sda:/apps/gbadumper/settings.ini", "device=sda\n");
	TEST_ASSERT_TRUE(settings_load("sda:/apps/gbadumper/boot.dol"));
	TEST_ASSERT_EQUAL_STRING("sda", s->device);
	TEST_ASSERT_EQUAL_STRING("sda:/apps/gbadumper/settings.ini", settings_file());
}

// moved to a custom folder: a pointer is left in the default folder, and the
// next start follows it
static void test_move_leaves_a_pointer(void)
{
	save_default_settings();
	TEST_ASSERT_TRUE(settings_move("sda:/custom/cfg"));
	TEST_ASSERT_EQUAL_STRING("sda:/custom/cfg/settings.ini", settings_file());
	TEST_ASSERT_TRUE(file_has("sd2:/gbadumper/settings.ini", "settings_path=sda:/custom/cfg/settings.ini"));
	settings_defaults(s);
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_EQUAL_STRING("sda:/custom/cfg/settings.ini", settings_file());
	TEST_ASSERT_TRUE(s->split_folders);
}

// moved again: the default pointer and the old file both point at the new
// place, so there's never a chain of two pointers
static void test_second_move_updates_both_pointers(void)
{
	save_default_settings();
	TEST_ASSERT_TRUE(settings_move("sda:/custom/cfg"));
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_TRUE(settings_move("sd2:/other"));
	TEST_ASSERT_TRUE(file_has("sd2:/gbadumper/settings.ini", "settings_path=sd2:/other/settings.ini"));
	TEST_ASSERT_TRUE(file_has("sda:/custom/cfg/settings.ini", "settings_path=sd2:/other/settings.ini"));
	settings_defaults(s);
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_EQUAL_STRING("sd2:/other/settings.ini", settings_file());
	TEST_ASSERT_TRUE(s->split_folders);
}

// moved back to the default folder: the settings replace the pointer there
static void test_move_back_to_the_default_folder(void)
{
	save_default_settings();
	TEST_ASSERT_TRUE(settings_move("sd2:/other"));
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_TRUE(settings_move("sd2:/gbadumper"));
	TEST_ASSERT_FALSE(file_has("sd2:/gbadumper/settings.ini", "settings_path"));
	TEST_ASSERT_TRUE(file_has("sd2:/gbadumper/settings.ini", "device="));
	settings_defaults(s);
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_EQUAL_STRING("sd2:/gbadumper/settings.ini", settings_file());
	TEST_ASSERT_TRUE(s->split_folders);
}

// two pointers at each other: followed once, no loop
static void test_pointer_loop_is_followed_once(void)
{
	storage_mkdirs("sda:/loop");
	write_file("sda:/loop/settings.ini", "settings_path=sd2:/gbadumper/settings.ini\nsplit_folders=1\n");
	storage_mkdirs("sd2:/gbadumper");
	write_file("sd2:/gbadumper/settings.ini", "settings_path=sda:/loop/settings.ini\n");
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_EQUAL_STRING("sda:/loop/settings.ini", settings_file());
	TEST_ASSERT_TRUE(s->split_folders);
}

// a pointer to itself is just a settings file
static void test_pointer_to_itself(void)
{
	storage_mkdirs("sd2:/gbadumper");
	write_file("sd2:/gbadumper/settings.ini", "settings_path=sd2:/gbadumper/settings.ini\nsplit_folders=1\n");
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_EQUAL_STRING("sd2:/gbadumper/settings.ini", settings_file());
	TEST_ASSERT_TRUE(s->split_folders);
}

// a move to a folder that can't be created fails and keeps the old place
static void test_move_to_a_folder_that_cannot_be_made(void)
{
	save_default_settings();
	write_file("sd2:/blocked", "");
	TEST_ASSERT_FALSE(settings_move("sd2:/blocked/cfg"));
	TEST_ASSERT_EQUAL_STRING("sd2:/gbadumper/settings.ini", settings_file());
}

// the settings move, but the pointer back can't be written: that fails too,
// the next start wouldn't find them
static void test_move_fails_if_the_pointer_cannot_be_written(void)
{
	save_default_settings();
	remove("sd2:/gbadumper/settings.ini");
	mkdir("sd2:/gbadumper/settings.ini", 0777);
	TEST_ASSERT_FALSE(settings_move("sda:/moved"));
}

// chosen device missing: auto is used instead
static void test_missing_device_falls_back(void)
{
	strcpy(s->device, "sdb");
	TEST_ASSERT_FALSE(settings_apply());
	TEST_ASSERT_EQUAL_STRING("sd2", active_id());
}

static void test_existing_file_settings(void)
{
	TEST_ASSERT_TRUE(storage_mkdirs("sd2:/gba/dumps/ROMs"));
	write_file("sd2:/gba/dumps/ROMs/x.gba", "");
	strcpy(p, "sd2:/gba/dumps/ROMs/x.gba");
	TEST_ASSERT_FALSE(paths_resolve_existing(p, sizeof(p), PATHS_EXISTING_SKIP));
	TEST_ASSERT_TRUE(paths_resolve_existing(p, sizeof(p), PATHS_EXISTING_OVERWRITE));
	TEST_ASSERT_EQUAL_STRING("sd2:/gba/dumps/ROMs/x.gba", p);
	TEST_ASSERT_TRUE(paths_resolve_existing(p, sizeof(p), PATHS_EXISTING_KEEP_BOTH));
	TEST_ASSERT_EQUAL_STRING("sd2:/gba/dumps/ROMs/x (1).gba", p);
	write_file(p, "");
	strcpy(p, "sd2:/gba/dumps/ROMs/x.gba");
	TEST_ASSERT_TRUE(paths_resolve_existing(p, sizeof(p), PATHS_EXISTING_KEEP_BOTH));
	TEST_ASSERT_EQUAL_STRING("sd2:/gba/dumps/ROMs/x (2).gba", p);
}

// ':' isn't allowed in FAT file names
static void test_cart_names_are_sanitised(void)
{
	paths_set_dump_dir("sd2:/gba/dumps");
	paths_set_split_folders(true);
	TEST_ASSERT_TRUE(paths_cart_file(p, sizeof(p), &cart, PATHS_KIND_SAVE));
	TEST_ASSERT_EQUAL_STRING("sd2:/gba/dumps/Saves/POKEMON_EMER [BPEE01].sav", p);
}

static void test_bios_without_split_folders(void)
{
	paths_set_dump_dir("sd2:/gba/dumps");
	paths_set_split_folders(false);
	TEST_ASSERT_TRUE(paths_bios_file(p, sizeof(p)));
	TEST_ASSERT_EQUAL_STRING("sd2:/gba/dumps/gba_bios.bin", p);
}

static void test_device_root_as_dump_folder(void)
{
	paths_set_dump_dir("sd2:/");
	paths_set_split_folders(false);
	TEST_ASSERT_TRUE(paths_cart_file(p, sizeof(p), &cart, PATHS_KIND_ROM));
	TEST_ASSERT_EQUAL_STRING("sd2:/POKEMON_EMER [BPEE01].gba", p);
}

static void use_backup_folder(void)
{
	paths_set_dump_dir("sd2:/ts");
	paths_set_split_folders(true);
	TEST_ASSERT_TRUE(paths_create_dirs());
}

// save backup names with the date and time, or without
static void test_save_backup_names(void)
{
	use_backup_folder();
	struct tm when = { 0 };
	when.tm_year   = 2026 - 1900;
	when.tm_mon    = 8;
	when.tm_mday   = 29;
	when.tm_hour   = 18;
	when.tm_min    = 5;
	when.tm_sec    = 7;
	TEST_ASSERT_TRUE(paths_save_backup_file(p, sizeof(p), &cart, &when));
	TEST_ASSERT_EQUAL_STRING("sd2:/ts/Saves/POKEMON_EMER [BPEE01] 2026-09-29 18-05-07.sav", p);
	TEST_ASSERT_TRUE(paths_save_backup_file(p, sizeof(p), &cart, NULL));
	TEST_ASSERT_EQUAL_STRING("sd2:/ts/Saves/POKEMON_EMER [BPEE01].sav", p);
}

static const char *backups[] = {
	"sd2:/ts/Saves/POKEMON_EMER [BPEE01].sav", // plain, from older versions
	"sd2:/ts/Saves/POKEMON_EMER [BPEE01] 2026-09-28 10-00-00.sav",
	"sd2:/ts/Saves/POKEMON_EMER [BPEE01] (1).sav", // keep both
	"sd2:/ts/Saves/POKEMON_EMER [BPEE01] 2026-09-29 18-05-07.sav",
	"sd2:/ts/Saves/POKEMON_EMER [BPEE02] 2030-01-01 00-00-00.sav", // other game (maker), newer
	"sd2:/ts/Saves/POKEMON_EMERALD.sav",                           // not ours
	"sd2:/ts/Saves/POKEMON_EMER [BPEE01].txt",                     // not a save
};
#define BACKUP_COUNT (int)(sizeof(backups) / sizeof(backups[0]))

static void set_mtime(const char *path, int seconds)
{
	struct utimbuf t = { 1790000000 + seconds, 1790000000 + seconds };
	utime(path, &t);
}

// the files above, modified in that order
static void make_backups(void)
{
	static const int mtimes[] = { 100, 200, 300, 400, 900, 950, 999 };
	use_backup_folder();
	for(int k = 0; k < BACKUP_COUNT; k++)
	{
		write_file(backups[k], "");
		set_mtime(backups[k], mtimes[k]);
	}
}

static void test_latest_save_none_yet(void)
{
	use_backup_folder();
	TEST_ASSERT_FALSE(paths_find_latest_save(p, sizeof(p), &cart));
}

// the newest by modification time, whatever the name style
static void test_latest_save_is_the_newest(void)
{
	make_backups();
	TEST_ASSERT_TRUE(paths_find_latest_save(p, sizeof(p), &cart));
	TEST_ASSERT_EQUAL_STRING(backups[3], p);
}

// an older timestamped name that was modified later (e.g. copied back) wins
static void test_latest_save_goes_by_modification_time(void)
{
	make_backups();
	set_mtime(backups[1], 500);
	TEST_ASSERT_TRUE(paths_find_latest_save(p, sizeof(p), &cart));
	TEST_ASSERT_EQUAL_STRING(backups[1], p);
}

// same time: the later name wins, so timestamped names order by date
static void test_latest_save_same_time_goes_by_name(void)
{
	make_backups();
	set_mtime(backups[1], 500);
	set_mtime(backups[3], 500);
	TEST_ASSERT_TRUE(paths_find_latest_save(p, sizeof(p), &cart));
	TEST_ASSERT_EQUAL_STRING(backups[3], p);
}

// only the old plain backup: still found for restore
static void test_latest_save_finds_an_old_plain_backup(void)
{
	make_backups();
	for(int k = 1; k < 4; k++)
	{
		remove(backups[k]);
	}
	TEST_ASSERT_TRUE(paths_find_latest_save(p, sizeof(p), &cart));
	TEST_ASSERT_EQUAL_STRING(backups[0], p);
}

// the save_names setting, timestamp by default
static void test_save_names_setting(void)
{
	settings_defaults(s);
	TEST_ASSERT_TRUE(s->timestamp_saves);
	s->timestamp_saves = false;
	TEST_ASSERT_TRUE(settings_save());
	settings_defaults(s);
	TEST_ASSERT_TRUE(settings_load(NULL));
	TEST_ASSERT_FALSE(s->timestamp_saves);
	TEST_ASSERT_TRUE(file_has(settings_file(), "save_names=plain"));
}

int main(void)
{
	if(!getcwd(run_dir, sizeof(run_dir)))
	{
		return 1;
	}
	UNITY_BEGIN();
	RUN_TEST(test_ini_parser);
	RUN_TEST(test_ini_byte_order_mark_and_cr_line_endings);
	RUN_TEST(test_ini_files);
	RUN_TEST(test_first_run);
	RUN_TEST(test_save_and_load);
	RUN_TEST(test_settings_next_to_the_dol_win);
	RUN_TEST(test_move_leaves_a_pointer);
	RUN_TEST(test_second_move_updates_both_pointers);
	RUN_TEST(test_move_back_to_the_default_folder);
	RUN_TEST(test_pointer_loop_is_followed_once);
	RUN_TEST(test_pointer_to_itself);
	RUN_TEST(test_move_to_a_folder_that_cannot_be_made);
	RUN_TEST(test_move_fails_if_the_pointer_cannot_be_written);
	RUN_TEST(test_missing_device_falls_back);
	RUN_TEST(test_existing_file_settings);
	RUN_TEST(test_cart_names_are_sanitised);
	RUN_TEST(test_bios_without_split_folders);
	RUN_TEST(test_device_root_as_dump_folder);
	RUN_TEST(test_save_backup_names);
	RUN_TEST(test_latest_save_none_yet);
	RUN_TEST(test_latest_save_is_the_newest);
	RUN_TEST(test_latest_save_goes_by_modification_time);
	RUN_TEST(test_latest_save_same_time_goes_by_name);
	RUN_TEST(test_latest_save_finds_an_old_plain_backup);
	RUN_TEST(test_save_names_setting);
	return UNITY_END();
}
