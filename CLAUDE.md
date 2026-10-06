# CLAUDE.md

GameCube/Wii homebrew that dumps GBA ROMs, saves and BIOS over the GC–GBA link cable. It has two programs:
- **GC/Wii app** (`source/`, devkitPPC + libogc), built as `linkcabledump_gc.dol` and `linkcabledump_wii.dol`
- **GBA payload** (`gba/`, devkitARM + libgba), uploaded to the GBA over the cable (multiboot) and embedded in the dols

Planned work is in [docs/ROADMAP.md](docs/ROADMAP.md). Update its status markers when a feature lands.

## Build

```bash
./build.sh          # everything (GBA payload -> data/gba_mb.gba -> linkcabledump_{gc,wii}_v<version>.dol)
./build.sh gc       # GameCube only
./build.sh wii      # Wii only
./build.sh clean
make test           # host tests (settings, ini, paths, storage on GC and Wii, multiboot, GBA progress bar and save types) with the normal Mac compiler
make screens        # render the GBA payload screens to tests/build/screens/*.png (check layout after GBA UI changes)
make format         # format every C file with clang-format 23 (.clang-format); make format-check only checks
```

- The toolchain is in `/opt/devkitpro` (devkitPPC r47, libogc 2.13, devkitARM r66). `build.sh` sets `DEVKITPRO`/`DEVKITPPC`/`DEVKITARM` if they're unset.
- `data/` is generated. The top-level `Makefile` copies the GBA payload there, and bin2o turns it into `build_*/gba_mb_gba.h`.
- GC and Wii use separate build dirs (`build_gc/`, `build_wii/`). New source subdirectories must be added to `SOURCES` in **both** `Makefile.gc` and `Makefile.wii`. Object files are named by basename, so **source file names must be unique across directories**.
- A change is done when `./build.sh` passes with **no warnings** on both platforms, `make test` passes and `make format-check` passes.
- CI (`.github/workflows/build.yml`, GitHub Actions) runs `make test` and builds both dols on every push and pull request, and fails on any warning. Its devkitPro Docker images are pinned to the toolchain above, so bump their tags whenever the local toolchain is updated.
- Host tests in `tests/` compile the real sources against stub libogc headers in `tests/stub/`. They use Unity (`tests/unity/`, v2.7.0, kept as released and left out of `make format`): each behaviour is a `static void test_...(void)` with `TEST_ASSERT_*` checks, run from `main()` with `RUN_TEST()`, and `setUp()`/`tearDown()` run around every test so each one sets up its own state. A new test file needs a target in `tests/Makefile` that also compiles `unity/unity.c`. Device roots like `sd2:/` are plain folders in a temp dir, which works on macOS/Linux. Fakes stand in for hardware where the logic is worth testing: the stub drivers' present flags insert and pull cards, a fake SI link plays the GBA BIOS for multiboot, and GBA logic that only needs ROM data lives in its own file (`gba/source/savetype.c`). Add tests there for any new code that doesn't need hardware, and keep such code free of libogc-only calls where practical. The tests should also pass under `-fsanitize=address,undefined`.
- There's no hardware or emulator testing here. The GBA link cable can't be emulated, so say clearly that a change is untested on hardware. When refactoring the GBA side, compare `gba/gba_mb.gba` against the previous build to confirm it's byte-identical when no behaviour change is intended.

- Versioning: bump `VERSION_MINOR` (or `VERSION_MAJOR`) in `common/version.h` only. The code builds `APP_VERSION` ("v1.7") from it and the Makefiles read it for the output file names. Keep the two `#define VERSION_*` lines in that exact form, since the Makefiles and CI parse them with `sed`. Releases are only published from tags: pushing a `v<major>.<minor>` tag that matches `common/version.h` makes CI build that commit and publish the release with both dols. Pushes to master only build and test.
- `make dist` packages `dist/gba-link-cable-dumper-v<version>.zip` locally (GameCube dol, Wii `apps/gbadumper/` with `boot.dol` + `meta.xml` from `dist_files/meta.xml.in`, README, LICENSE). The CI release publishes the dols from the tag.

## Layout and rules

