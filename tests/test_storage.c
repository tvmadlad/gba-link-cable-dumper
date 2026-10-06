/*
 * Copyright (C) 2026 tvmadlad
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
/*
 * Host tests for the storage device table (source/storage/storage.c), built
 * twice: as on GameCube, and as on Wii with HW_RVL. Cards go in and come out
 * through the stub drivers' present flags; SD Gecko slot B is never present.
 */
#include <string.h>
#include "unity.h"
#include "storage/storage.h"

bool stub_present_sd2, stub_present_sda;
#ifdef HW_RVL
bool               stub_present_wiisd, stub_present_usb;
static const char *order[] = { "sd", "usb", "sda", "sdb" };
static bool       *first = &stub_present_wiisd, *second = &stub_present_usb;
static const char *other_console = "sd2";
#else
static const char *order[] = { "sd2", "sda", "sdb" };
static bool       *first = &stub_present_sd2, *second = &stub_present_sda;
static const char *other_console = "usb";
#endif
#define ORDER_COUNT (int)(sizeof(order) / sizeof(order[0]))

// every card in, as at startup
void setUp(void)
{
	stub_present_sd2 = stub_present_sda = true;
#ifdef HW_RVL
	stub_present_wiisd = stub_present_usb = true;
#endif
	storage_init();
}

void tearDown(void)
{
}

static const char *active_id(void)
{
	return storage_active() ? storage_active()->id : "(none)";
}

// the devices in auto order, and none of the other console's
static void test_device_table_in_auto_order(void)
{
	TEST_ASSERT_EQUAL_INT(ORDER_COUNT, storage_device_count());
	int i;
	for(i = 0; i < ORDER_COUNT; i++)
	{
		TEST_ASSERT_EQUAL_STRING(order[i], storage_device_at(i)->id);
	}
	TEST_ASSERT_NULL(storage_device_at(ORDER_COUNT));
	TEST_ASSERT_NULL(storage_device_at(-1));
	TEST_ASSERT_NULL(storage_find_device(other_console));
}

// everything present: auto picks the first
static void test_auto_picks_the_first_device(void)
{
	TEST_ASSERT_TRUE(storage_init());
	TEST_ASSERT_EQUAL_STRING(order[0], active_id());
	TEST_ASSERT_FALSE(storage_find_device("sdb")->mounted);
}

// a chosen device, a missing one (auto is used and it says so) and auto
static void test_select_a_device(void)
{
	TEST_ASSERT_TRUE(storage_select("sda"));
	TEST_ASSERT_EQUAL_STRING("sda", active_id());
	TEST_ASSERT_FALSE(storage_select("sdb"));
	TEST_ASSERT_EQUAL_STRING(order[0], active_id());
	TEST_ASSERT_TRUE(storage_select(STORAGE_AUTO));
	TEST_ASSERT_EQUAL_STRING(order[0], active_id());
}

// the first device missing at startup: auto takes the next one
static void test_auto_skips_a_missing_first_device(void)
{
	*first = false;
	TEST_ASSERT_TRUE(storage_init());
	TEST_ASSERT_EQUAL_STRING(order[1], active_id());
}

// the card in use is pulled: checks fail until it's back, then it's mounted again
static void test_pulled_card_is_mounted_again(void)
{
	TEST_ASSERT_EQUAL_STRING(order[0], active_id());
	TEST_ASSERT_TRUE(storage_check());
	*first = false;
	TEST_ASSERT_FALSE(storage_check());
	TEST_ASSERT_FALSE(storage_find_device(order[0])->mounted);
	TEST_ASSERT_FALSE(storage_check());
	*first = true;
	TEST_ASSERT_TRUE(storage_check());
	TEST_ASSERT_TRUE(storage_find_device(order[0])->mounted);
}

static void test_nothing_present(void)
{
	*first = *second = false;
	stub_present_sda = false;
	TEST_ASSERT_FALSE(storage_init());
	TEST_ASSERT_NULL(storage_active());
	TEST_ASSERT_FALSE(storage_check());
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_device_table_in_auto_order);
	RUN_TEST(test_auto_picks_the_first_device);
	RUN_TEST(test_select_a_device);
	RUN_TEST(test_auto_skips_a_missing_first_device);
	RUN_TEST(test_pulled_card_is_mounted_again);
	RUN_TEST(test_nothing_present);
	return UNITY_END();
}
