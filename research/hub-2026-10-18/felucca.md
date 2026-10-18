# Felucca 1.6.1 (hub id: felucca)

Source: https://github.com/hugelton/Felucca tag `v1.6.1` (a53ecf4, 2026-10-15), plus the submodule
`firmware/engines` = https://github.com/hugelton/Felucca-Engines c46e008 (new since 1.0.x).
Clone: `hubsrc/felucca` (`git submodule update --init --depth 1` done).
FM1VST pins 1.0.1 (cores/Felucca 20c275e).

## 1. License, size
- `LICENSE` = GPL-3.0 text; `LICENSING.md`: **GPL-3.0-only** (SPDX headers). Copyright Leo Kuroshita / Hügelton Instruments.
- Exceptions (texts in `LICENSES/`): `firmware/engines/` (ANALOG, LOFI, TRIO, WHEEL, NOISE, dsp.c, felucca_engine.h) is
  **MIT** (Felucca-Engines, a git submodule); phys_dsp.c (DaisySP MIT), phys_symp.c (Rings MIT), fm6_core.c (msfa
  Apache-2.0); fonts InterTight (OFL), DotGothic16 (OFL, web emu), Fukiai (MIT). Whole firmware: GPL-3.0-only.
- Note for FM1VST: the GPL source offer now needs the submodule's files too (clone `--recurse-submodules`).
- 258 tracked files (+ engines submodule), ~80.6 k lines of C/H.

