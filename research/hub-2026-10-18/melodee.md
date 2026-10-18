# Melodee 1.4.3 (hub id: melodee)

Source: https://github.com/keremimo/melodee tag `v1.4.3` (ba9c812, 2026-10-17). Clone: `hubsrc/melodee`.
FM1VST pins 0.11.1 (cores/melodee 9c271e2).

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md`: **GPL-3.0-only**, "a modified version of Felucca", Melodee code
  (C) Kerem Kilic (Ellic Studio), retained Felucca portions (C) Leo Kuroshita / Hügelton.
- Exceptions (`LICENSES/`): phys_dsp.c (DaisySP MIT), phys_symp.c (Rings MIT), **cz_native.c (MAME uPD933,
  BSD-3-Clause)**, fm6_core.c (msfa Apache-2.0), fonts InterTight + **Rubik** (OFL), Fukiai (MIT). The x0x 909 sources
  (`firmware/src/x0x/`) are GPL-3.0-only from charlesvestal/fm1-x0x. No MIT engines submodule (forked before it).
- 621 tracked files, ~84.6 k lines of C/H (a dense style: many one-line functions).

## 2. Lineage
Felucca 1.0.x fork (README: "follows Felucca 1.0's design"; same unit layout `firmware/src/melodee.c`, main.c,
storage.c, tests/hostsim.c, tests/ui_test.c). Grown far since 0.11: CZ-1 (uPD933), FM6, **Prophet (P5) engine**,
**SID drums**, **x0x 909 drums imported from Charles Vestal's fm1-x0x @201e5c5**, MPC pads, pattern banks,
recording, duplex USB audio, **dual core required** ("single-core packages are retired"), boot stub (sector 0x4000).

## 3. Host builds
- **Native**: `tests/hostsim.c` (979 lines: libc, engines, params, mod, voice, mix.c, usb.c, midi_uart, song_chain,
  seq; a constructor runs `dsp_tables_init()`), `tests/ui_test.c` (5.5 k lines: the UI files over stubs for time,
  input, LCD), ~100 more host tests (`tests/run_tests.sh`, flags `-w -Ibuild/gen -Ifirmware/src`, some with
  `-DMELODEE_LEGACY_EXTRAS=1`). No emulator, **no WASM / browser build** in the repo (web/ = installer, editor,
  manual only). The hub's player, if any, is not from this repo.
- HAL seen by the UI (ui_test.c stubs): `fm1_ticks`, `fm1_input_edges(int)`, `fm1_input_note_edges`, `fm1_enc_take`,
  `fm1_wdt_feed`, `fm1_irq_off/on`, `lcd_fill / lcd_blit / lcd_sync`; flash hooks st_read/st_erase/st_prog,
  `fl_jedec_ram`, `irq_save/restore`, `FL_FAR`, `fl_plain_window_init`.
- Device loop: main.c `fm1_main` (406 lines): boot (settings_init, lcd_init, dsp_tables_init, persist_boot,
  clock_boot = voice budget from `fm1_clock_mhz_of`, melodee_init, multicore start, audio_init, usb, timer5,
  glo_restore, project_boot), loop: pattern_poll, usb_retry, MASTER ADC IIR, OCT-/OCT+ UBOOT countdown,
  cz_service, fm6_service, ed_service/ota_service, ui_input, glo_poll, settings_poll, ui_leds, ui_draw, then
  ui_input until 15 ms. main.c itself is not host-buildable (no web unit); the glue re-implements it.
- Panel: as Felucca (14 buttons, 27 keys, 7 encoders + MASTER, LCD 240x240 RGB565 BE); LEDs `fm1_led[11]` +
  **three** dim planes `fm1_led_dim[3][11]` with duty masks `fm1_led_dim_mask[3]` (plane 2 = a breathing level,
  ui_input.c:471-475). `hal/fm1_led_pwm.h`, `fm1_eq.h` (hardware EQ) are new HAL files.

## 4. Build steps
- `tools/build.py generate()`: ui_fonts.h (`--preset rubik`), ui_icons.h, ui_keycaps.h, ui_palettes.h,
  melodee_tables.h, melodee_fm6.h, melodee_cz1.h, **melodee_prophet_factory.h** (new), after `gen_scales.py --check`.
- **Not in generate()**: `x0x_drum_tables.h` (tools/gen_909_tables.py; build.py runs it in build_app only). A core
  must run it as an extra generate step (fm1_add_core's GEN list + a second command).
- Python 3 + Pillow + fontTools. Assets in repo (Rubik/InterTight fonts, P5 factory data). No network.
  Generate took 2.3 s.

## 5. Blockers for a native macOS build
- None hard: only `__asm__ volatile("" ::: "memory")` (a compiler barrier, core.h RING_PUBLISH).
- **Dual core**: `MELODEE_DUAL_CORE` 1 by default (build_options.h), with a serial fallback when CPU1 is not
  online (`audio_worker_online` 0 on the host). Keep 1 with the worker offline, or build 0 (the single-core path is
  "retired" upstream: may bit-rot). No threads needed.
- x0x "fp32" math (fastmath.h, pi32v2-friendly) is plain C on the host.

## 6. Persistence, audio
- 1 MiB NOR. storage.c map: app 0x4000..0x92FFF; **Prophet user banks 0x93000..0x9CFFF**; shared extension
  0x9D000..0x9EFFF; FM6 patch bank A 0x9F000 / B 0xFE000; **bank projects 0xA0000..0xC7FFF** (two five-sector copies
  per slot); CZ banks 0xC8000..0xD7FFF; native FM6 0xD8000..; user preset banks 0xDC000..0xDFFFF; OTA staging
  0xE0000..0xE4FFF; extensions 0xE5000..0xFAFFF (expanded project copies 0xEA000..0xF9FFF); settings 0xFC000.
  Same "FELU" commit record; compact preset tag "CMP2". The 0.11 layout is not compatible (user flash images from
  the pinned core will be migrated or ignored by the firmware itself).
- Audio: 44.1 kHz, HALF_FRAMES 128, stereo, 4 parts (NPART 4), 8 voices; USB duplex audio (off in a core).

## 7. Native try (done)
- generate + gen_909_tables: OK.
- `cc -O1 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src tests/hostsim.c -lm`: built in 1.2 s; `hostsim 15 0 1
  render.wav` ran headless (0.1 s, 353 KB WAV).
- `tests/ui_test.c`: built in 5.3 s, ran headless in 0.8 s: "ui: all passed".
- No screen / panel run possible without a glue (no emulator upstream).

## 8. FM1VST: closest glue, effort, what breaks
Closest: **core-glue/melodee** (this firmware's own glue). Compiling the pinned glue against 1.4.3
(`gluetry/`, after gen + gen_909_tables):
- The firmware include list (melodee_firmware.h: hostsim.c + HAL + audio.c + UI + storage/upreset/project/cz_store/
  fm6_store) **still compiles**, and no internal function is left undefined (checked with -Wundefined-internal).
- **Silent memory corruption**: melodee_hal.h declares `fm1_led_dim[2][11]`, `fm1_led_dim_mask[2]`; 1.4.3 writes
  plane 2 (`fm1_led_dim[2][c]`, `fm1_led_dim_mask[2]`, ui_input.c:471-475). With `-w` this builds and overruns.
  Fix: 3 planes, and emu_led_sync folds plane 2 (breath) too.
- melodee_fw.c (7 errors): its copy of `melodee_init` uses `PATTERNS`, `load_pat16` (gone: pattern banks,
  `pattern_init`), and the splash fonts `AF_L/AF_S/AF_M` (now `TY_HR/TY_TX/TY_VL`). The frame body misses
  pattern_poll, cz_service, fm6_service, glo_poll, glo_restore, project_boot, dsp_tables_init (hostsim's
  constructor covers it), clock_boot (voice budget).
- core_melodee_params.c (64 errors): `P_DIST, P_CHOR, P_DLY, P_REV` gone (FX slots / sends reworked: `P_FXSEND2..`,
  `FX_SLOTS`), etc. The Tier 2 map needs re-deriving from 1.4.3's params.c.
- GEN list: + melodee_prophet_factory.h, + x0x_drum_tables.h (extra command). Version file unchanged
  (`firmware/src/melodee.c` MELODEE_VERSION "v1.4.3").
**Effort: M** (init/loop hooks + LED planes + params map; the include list survives).
**Risks**: no upstream emulator to diff against (behaviour of the re-implemented main loop is ours to keep in
step); dual-core-only upstream (serial fallback is a less-tested path); new flash layout; Prophet/SID/909 CPU cost
on the host is fine but the firmware's admission/voice-weight logic depends on `fm1_clock_mhz_of` (stub it to the
device clock or the budget changes); boot stub / sector 0x4000 logic is device-only.
