/*
 * Copyright (C) 2016 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <string.h>
#include "savetype.h"

// the ID strings as little endian words
#define ID_FLAS 0x53414C46 // "FLAS"
#define ID_H1M_ 0x5F4D3148 // "H1M_", FLASH1M_V
#define ID_H_   0x00005F48 // "H_", FLASH_V
#define ID_H512 0x32313548 // "H512", FLASH512_V
#define ID_EEPR 0x52504545 // "EEPR"
#define ID_OM_  0x005F4D4F // "OM_", EEPROM_V
#define ID_SRAM 0x4D415253 // "SRAM"
#define ID__    0x0000005F // "_", SRAM_V and SRAM_F_V

save_type save_type_find(const u32 *rom, u32 words)
{
	//every ID is longer than a word, so it can't start in the last one
	u32 x = words > 1 ? words - 1 : 0;
	while(x-- > 0)
	{
		u32 next = rom[x + 1];
		switch(rom[x])
		{
			case ID_FLAS:
				if(next == ID_H1M_)
				{
					return SAVE_TYPE_FLASH_128KB;
				}
				if((next & 0x0000FFFF) == ID_H_ || next == ID_H512)
				{
					return SAVE_TYPE_FLASH_64KB;
				}
				break;
			case ID_EEPR:
				if((next & 0x00FFFFFF) == ID_OM_)
				{
					return SAVE_TYPE_EEPROM;
				}
				break;
			case ID_SRAM:
				if((next & 0x000000FF) == ID__)
				{
					return SAVE_TYPE_SRAM_32KB;
				}
				break;
		}
	}
	return SAVE_TYPE_NONE;
}

u32 save_type_eeprom_size(const u8 *probe)
{
	u32 i;
	for(i = 8; i < 0x800; i += 8)
	{
		if(memcmp(probe, probe + i, 8) != 0)
		{
			return 0x2000;
		}
	}
	return 0x200;
}
