#pragma once
#include <fat.h>
extern bool stub_present_usb;
static bool usb_present(void)
{
	return stub_present_usb;
}
static bool usb_startup(void)
{
	return true;
}
static const DISC_INTERFACE __io_usbstorage = { usb_startup, usb_present };
