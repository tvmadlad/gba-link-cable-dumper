/*
 * Copyright (C) 2026 tvmadlad
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
/*
 * Host tests for the GBA payload screen (gba/source/screen.c).
 * Builds the real source against tests/gba_stub, with VRAM and the palette
 * as plain arrays. Checks the screens and the progress bar maths and, when
 * run with an output folder, dumps each screen for render_gba_screen.py.
 */
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "gba.h"
#include "screen.h"

u8                 fake_vram[0x18000];
u16                fake_pal[256];
static const char *font_path = NULL;
static const char *out_dir   = NULL;

// mirrors libgba's consoleDemoInit: 1bpp font -> 4bpp tiles in charblock 0,
// map in screenblock 4, palette 15 colour 1 white
void consoleDemoInit(void)
{
	u8    font[2048] = { 0 };
	FILE *f          = font_path ? fopen(font_path, "rb") : NULL;
	if(f)
	{
		fread(font, 1, sizeof(font), f);
		fclose(f);
	}
	for(int c = 0; c < 256; c++)
	{
		for(int y = 0; y < 8; y++)
		{
			u32 row = 0;
			u8  b   = font[c * 8 + y];
			for(int x = 0; x < 8; x++)
			{
				if(b & (0x80 >> x))
				{
					row |= 1u << (x * 4);
				}
			}
			memcpy(fake_vram + c * 32 + y * 4, &row, 4);
		}
	}
	u16 *map = (u16 *)(fake_vram + 4 * 0x800);
	for(int i = 0; i < 32 * 32; i++)
	{
		map[i] = 0x20;
	}
	fake_pal[0]           = 0x51A7;
	fake_pal[15 * 16 + 1] = 0x7FFF;
}

static void dump(const char *name)
{
	if(!out_dir)
	{
		return;
	}
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", out_dir, name);
	FILE *f = fopen(path, "wb");
	if(!f)
	{
		return;
	}
	fwrite(fake_vram, 1, 0x3000, f);
	fwrite(fake_pal, 2, 256, f);
	fclose(f);
}

// the tile on screen at a row and column: the font's tiles are numbered by
// character, and tile 1 is a filled cell of the progress bar
static int tile_at(int row, int col)
{
	const u16 *map = (const u16 *)(fake_vram + 4 * 0x800);
	return map[row * 32 + col] & 0xFF;
}

static int pct_of(const progress_t *p)
{
	return p->pct[0] * 100 + p->pct[1] * 10 + p->pct[2];
}

// Pokemon Sapphire, as in the cartridge header
static u8 header[0xC0];

// every test starts on a fresh screen, as the payload does
void setUp(void)
{
	memset(header, 0, sizeof(header));
	memcpy(header + 0xA0, "POKEMON SAPP", 12);
	memcpy(header + 0xAC, "AXPE", 4);
	memcpy(header + 0xB0, "01", 2);
	header[0xBC] = 1;
	screen_init();
}

void tearDown(void)
{
}

// ready, a cartridge read, and the question before clearing its save
static void test_cart_screens(void)
{
	screen_controls("A: read cartridge", "SELECT: dump BIOS");
	dump("1_ready.bin");
	screen_cart(header, 16 << 20, 0x20000);
	screen_status("Choose on the GBA or TV");
	screen_controls("A: dump ROM    B: cancel", "R:backup L:restore SEL:clear");
	dump("2_cart.bin");
	screen_status("Press SELECT again to clear");
	dump("2b_confirm.bin");
	// "Title: POKEMON SAPP"
	TEST_ASSERT_EQUAL_CHAR('T', tile_at(6, 2));
	TEST_ASSERT_EQUAL_CHAR('P', tile_at(6, 9));
}

