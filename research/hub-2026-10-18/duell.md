# DUELL 0.2.20-beta (oscillr/DUELL @ v0.2.20-beta, 0bd7f90)

Source: hubsrc/duell. Host build tried: hubbuild/duell.

## 1. License, tag, size
- `LICENSE` GPL-3.0 text; `NOTICE.md`: "Licence: GPL-3.0-only"; 202 `GPL-3.0-only` SPDX files, 1 Apache-2.0 (msfa), 2 MIT
  (the CHOMPI / Mutable plate reverb notices in `felucca-base/LICENSES/MIT-CHOMPI.txt`). The Felucca Assets section-7
  terms come via `felucca-base/LICENSING.md`. NOTICE: the names DUELL / OSCILL/R and the logo artwork are the author's;
  "builds that are not the official release must not present themselves as official DUELL releases". A plugin core
  should say "DUELL (unofficial build, commit X)".
- Tag v0.2.20-beta; `VERSION` file: 0.2.20; `UPSTREAM_COMMIT.txt`: Felucca 129a4cf (hugelton/Felucca main).
- 392 files, 205 C/H, ~102.8 k lines (`felucca-base/firmware/src` ~40 k .c, plus a large test suite).
- Beta: "Saves may change before the alpha", so the flash format is not stable between versions.

## 2. Lineage
- **Felucca, recent main** (NOTICE: "DUELL is a modified version of Felucca", upstream 129a4cf; the whole Felucca tree
  sits in `felucca-base/`). This is the newer Felucca with the AA UI (`ui_fonts.h`, `ui_icons.h`, `ui_keycaps.h`,
  `ui_palettes.h`), the autosave, and the web emulator. It is not SLOOP or Dinghy. Credits: JIANT reverbs, CHOMPI plate
  (Mutable Rings / Clouds), Cooley-Tukey FFT.
- Two parts (DUEL / STEREO / LAYER), MORPH, WAVE engine with user wavetables, 128 patch / 16 multi / 16 pattern slots.

