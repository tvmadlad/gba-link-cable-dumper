#---------------------------------------------------------------------------------
# Host tests, built with the normal Mac/Linux compiler (no devkitPro needed)
# and the Unity test framework in unity/
#   make -C tests            run all tests
#   make -C tests screens    render the GBA payload screens to build/screens/*.png
#                            (uses the libgba font, needs devkitARM for that)
#---------------------------------------------------------------------------------
CC		?=	cc
CFLAGS	:=	-std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Iunity
BUILD	:=	build
UNITY	:=	unity/unity.c $(wildcard unity/*.h)

SETTINGS_SRCS	:=	test_settings.c \
				../source/settings/settings.c ../source/settings/ini.c \
				../source/storage/paths.c ../source/storage/storage.c
GBA_SCREEN_SRCS	:=	test_gba_screen.c ../gba/source/screen.c
PATHS_SRCS		:=	test_paths.c ../source/storage/paths.c ../source/storage/storage.c \
				../source/app/folder_browser.c
STORAGE_SRCS	:=	test_storage.c ../source/storage/storage.c
MULTIBOOT_SRCS	:=	test_multiboot.c ../source/link/multiboot.c
SAVETYPE_SRCS	:=	test_savetype.c ../gba/source/savetype.c

DEVKITPRO	?=	/opt/devkitpro
DEVKITARM	?=	$(DEVKITPRO)/devkitARM
LIBGBA		?=	$(DEVKITPRO)/libgba
FONT		:=	$(BUILD)/amiga_fnt.bin

.PHONY: all screens clean

all: $(BUILD)/test_settings $(BUILD)/test_gba_screen $(BUILD)/test_paths \
		$(BUILD)/test_storage_gc $(BUILD)/test_storage_wii $(BUILD)/test_multiboot $(BUILD)/test_savetype
	@rm -rf $(BUILD)/run && mkdir -p $(BUILD)/run
	@echo "== settings, ini, paths, storage"
	@cd $(BUILD)/run && ../test_settings
	@echo "== gba screen"
	@$(BUILD)/test_gba_screen
	@rm -rf $(BUILD)/run_paths && mkdir -p $(BUILD)/run_paths
	@echo "== path helpers, new folders"
	@cd $(BUILD)/run_paths && ../test_paths
	@echo "== storage devices, GameCube"
	@$(BUILD)/test_storage_gc
	@echo "== storage devices, Wii"
	@$(BUILD)/test_storage_wii
	@echo "== multiboot upload"
	@$(BUILD)/test_multiboot
	@echo "== gba save types"
	@$(BUILD)/test_savetype

$(BUILD)/test_settings: $(SETTINGS_SRCS) $(wildcard stub/*.h stub/*/*.h ../source/*/*.h) $(UNITY)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Istub -I../source -I../common $(SETTINGS_SRCS) unity/unity.c -o $@

$(BUILD)/test_gba_screen: $(GBA_SCREEN_SRCS) $(wildcard gba_stub/*.h ../gba/source/*.h) $(UNITY)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Igba_stub -I../gba/source -I../common $(GBA_SCREEN_SRCS) unity/unity.c -o $@

$(BUILD)/test_paths: $(PATHS_SRCS) $(wildcard stub/*.h stub/*/*.h ../source/*/*.h) $(UNITY)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Istub -I../source -I../common $(PATHS_SRCS) unity/unity.c -o $@

# the storage device table, as on GameCube and as on Wii
$(BUILD)/test_storage_gc: $(STORAGE_SRCS) $(wildcard stub/*.h stub/*/*.h ../source/*/*.h) $(UNITY)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Istub -I../source -I../common $(STORAGE_SRCS) unity/unity.c -o $@

$(BUILD)/test_storage_wii: $(STORAGE_SRCS) $(wildcard stub/*.h stub/*/*.h ../source/*/*.h) $(UNITY)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DHW_RVL -Istub -I../source -I../common $(STORAGE_SRCS) unity/unity.c -o $@

$(BUILD)/test_multiboot: $(MULTIBOOT_SRCS) $(wildcard stub/*.h ../source/link/*.h) $(UNITY)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Istub -I../source -I../common $(MULTIBOOT_SRCS) unity/unity.c -o $@

$(BUILD)/test_savetype: $(SAVETYPE_SRCS) $(wildcard gba_stub/*.h ../gba/source/*.h) $(UNITY)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Igba_stub -I../gba/source -I../common $(SAVETYPE_SRCS) unity/unity.c -o $@

# the font consoleDemoInit uses, taken out of libgba
$(FONT):
	@mkdir -p $(BUILD)/font
	@cd $(BUILD)/font && $(DEVKITARM)/bin/arm-none-eabi-ar x $(LIBGBA)/lib/libgba.a amiga.fnt.o
	@$(DEVKITARM)/bin/arm-none-eabi-objcopy -O binary --only-section=.rodata.amiga_fnt $(BUILD)/font/amiga.fnt.o $@

screens: $(BUILD)/test_gba_screen $(FONT)
	@rm -rf $(BUILD)/screens && mkdir -p $(BUILD)/screens
	@$(BUILD)/test_gba_screen $(FONT) $(BUILD)/screens > /dev/null
	@python3 render_gba_screen.py $(BUILD)/screens/*.bin
	@rm $(BUILD)/screens/*.bin
	@ls $(BUILD)/screens

clean:
	@rm -rf $(BUILD)
