/*
 * Copyright (C) 2026 tvmadlad
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
/*
 * Host tests for the path helpers (source/storage/paths.c) and the folder
 * browser's new folder (source/app/folder_browser.c): device roots, going up
 * and into folders, and paths that don't fit coming back empty instead of
 * cut off. Device roots like "sd2:/" are plain folders in the run directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "unity.h"
#include "storage/storage.h"
#include "storage/paths.h"
#include "app/folder_browser.h"
#include "ui/ui.h"
#include "ui/input.h"

bool stub_present_sd2 = true, stub_present_sda = true;

// the folder browser's screen, not used by these tests
void ui_draw_menu(const char *title, const ui_menu_item *items, int count, int cursor, const char *help)
{
}
void ui_warn(const char *msg)
{
}
void ui_exit(void)
{
	abort();
}
void ui_frame(void)
{
}
void input_scan(void)
{
}
u32 input_down(void)
{
	return 0;
}

static gba_cart_info cart;
static char          big[256], tiny[24];
static char         *exact; // sized to fit exactly, so the sanitizers catch any overrun

// dumps go into sd2:/dumps, split into folders
void setUp(void)
{
	mkdir("sd2:", 0777);
	paths_set_dump_dir("sd2:/dumps");
	paths_set_split_folders(true);
	memset(&cart, 0, sizeof(cart));
	memcpy(cart.header + 0xA0, "POKEMON EMER", 12);
	memcpy(cart.header + 0xAC, "BPEE", 4);
	memcpy(cart.header + 0xB0, "01", 2);
}

void tearDown(void)
{
	free(exact);
	exact = NULL;
}

static void test_device_roots(void)
{
	TEST_ASSERT_TRUE(paths_is_device_root("sd2:/"));
	TEST_ASSERT_TRUE(paths_is_device_root("usb:/"));
	TEST_ASSERT_FALSE(paths_is_device_root("sd2:"));
	TEST_ASSERT_FALSE(paths_is_device_root("sd2:/a"));
	TEST_ASSERT_FALSE(paths_is_device_root("/"));
	TEST_ASSERT_FALSE(paths_is_device_root(""));
}

static void test_going_up_stops_at_the_device_root(void)
{
	char p[64] = "sd2:/a/b";
	paths_parent(p);
	TEST_ASSERT_EQUAL_STRING("sd2:/a", p);
	paths_parent(p);
	TEST_ASSERT_EQUAL_STRING("sd2:/", p);
	paths_parent(p);
	TEST_ASSERT_EQUAL_STRING("sd2:/", p);
}

static void test_going_up_from_a_name_without_a_device(void)
{
	char p[64] = "plain";
	paths_parent(p);
	TEST_ASSERT_EQUAL_STRING("plain", p);
}

// going into folders, from a device root and below it
static void test_going_into_folders(void)
{
	char p[64] = "sd2:/";
	TEST_ASSERT_TRUE(paths_join(p, sizeof(p), "dumps"));
	TEST_ASSERT_EQUAL_STRING("sd2:/dumps", p);
	TEST_ASSERT_TRUE(paths_join(p, sizeof(p), "Saves"));
	TEST_ASSERT_EQUAL_STRING("sd2:/dumps/Saves", p);
}

// "sd2:/dumps" + "/ROMs" needs exactly 16 bytes, "/Saves" one more
static void test_folder_that_does_not_fit_keeps_the_path(void)
{
	char small[16] = "sd2:/dumps";
	TEST_ASSERT_FALSE(paths_join(small, sizeof(small), "Saves"));
	TEST_ASSERT_EQUAL_STRING("sd2:/dumps", small);
	TEST_ASSERT_TRUE(paths_join(small, sizeof(small), "ROMs"));
	TEST_ASSERT_EQUAL_STRING("sd2:/dumps/ROMs", small);
}

static void test_cart_file_name(void)
{
	TEST_ASSERT_TRUE(paths_cart_file(big, sizeof(big), &cart, PATHS_KIND_ROM));
	TEST_ASSERT_EQUAL_STRING("sd2:/dumps/ROMs/POKEMON EMER [BPEE01].gba", big);
	size_t need = strlen(big) + 1;
	exact       = malloc(need);
	TEST_ASSERT_TRUE(paths_cart_file(exact, need, &cart, PATHS_KIND_ROM));
	TEST_ASSERT_EQUAL_STRING(big, exact);
}

// a file name that doesn't fit comes back empty instead of cut off
static void test_cart_file_that_does_not_fit_is_empty(void)
{
	TEST_ASSERT_TRUE(paths_cart_file(big, sizeof(big), &cart, PATHS_KIND_ROM));
	size_t need = strlen(big) + 1;
	exact       = malloc(need - 1);
	TEST_ASSERT_FALSE(paths_cart_file(exact, need - 1, &cart, PATHS_KIND_ROM));
	TEST_ASSERT_EQUAL_STRING("", exact);
	strcpy(tiny, "junk");
	TEST_ASSERT_FALSE(paths_cart_file(tiny, sizeof(tiny), &cart, PATHS_KIND_ROM));
	TEST_ASSERT_EQUAL_STRING("", tiny);
	// even the folder doesn't fit: never a name without its folder and device
	char folder_only[8] = "junk";
	TEST_ASSERT_FALSE(paths_cart_file(folder_only, sizeof(folder_only), &cart, PATHS_KIND_ROM));
	TEST_ASSERT_EQUAL_STRING("", folder_only);
}

static void test_save_backup_file_name(void)
{
	struct tm when;
	memset(&when, 0, sizeof(when));
	when.tm_year = 2026 - 1900;
	when.tm_mon  = 9;
	when.tm_mday = 6;
	when.tm_hour = 12;
	TEST_ASSERT_TRUE(paths_save_backup_file(big, sizeof(big), &cart, &when));
	TEST_ASSERT_EQUAL_STRING("sd2:/dumps/Saves/POKEMON EMER [BPEE01] 2026-10-06 12-00-00.sav", big);
	size_t need = strlen(big);
	TEST_ASSERT_FALSE(paths_save_backup_file(big, need, &cart, &when));
	TEST_ASSERT_EQUAL_STRING("", big);
}

static void test_bios_file_name(void)
{
	TEST_ASSERT_TRUE(paths_bios_file(big, sizeof(big)));
	TEST_ASSERT_EQUAL_STRING("sd2:/dumps/BIOS/gba_bios.bin", big);
	strcpy(tiny, "junk");
	TEST_ASSERT_FALSE(paths_bios_file(tiny, sizeof(tiny)));
	TEST_ASSERT_EQUAL_STRING("", tiny);
}

// nothing is written to an empty path, whatever the setting
static void test_empty_path_is_refused(void)
{
	char none[8] = "";
	TEST_ASSERT_FALSE(paths_resolve_existing(none, sizeof(none), PATHS_EXISTING_OVERWRITE));
	TEST_ASSERT_FALSE(paths_resolve_existing(none, sizeof(none), PATHS_EXISTING_KEEP_BOTH));
}

// keep both: a "name (n)" that doesn't fit is refused
static void test_keep_both_name_that_does_not_fit_is_refused(void)
{
	TEST_ASSERT_TRUE(storage_mkdirs("sd2:/kb"));
	FILE *f = fopen("sd2:/kb/a.sav", "w");
	TEST_ASSERT_NOT_NULL(f);
	fclose(f);
	char kb[16] = "sd2:/kb/a.sav";
	TEST_ASSERT_FALSE(paths_resolve_existing(kb, sizeof(kb), PATHS_EXISTING_KEEP_BOTH));
	TEST_ASSERT_EQUAL_STRING("", kb);
	char kb_room[32] = "sd2:/kb/a.sav";
	TEST_ASSERT_TRUE(paths_resolve_existing(kb_room, sizeof(kb_room), PATHS_EXISTING_KEEP_BOTH));
	TEST_ASSERT_EQUAL_STRING("sd2:/kb/a (1).sav", kb_room);
}

// a new folder each time, with the next free name
static void test_new_folder_takes_the_next_free_name(void)
{
	TEST_ASSERT_TRUE(storage_mkdirs("sd2:/nf"));
	strcpy(big, "sd2:/nf");
	TEST_ASSERT_TRUE(folder_browser_new_folder(big, sizeof(big)));
	TEST_ASSERT_EQUAL_STRING("sd2:/nf/New Folder", big);
	TEST_ASSERT_TRUE(storage_dir_exists(big));
	strcpy(big, "sd2:/nf");
	TEST_ASSERT_TRUE(folder_browser_new_folder(big, sizeof(big)));
	TEST_ASSERT_EQUAL_STRING("sd2:/nf/New Folder 2", big);
	TEST_ASSERT_TRUE(storage_dir_exists(big));
	strcpy(big, "sd2:/");
	TEST_ASSERT_TRUE(folder_browser_new_folder(big, sizeof(big)));
	TEST_ASSERT_EQUAL_STRING("sd2:/New Folder", big);
}

// too long for the caller's buffer: nothing is created and the path stays
static void test_new_folder_that_does_not_fit_makes_nothing(void)
{
	TEST_ASSERT_TRUE(storage_mkdirs("sd2:/nf2"));
	char nf[16] = "sd2:/nf2";
	TEST_ASSERT_FALSE(folder_browser_new_folder(nf, sizeof(nf)));
	TEST_ASSERT_EQUAL_STRING("sd2:/nf2", nf);
	TEST_ASSERT_FALSE(storage_dir_exists("sd2:/nf2/New Folder"));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_device_roots);
	RUN_TEST(test_going_up_stops_at_the_device_root);
	RUN_TEST(test_going_up_from_a_name_without_a_device);
	RUN_TEST(test_going_into_folders);
	RUN_TEST(test_folder_that_does_not_fit_keeps_the_path);
	RUN_TEST(test_cart_file_name);
	RUN_TEST(test_cart_file_that_does_not_fit_is_empty);
	RUN_TEST(test_save_backup_file_name);
	RUN_TEST(test_bios_file_name);
	RUN_TEST(test_empty_path_is_refused);
	RUN_TEST(test_keep_both_name_that_does_not_fit_is_refused);
	RUN_TEST(test_new_folder_takes_the_next_free_name);
	RUN_TEST(test_new_folder_that_does_not_fit_makes_nothing);
	return UNITY_END();
}