## 3. Host builds
- **Felucca's own emulator unit**: `felucca-base/web/emu/felucca_web.c` (515 lines; Felucca's, "following X0X's browser
  build web/emu/x0x_web.c of charlesvestal/fm1-x0x"). It is built to WASM by `web/emu/build.sh` (Emscripten,
  `-sSTANDALONE_WASM`) and **natively** by `web/emu/native_check.c` (plain `cc`, compares the browser's samples with the
  host's). Hub / author emulator: oscillr.github.io/DUELL/emu.
  - Contents: `#include "../../tests/hostsim.c"` (sound side), the real `audio.c` ISR, the UI, `main.c`'s duties, and
    the storage on a **1 MiB NOR array** `nor[0x100000]`. Clock: `web_ms()` is one device ms (`fm1_ms++`, TIMER4 ticks
    at 24 MHz, the audio ISR for each half due, then `web_frame()` every 15 ms or `ui_input()` in between, as main.c's
    wait loop). Boot: `web_power_on()` (persist_boot, settings_init, splash, panel_init, felucca_init, audio_init, boot
    LEDs, autosave_boot). Exports: `web_boot`, `web_render(n)` (runs `web_ms` until n frames exist), `web_nor` /
    `web_nor_erase` / `web_flash_writes_count`, `web_buttons(label mask)`, `web_keys`, `web_enc(role, n)`,
    `web_master`, `web_midi`, `web_screen`, LED readers.
  - This is **almost the FM1VST hook set already**: tick = `web_ms` minus its audio part, frame / idle = its 15 ms split,
    audio = `fm1_alnk0_irq` per half, flash = `nor` (`flash_stage` = copy into `nor` before `web_boot`).
- Tests: `host_tests.sh` / `felucca-base/tests/run_tests.sh` (~60 programs: `ui_render.c`, `persistence_test.c`, ...).
- Panel: Felucca's labels (`panel.c`); LEDs are **multi-level**: `fm1_led`, `fm1_led_dim`, `fm1_led_breath`,
  `fm1_led_mid`, `fm1_led_scl`, `fm1_led_root`, `fm1_led_kout`, `fm1_led_ktop`, plus key level bytes and a pulse period
  and the power-on LED animation. `emu_hal_t`'s lit / dim pair must fold these. LCD 240x240 RGB565 (panel byte order).
- Not in the web unit: USB (MIDI out, USB audio, the editor), TRS MIDI, console, UBOOT. MIDI **in** is there
  (`web_midi`); MIDI out has to come from hostsim's `usb.c` queue (`midi_out_q`), as the Felucca glue does.

## 4. Build steps
- Generate (`felucca-base/tools/build.py generate()` or the root `gen_only.py`): `gen_version.py` (reads the root
  `VERSION`, so the source dir is `felucca-base` but the version file is one level up), `gen_aa_font.py` (Inter Tight
  variable TTF, Pillow + fontTools; Pillow with libraqm is why `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib`),
  `gen_aa_icons.py`, `gen_aa_keycaps.py`, `gen_ui_palettes.py`, `gen_splash_logo.py` (SVG -> mask),
  `gen_tables.py`, `gen_fm6_patches.py`, `gen_duell_sounds.py`, `gen_duell_multis.py` (`tools/factory_multis/*.bin`).
  It took 2.3 s, offline.
- GEN for `fm1_add_core`: `duell_version.h ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h splash_logo.h
  felucca_tables.h felucca_fm6.h duell_sounds.h duell_multis.h`. Version: `build/gen/duell_version.h`
  `DUELL_VERSION_STR` ("DUELL 0.2.20 beta"), or the root `VERSION`.
- `DUELL_TEST_MODE` must stay 0 (release), as must `FELUCCA_OTA` / `UAC` / `UART` (the web unit's defaults).

## 5. Blockers for a native macOS build
- None found: `native_check.c` built with Apple clang (`-ffp-contract=off -w`) in 5.8 s. The section attributes are
  dropped by hostsim's `#define __attribute__(x)` (the unit restores it for its `EXPORT`).
- `-ffp-contract=off` matters for the browser-vs-native match (there is some float in the newer Felucca).
- No CPU1 in this tree. Asm only in the loader and compiler barriers.

## 6. Persistence and audio
- 1 MiB NOR (`nor[]`, storage.c's hooks), Felucca object store (4 KiB sectors, 256 B header, A/B). DUELL map
  (`storage.c`): settings `0xFC000`; MULTI M01..M08 `0x97000..0x9EFFF`; user wavetables `0xA0000..0xA5FFF`; PATCH chunks
  6..15 `0xA6000..0xB9FFF`; free `0xBA000..0xC7FFF`; PATCH store `0xC8000..0xCDFFF`; MULTI M09..M16
  `0xCE000..0xD5FFF`; pattern slots `0xD6000..0xDBFFF`; patch chunks 3-4 `0xDC000..0xDFFFF`; chunk 5 `0x9F000` /
  `0xFE000`; OTA `0xE0000..0xE4FFF`; autosave `0xE5000` / `0xE6000`; FL_VBANK `0xE7000`.
- Audio: 44.1 kHz, `HALF_FRAMES` 128 (= EMU_BLOCK), stereo, 24-bit (`/ 8388608`).

## 7. Host sim tried
- `gen_only.py` (with the framework Python 3.11) ran in 2.3 s. `native_check` built in 5.8 s and ran in 0.14 s,
  rendering the emulator test's 2 s "heavy song" (88200 frames, peak 0.28 FS, non-silent). Works headless.

## 8. FM1VST glue
- Closest: **felucca** in purpose, but better: base the glue on `felucca_web.c` itself. Include it in the core unit
  (`#define main` is already handled; its `static` `web_ms` / `web_frame` / `web_power_on` are reachable from the same
  TU), split `web_ms` into tick / frame / idle and call `fm1_alnk0_irq` from `audio()`, map `nor` as the flash image,
  fold the LED levels, and take MIDI out from `midi_out_q`.
- Effort: **S-M**.
- Risks: beta save format (plugin states may not survive DUELL updates); the multi-level LED model and the boot LED
  animation; the naming condition in NOTICE; the ~0.3 FS level; the large test-mode multis must stay out.
- Family note: hubsrc/felucca (Felucca 1.6.1) has the same `web/emu/felucca_web.c`. One "felucca_web glue" would serve
  current Felucca, DUELL and probably other recent Felucca forks (and x0x's `web/emu/x0x_web.c`, which it was modelled
  on), and would replace the hand-written `felucca_fw.c` approach for those.
