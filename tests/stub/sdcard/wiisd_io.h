#pragma once
#include <fat.h>
extern bool stub_present_wiisd;
static bool wiisd_present(void)
{
	return stub_present_wiisd;
}
static bool wiisd_startup(void)
{
	return true;
}
static const DISC_INTERFACE __io_wiisd = { wiisd_startup, wiisd_present };