// a ROM dump part way and finished, then dismissed with a button: ready again,
// the bar gone, and the cartridge info kept as the last game
static void test_rom_dump_screens(void)
{
	progress_t p;
	u32        total = 16 << 20, i;
	screen_cart(header, total, 0x20000);
	screen_controls(NULL, NULL);
	screen_status("Dumping ROM...");
	progress_start(&p, total);
	for(i = 0; i < total * 45 / 100; i += 4)
	{
		progress_update(&p, i + 4);
	}
	dump("3_rom_45.bin");
	TEST_ASSERT_EQUAL_INT(44, pct_of(&p));
	TEST_ASSERT_EQUAL_INT(11, p.cells);
	for(; i < total; i += 4)
	{
		progress_update(&p, i + 4);
	}
	progress_finish(&p);
	screen_status_line("ROM dumped!");
	screen_controls(NULL, "Press any button to continue");
	dump("4_rom_done.bin");
	TEST_ASSERT_EQUAL_INT(1, tile_at(14, 2));
	TEST_ASSERT_EQUAL_INT(1, tile_at(14, 27));
	TEST_ASSERT_EQUAL_CHAR('C', tile_at(4, 2)); // "Cartridge:" heading

	screen_status("Ready");
	screen_cart_last();
	screen_controls("A: read cartridge", "SELECT: dump BIOS");
	dump("4b_last_game.bin");
	TEST_ASSERT_EQUAL_CHAR('L', tile_at(4, 2)); // "Last game:" heading
	TEST_ASSERT_EQUAL_CHAR('P', tile_at(6, 9));
	TEST_ASSERT_EQUAL_CHAR(' ', tile_at(14, 2));
}

// no cartridge after a transfer: no bar, and no "Last game:" heading without game info
static void test_no_cart_screen(void)
{
	progress_t p;
	screen_cart(header, 16 << 20, 0x20000);
	progress_start(&p, 0x200);
	progress_finish(&p);
	screen_cart(NULL, -1, 0);
	screen_cart_last();
	screen_status("Ready");
	screen_controls("A: read cartridge", "SELECT: dump BIOS");
	dump("5_no_cart.bin");
	TEST_ASSERT_EQUAL_CHAR(' ', tile_at(14, 2));
	TEST_ASSERT_EQUAL_CHAR(' ', tile_at(4, 2));
}

// steps like the payload's transfer loops and checks the display never runs
// ahead of the real progress or goes backwards, and only shows 100 when finished
static void check_size(u32 total)
{
	char what[24];
	snprintf(what, sizeof(what), "size 0x%X", (unsigned)total);
	progress_t p;
	progress_start(&p, total);
	int  last = 0, lastcells = 0;
	bool backwards = false, ahead = false, past_end = false;
	u32  i;
	for(i = 0; i < total; i += 4)
	{
		progress_update(&p, i + 4);
		int pct = pct_of(&p);
		if(pct < last || p.cells < lastcells)
		{
			backwards = true;
		}
		if((unsigned long long)pct * total > (unsigned long long)(i + 4) * 100 || (unsigned long long)p.cells * total > (unsigned long long)(i + 4) * 26)
		{
			ahead = true;
		}
		if(pct > 99 || p.cells > 26)
		{
			past_end = true;
		}
		last      = pct;
		lastcells = p.cells;
	}
	TEST_ASSERT_FALSE_MESSAGE(backwards, what);
	TEST_ASSERT_FALSE_MESSAGE(ahead, what);
	TEST_ASSERT_FALSE_MESSAGE(past_end, what);
	progress_finish(&p);
	TEST_ASSERT_EQUAL_INT_MESSAGE(100, pct_of(&p), what);
	TEST_ASSERT_EQUAL_INT_MESSAGE(26, p.cells, what);
}

// every save size, the BIOS and the smallest and largest ROM
static void test_progress_follows_the_transfer(void)
{
	static const u32 sizes[] = { 0x200, 0x2000, 0x4000, 0x8000, 0x10000, 0x20000, 1 << 20, 32 << 20 };
	unsigned         k;
	for(k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
	{
		check_size(sizes[k]);
	}
}

int main(int argc, char *argv[])
{
	if(argc > 1)
	{
		font_path = argv[1];
	}
	if(argc > 2)
	{
		out_dir = argv[2];
	}
	UNITY_BEGIN();
	RUN_TEST(test_cart_screens);
	RUN_TEST(test_rom_dump_screens);
	RUN_TEST(test_no_cart_screen);
	RUN_TEST(test_progress_follows_the_transfer);
	return UNITY_END();
}
