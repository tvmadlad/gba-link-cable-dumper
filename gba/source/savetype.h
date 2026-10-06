/*
 * Copyright (C) 2016 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __SAVETYPE_H__
#define __SAVETYPE_H__

#include <gba_types.h>

// which save chip a game uses, worked out from the ROM without touching the
// chip, so it can be tested on a host (see tests/test_savetype.c)

typedef enum
{
	SAVE_TYPE_NONE,
	SAVE_TYPE_EEPROM, // 512 B or 8 KB, see save_type_eeprom_size
	SAVE_TYPE_SRAM_32KB,
	SAVE_TYPE_FLASH_64KB,
	SAVE_TYPE_FLASH_128KB,
} save_type;

// looks for the save library ID string Nintendo's SDK builds into every game
// ("EEPROM_V", "SRAM_V", "FLASH_V", "FLASH512_V", "FLASH1M_V"), searching the
// ROM's 32 bit words backwards; the string has to start on a word
save_type save_type_find(const u32 *rom, u32 words);
// 512 B EEPROMs ignore the extra address bits of 8 KB style reads, so the
// first 2 KB read that way repeat their first 8 bytes; probe holds those 2 KB.
// A blank 8 KB EEPROM repeats too, and is taken for 512 B.
u32 save_type_eeprom_size(const u8 *probe);

#endif
