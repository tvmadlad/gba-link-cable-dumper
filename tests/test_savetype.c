/*
 * Copyright (C) 2026 tvmadlad
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
/*
 * Host tests for the GBA payload's save type detection (gba/source/savetype.c),
 * with fake ROM images holding the save library ID strings Nintendo's SDK
 * builds into every game.
 */
#include <string.h>
#include "unity.h"
#include "savetype.h"

#define ROM_WORDS 0x400
static u32 rom[ROM_WORDS];
// 2 KB of EEPROM read the 8 KB way
static u8 probe[0x800];

// game data that happens not to contain an ID
static void fill_rom(void)
{
	u32 i;
	for(i = 0; i < ROM_WORDS; i++)
	{
		rom[i] = i * 0x9E3779B9;
	}
}

void setUp(void)
{
	fill_rom();
}

void tearDown(void)
{
}

static void place(u32 byte_at, const char *id)
{
	memcpy((u8 *)rom + byte_at, id, strlen(id));
}

static save_type find_at(const char *id, u32 byte_at)
{
	fill_rom();
	place(byte_at, id);
	return save_type_find(rom, ROM_WORDS);
}

// every ID the SDK uses, word aligned as in real ROMs
static void test_finds_every_sdk_id(void)
{
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_FLASH_128KB, find_at("FLASH1M_V103", 0x400));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_FLASH_64KB, find_at("FLASH512_V131", 0x400));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_FLASH_64KB, find_at("FLASH_V126", 0x400));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_SRAM_32KB, find_at("SRAM_V113", 0x400));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_SRAM_32KB, find_at("SRAM_F_V100", 0x400));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_EEPROM, find_at("EEPROM_V124", 0x400));
}

// nothing found, or only something close
static void test_no_id_or_a_near_miss_is_none(void)
{
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_NONE, save_type_find(rom, ROM_WORDS));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_NONE, find_at("FLASHX_V", 0x400));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_NONE, find_at("EEPROMV", 0x400));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_NONE, find_at("SRAMV", 0x400));
}

static void test_id_not_on_a_word_is_not_found(void)
{
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_NONE, find_at("FLASH1M_V103", 0x401));
}

static void test_ids_in_the_first_and_last_words(void)
{
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_FLASH_128KB, find_at("FLASH1M_V103", 0));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_EEPROM, find_at("EEPROM_V", (ROM_WORDS - 2) * 4));
}

// an ID in the very last word is cut off, nothing past the end is read
static void test_nothing_past_the_end_is_read(void)
{
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_NONE, find_at("SRAM", (ROM_WORDS - 1) * 4));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_NONE, save_type_find(rom, 1));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_NONE, save_type_find(rom, 0));
}

// the search runs backwards, so the ID nearest the end wins
static void test_id_nearest_the_end_wins(void)
{
	place(0x400, "SRAM_V113");
	place(0x800, "FLASH1M_V103");
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_FLASH_128KB, save_type_find(rom, ROM_WORDS));
	TEST_ASSERT_EQUAL_INT(SAVE_TYPE_SRAM_32KB, save_type_find(rom, 0x800 / 4));
}

// a 512 B chip answers every one of those reads with its first 8 bytes
static void test_eeprom_512b_repeats_its_first_8_bytes(void)
{
	u32 i;
	for(i = 0; i < sizeof(probe); i += 8)
	{
		memcpy(probe + i, "\x12\x34\x56\x78\x9A\xBC\xDE\xF0", 8);
	}
	TEST_ASSERT_EQUAL_HEX32(0x200, save_type_eeprom_size(probe));
}

static void test_eeprom_8kb_with_a_game_saved(void)
{
	u32 i;
	for(i = 0; i < sizeof(probe); i++)
	{
		probe[i] = i * 7;
	}
	TEST_ASSERT_EQUAL_HEX32(0x2000, save_type_eeprom_size(probe));
}

// one differing byte anywhere in the 2 KB is enough
static void test_eeprom_one_differing_byte_is_8kb(void)
{
	memset(probe, 0x5A, sizeof(probe));
	probe[sizeof(probe) - 1] ^= 1;
	TEST_ASSERT_EQUAL_HEX32(0x2000, save_type_eeprom_size(probe));
}

// known limitation: a blank or cleared 8 KB chip repeats as well, and is taken for 512 B
static void test_eeprom_blank_8kb_is_taken_for_512b(void)
{
	memset(probe, 0xFF, sizeof(probe));
	TEST_ASSERT_EQUAL_HEX32(0x200, save_type_eeprom_size(probe));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_finds_every_sdk_id);
	RUN_TEST(test_no_id_or_a_near_miss_is_none);
	RUN_TEST(test_id_not_on_a_word_is_not_found);
	RUN_TEST(test_ids_in_the_first_and_last_words);
	RUN_TEST(test_nothing_past_the_end_is_read);
	RUN_TEST(test_id_nearest_the_end_wins);
	RUN_TEST(test_eeprom_512b_repeats_its_first_8_bytes);
	RUN_TEST(test_eeprom_8kb_with_a_game_saved);
	RUN_TEST(test_eeprom_one_differing_byte_is_8kb);
	RUN_TEST(test_eeprom_blank_8kb_is_taken_for_512b);
	return UNITY_END();
}
