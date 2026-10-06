#ifndef __LIBSAVE_H__
#define __LIBSAVE_H__

#include <gba_types.h>

//---------------------------------------------------------------------------------
#ifdef __cplusplus
extern "C"
{
#endif
	//---------------------------------------------------------------------------------

	void GetSave_EEPROM_512B(u8 *data);
	void PutSave_EEPROM_512B(u8 *data);
	void GetSave_EEPROM_8KB(u8 *data);
	void PutSave_EEPROM_8KB(u8 *data);
	void GetSave_SRAM_32KB(u8 *data);
	void PutSave_SRAM_32KB(u8 *data);
	void GetSave_FLASH_64KB(u8 *data);
	void PutSave_FLASH_64KB(u8 *foo);
	void GetSave_FLASH_128KB(u8 *data);
	void PutSave_FLASH_128KB(u8 *foo);

	// the save size from the ROM's save library ID (savetype.c), 0 if there is
	// none; reads the EEPROM into data (8 KB) to tell its two sizes apart
	u32 SaveSize(u8 *data, s32 gamesize);

//---------------------------------------------------------------------------------
#ifdef __cplusplus
} // extern "C"
#endif
//---------------------------------------------------------------------------------

#endif