```
common/version.h    VERSION_MAJOR/VERSION_MINOR, the only place the version is set
common/protocol.h   commands/sizes shared by both programs
source/app/         screen flow; the only place that combines link + storage + ui
source/link/        SI transfers, multiboot upload, high-level GBA commands
source/settings/    settings.ini search/load/save/move, ini parser
source/storage/     device table + mounting, file helpers, output paths
source/ui/          ui.h (front end interface) + ui_console.c, input.h + input_pad.c
gba/source/         GBA payload (main.c) and libSave (save chip access)
```

- `link/` and `storage/` must **never** print or read input. Report progress with callbacks (`gba_progress_cb`, `gba_chunk_cb`).
- All on-screen output goes through `ui.h`, and all button reads go through `input.h` (`INPUT_*` bits, not `PAD_*`). This is what lets a GUI replace `ui_console.c`.
- Any change to the cable protocol must update **both** `source/link/gba_protocol.c` and `gba/source/main.c`, with constants in `common/protocol.h`. The payload is embedded, so both sides always ship together.
- Build file paths with `storage/paths.c` (it sanitises FAT-invalid characters and applies the ROMs/Saves/BIOS subfolders). Don't hard-code `/dumps` or a device name.
- Every path includes its device (`sd2:/dumps/...`). The device table is in `storage/storage.c`, split by `HW_RVL` (Wii) vs GameCube.
- Before writing a file, call `storage_check()` and `paths_resolve_existing()` (the existing-file setting). Do this **before** sending the command to the GBA so a failure doesn't leave the protocol half-way.
- Menus/lists are drawn with `ui_draw_menu()`. Screens own their state and input loop in `app/`.
- Check `snprintf` results when building paths. A cut-off path must fail, not point somewhere else.
- **GBA transfer timing:** the GC reads/sends each word without checking the GBA is ready (~345 µs, ~5,800 cycles per word). Anything inside a transfer loop in `gba/source/main.c` must be tiny: use `progress_update()` (inline compare, direct map writes), never `iprintf`/`siprintf` or division. Draw status text and call `progress_start()` **before** the handshake that starts a stream (the GC starts straight after it). After GBA screen changes, run `make screens` and look at the PNGs.
- **GBA button requests:** the GBA must never put a request in JOYTR without also setting `GBA_JSTAT_REQUEST`, and it must reset JOYTR to 0 **before** clearing that bit (the GC reads again as soon as the bit clears). Only poll with `gba_poll_request()` (status first, no side effects) in places where the GBA is idle or waiting for a choice. A `si_link_recv()` anywhere else starts the GBA's cart-info path.
- GBA screen text is written directly to the console map (`screen.c` `put_line`), not with `\x1b[` escape codes. Lines are 28 characters max (columns 2–29).
- GBA payload constraints: multiboot image ≤ 256 KB. Interrupts are disabled during transfers (`REG_IME = 0`), so don't rely on vblank IRQs there. The GBA side waits in busy loops on `REG_HS_CTRL`.
- SI timing: `SI_TRANS_DELAY` of 50 µs is the lowest value found to be reliable. Don't lower it without hardware testing.

## Code style

- C, `lower_snake_case` for new functions, prefixed by module (`gba_`, `si_link_`, `storage_`, `paths_`, `ui_`, `input_`)
- Formatting is clang-format 23 with the repo's `.clang-format`, run `make format` before committing (it skips the third-party `tests/unity/`). It indents with tabs and aligns with spaces, puts every brace on its own line, adds braces to every `if`/`else`/`for`/`while` body, keeps nothing squeezed onto one line, and lines up neighbouring defines, declarations, assignments, trailing comments and table rows. Install it with `pip install clang-format==23.1.3`; other versions can format differently.
- clang-format can't brace an empty loop body, so write busy-waits as `while(cond) {}` (`make format-check` rejects a lone `;`). It also can't move a data table's opening brace onto its own line, so that one stays after the `=`.
- Every source file starts with the MIT license header. Files containing FIX94's original code keep "Copyright (C) 2016 FIX94"; files written from scratch in this fork use "Copyright (C) 2026 tvmadlad" (see e.g. `source/settings/settings.c`). Don't change the owner of files derived from FIX94's code.
- Header guards look like `__MODULE_H__`
- Short `//` comments, only where the reason isn't obvious

## Git

- Branch for each feature, merge into `master` with `--no-ff`
- Push to the **`github`** remote (the public repo) and the **`gitea`** remote (private mirror). `origin` is FIX94's upstream repo, so never push there.
- The repo is public. Don't commit local network addresses, credentials or personal paths.