## 2. Lineage
The original (Hügelton Instruments, from scratch on the FM-1's JieLi BR25 / pi32v2). Every other firmware in this
group descends from some Felucca version. 1.6.1's web emulator credits the design of charlesvestal/fm1-x0x
(`web/emu/x0x_web.c`); its `tests/c1_host.h` credits Jangada (zednaked/jangada) for the host second core.

## 3. Host builds
- **Native**: `tests/hostsim.c` (DSP render: libc, engines, params, mod, voice, slicer, fx, usb.c's queues,
  midi_uart parser, song_chain, seq) + `tests/ui_test.c` (UI against stubs, 9.9 k lines) + ~60 other host tests
  (`tests/run_tests.sh`). `tests/c1_host.h`: the second core as a pthread (tests only).
- **WASM (new in 1.6.x)**: `web/emu/felucca_web.c` (499 lines) + `worklet.js` + `index.html`, built by
  `web/emu/build.sh` (emcc `-sSTANDALONE_WASM`, -> `build/emu/felucca.wasm`). It is a complete device unit:
  hostsim.c's sound side, then audio.c, the UI files, storage/upreset/project, **master.c and main.c themselves**
  (with HAL stubs: fm1_adc_read, fm1_timer5_*, fm1_reboot, fm1_enter_uboot, guard, CPU1, usb_* macros).
  `web/emu/native_check.c` builds the same unit natively with `cc` (the wasm-vs-native audio check).
- HAL / driving functions in felucca_web.c (all statics in one TU):
  - clock: `fm1_ticks()` = `web_ticks` (TIMER4, 24 ticks/us); `web_ms()` = one device ms: fm1_ms++, audio halves
    due (`fm1_alnk0_irq()` per HALF_FRAMES), then `web_frame()` every 15 ms (master_poll, ui_input, settings_poll,
    autosave_poll, ui_leds, ui_draw) or `ui_input()` in between; `web_render(n)` runs web_ms until n frames exist.
  - boot: `web_power_on()` = persist_boot, settings_init, usb_serial_apply, lcd_init, draw_splash, panel_init,
    felucca_init (main.c), master_boot, audio_init, boot_leds, autosave_boot; first frame at +430 ms.
  - input: `web_buttons(label mask)`, `web_keys(27-bit)`, `web_enc(role, n)`, `web_master(0..1023)`,
    `web_midi(status,d1,d2)` (channel messages only, midi_enqueue).
  - panel model: 14 labelled buttons (B_FX..B_OCTUP via panel.c), 27 keys, 7 encoders by role + MASTER ADC;
    LCD 240x240 RGB565 big-endian (`web_fb`, `lcd_fill/lcd_blit/lcd_power`); LEDs: `fm1_led`, `fm1_led_dim`,
    **`fm1_led_breath`, `fm1_led_mid`**, `fm1_led_dim_level()`, plus the power-on / idle LED animation
    (`hal/fm1_led_anim.h`, per-LED levels 0..64 via `web_anim_levels`), PLAY green LED.
  - flash: `nor[0x100000]` with st_read / st_erase / st_prog; `web_nor()` exported.
- Not in the web unit: USB (MIDI out, SysEx editor, OTA, UAC), TRS UART, CDC console, UBOOT / crash paths,
  battery (fixed 620), the second core (stubbed off).

## 4. Build steps
- `tools/build.py` `generate()` (same seven headers as 1.0.1: ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h
  felucca_tables.h felucca_fm6.h felucca_samples.h). Needs Python 3 + **Pillow + fontTools**; gen_tables.py imports
  `firmware/engines/tools/gen_engine_tables.py` (the submodule must be present). Assets in repo: InterTight font,
  icons.json, assets/samples-cc0 (FLUTE/PIANO/SAX). Nothing fetched at build time. Generate took 1.5 s.
- Include paths: `build/gen`, `firmware/src` (engines are reached as `../engines/src/*.c` from engines.c).

## 5. Blockers for a native macOS build
None found. No inline asm in firmware/src (asm only in hal/*.S, fm1_cpu1.S; not built on the host).
- Dual core: `FELUCCA_CPU2` defaults to 1 (1.6.1) but MENU > SYSTEM > 2ND CORE is OFF by default; fx.c
  mix_block uses `C1_ON()/C1_RUN/C1_WAIT`; felucca_web.c leaves them undefined (one core, `fm1_c1_on` = 0). A core
  for FM1VST must do the same (no threads: ABI threading contract) or run the job inline.
- Statics everywhere / one TU: globals once per loaded image (fine with the module model).
- `#define __attribute__(x)` in hostsim.c (glue must include fm1core.h first, as today).

## 6. Persistence, audio
- NOR 1 MiB image. Map (storage.c): settings 0xFC000; projects 0x97000..0x9EFFF; user sample slots 0xA0000..0xDBFFF
  (3 x 0x14000, read as XIP via `SMP_USER_XIP`); user preset banks 0xDC000..0xDFFFF; user-preset FM6 patches
  A 0x9F000 / B 0xFE000; OTA staging 0xE0000..0xE4FFF; **autosave (1.2) A 0xE5000 / B 0xE6000**; **FM6 voice bank
  (1.4.1) 0xE7000**. A/B sector pairs with a 32-byte commit record ("FELU").
- Audio: 44.1 kHz, HALF_FRAMES 128, stereo (24-bit in int32 `abuf`, OUT_SHIFT 7), 4 parts, 8 voices.

## 7. Native try (done)
- generate: OK (1.5 s, python3.11 with Pillow/fontTools).
- `web/emu/native_check.c` with Apple clang 21: built in 4.7 s, ran headless in 0.14 s, wrote 2 s of float audio.
- My harness `hubtools/webcheck.c` (includes felucca_web.c; boot, 1.5 s, screen to PPM, heavy song + PLAY, 2 s):
  boot draws 56, full screen non-black (HOME: part 1 ANALOG ACID, CUT/RES/ATK/REL cards), audio peak 0.327,
  RMS 0.087, playing 1. Screenshot: `hubout/felucca.png`.

## 8. FM1VST: closest glue, effort, what breaks
Closest: **core-glue/felucca** (it is this firmware), but the pinned glue does **not** compile against 1.6.1.
Tried (`gluetry/`, fm1core.h + the pinned glue + 1.6.1): errors in the firmware include list itself:
`lcd_wake_now`, `lcd_power` undeclared (ui.c), `MTAB_COUNT / MI_TAB / mtab_rows` undeclared (ui_menu.c needs the new
`menu_items.c`), and more past the error limit. What changed since 1.0.1 that hits the glue:
- felucca_firmware.h's include list: new `menu_items.c`, `master.c` (MASTER pot, `master_poll/master_boot`), and
  main.c is now buildable on the host (felucca_web.c includes it) - the glue's copied `emu_felucca_init` and its
  main-loop body (`emu_fw_frame`) are stale (autosave_boot/poll, boot_leds, kb_boot_hold, usb_serial_apply,
  draw_splash, master_poll).
- New HAL symbols: `lcd_power`, `lcd_wake_now`, `lcd_init`, `fm1_audio_stop`, `fm1_input_init/tick`,
  `fm1_led_breath[]`, `fm1_led_mid[]`, `fm1_led_dim_level()`, `fm1_led_anim_start/on/stop`, `fm1_led_idle_start/on`,
  `fm1_adc_read/init`, timer5 / reboot / uboot / guard / CPU1 stubs; `fm1_input_edges(&released)` now used.
- LEDs: two more planes (breath, mid) + the animation levels; emu_hal_t only has led / led_dim (ChoralRoot 0.25's
  emu_hooks.h adds led_mid / led_faint: breath -> mid or dim is a choice).
- Engines moved to `firmware/engines/` (submodule): FM1VST's `cores/Felucca` must be checked out recursively.
- Params map (core_felucca_params.c, 594 lines): written against 1.0.1's P_* / song.g ids; expect renames (the
  menu moved to menu_items.c, FX/master changes). Re-derive from 1.6.1's params.c / menu_items.c.
- Flash sync: autosave (project.c autosave_poll) now also writes; `flash_sync` should run settings_poll + autosave.
**Recommended approach**: rebase the core on `web/emu/felucca_web.c` (upstream-maintained, includes main.c): one
`core_felucca.c` that `#include`s it, splits `web_ms()` across tick/frame/idle/audio, maps emu_hal_t input to
`web_buttons/web_keys/web_enc/web_master`, copies `web_fb` -> hal.lcd and folds the LED planes, exposes `nor` as
the flash image (it is already RAM-only: fits ABI 2's staged boot). Adds back what the web unit lacks: MIDI out
(usb.c `midi_out_q`, set `usb.up`), UBOOT/exit, the erase stall.
**Effort: M** (glue rewrite on the web unit is mechanical; params map redo is most of the work).
**Risks**: params map drift; LED levels lost without an ABI change; the web unit is "what the browser has"
(no SysEx editor, no USB audio); fm1_ticks units differ (24 MHz ticks, not us); dual core must stay off.
