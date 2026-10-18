# SLOOP 2.7.1 (hub id: sloop)

Source: https://github.com/isod89/sloop-fm1 tag `v2.7.1` (4ec1ac8, 2026-10-16). Clone: `hubsrc/sloop`.
Not in FM1VST today.

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` (Felucca's early text): code **GPL-3.0-only**; fm6_core.c msfa
  Apache-2.0 (`LICENSES/Apache-2.0-msfa.txt` is the only file in LICENSES/).
- **Caveat: the "Felucca Assets"** - `assets/icons.png` (the icon atlas, compiled into the firmware by
  tools/gen_icons.py -> felucca_icons.h), docs/panel.jpg and the Hügelton Sample Pack are "(C) Hügelton Instruments,
  all rights reserved, terms to be published later", conveyed under a **GPL-3.0 section 7 additional permission**
  ("you follow the terms published for the assets"). A SLOOP core shipped in FM1VST would carry the icon atlas:
  check that permission covers redistribution inside a plugin, or build the core with replacement icons
  (LICENSING: "a firmware image built ... with replacement assets ... is entirely governed by the GPL").
  The sample pack is not in SLOOP's firmware (CC0 kit). Other third party: Terminus font (OFL), CrispyZebra
  (GPL-3.0, PHASE), DaisySP/Rings (MIT, PHYS), klattsch (design reference only), CC0 samples, the JieLi SDK
  blobs read at package time only (not in tree).
- 325 tracked files, ~43.3 k lines of C/H (the smallest here).

## 2. Lineage
An early Felucca fork (pre-1.0 era: the bitmap font `gen_font.py -> felucca_font.h`, `gen_icons.py` from
icons.png, not 1.0's AA fonts / keycaps / palettes), kept in step by hand-ports from Felucca 1.0 / 1.0.1 / 1.0.3 /
1.4 / 1.4.1 / 1.5 / 1.5.1 (README credits list each). Own: 3 synth parts + a drum machine (NPART 3), arranger /
song mode, punch, studio drums, recovery updater, splash logo. Dual core (2.7) from Andre Rezemini's SLOOP dualcore
branch (cpu1.c / cpu1.h over hal/fm1_cpu1.h, "the second core (X0X)" - Charles Vestal's work). Not Dinghy, not
ChoralRoot. JIANT takes its song system from SLOOP.

## 3. Host builds
- **Native**: `tests/hostsim.c` (916 lines: DSP + sequencer render to WAV; includes `tests/cpu1_host.c`, the
  second core as an inline double (CORES=1, default) or a pthread (CORES=2)); `tests/ui_pages_test.c` (1.7 k lines:
  gfx, panel, ui, ui_song, ui_studio, icons, ui_draw, ui_vis, ui_layers, ui_menu, ui_input, splash, storage,
  editor_live over stubs; writes PPM screenshots); ~45 more tests (`tests/run_tests.sh`).
- **WASM**: only a **DSP preview** for the web editor: `firmware/host/preview.c` (170 lines: init / track / param /
  global / fm6 / note / render exports), built by `tools/build_preview.py` (emcc, or clang + wasm-ld with
  tools/wasm_shim), played by `web/preview.js` + `web/preview-worklet.js`; `tests/preview_native.c` builds it
  natively. **No panel / UI emulator** in the repo (the hub's player, if it has one for SLOOP, is not from here).
- HAL seen by the UI (ui_pages_test stubs): `fm1_ticks` (24 MHz ticks: `FM1_TICKS_PER_US 24`), `fm1_enc_take`,
  `fm1_input_edges(int)`, `fm1_input_note_edges`, `fm1_wdt_feed`, `fm1_adc_read`, `lcd_fill/lcd_blit/lcd_sync`;
  LEDs `fm1_led[11]`, `fm1_led_dim[11]` (glow), **`fm1_led_bg[11]` + `fm1_led_bg_ns`** (the backlight layer, menu
  LIGHTS), `fm1_led_key/btn` helpers. Same 11 x 6 matrix and keymap as Felucca.
- Device loop: firmware/src/main.c (410 lines): uac_service, usb_poll, uart_midi_poll in the timer; loop with
  ed_service, ota_service, persist_flush_now, ui_input, ui_leds, ui_draw, ui_input until the frame time.

## 4. Build steps
- `tools/build.py generate()`: felucca_font.h, felucca_icons.h (from assets/icons.png + icons.json),
  felucca_tables.h, felucca_samples.h, **felucca_drumkits.h** (gen_drumkits.py: 37 kits), felucca_fm6.h,
  **sloop_logo.h** (gen_logo.py: splash). Python 3 + Pillow (fontTools for the Terminus font). Assets in repo.
  Nothing fetched (the SDK blobs are only for the device package).

## 5. Blockers for a native macOS build
- None for the code (no inline asm in firmware/src; asm only in hal/*.S).
- **HALF_FRAMES 256** (5.8 ms halves), not 128: the ABI's audio() hands 128-frame blocks; the glue must render a
  256-frame half every second block (a FIFO: +128 frames latency) or the host calls audio in 256s.
- Dual core: `FELUCCA_CPU1 1`; on the host use the cpu1_host.c pattern inline (no thread) or `cpu1_run` -> 0.
- `#define __attribute__(x)` in hostsim (as Felucca).

## 6. Persistence, audio
- 1 MiB NOR, Felucca's A/B commit records, **SLOOP's own map**: settings 0xFC000 / 0xFD000; projects
  0x97000..0x9EFFF; user sample slots 0xA0000..0xDBFFF; user preset banks 0xDC000..0xDFFFF; **the working project
  (autosave) A 0x9F000 / B 0xFE000**; **FM6 patch bank 0xE5000 / 0xE6000** (the reverse of Felucca 1.6's use of
  those sectors: an image is per-firmware, never shared between cores).
- Audio 44.1 kHz, HALF_FRAMES 256, stereo, NPART 3 synth parts + drums, USB audio in (UAC, off in a core).

## 7. Native try (done)
- generate: OK.
- `cc -O2 -w -Ibuild/gen -Ifirmware/src tests/hostsim.c -lm`: 1.7 s; `hostsim 0 0 1 render.wav` ran headless
  (0.16 s, 353 KB WAV).
- `tests/ui_pages_test.c` (+ `-Ifirmware/hal`): 4.8 s; ran headless in 0.9 s: "... 20000-frame fuzz PASS", 60+
  PPM screens (e.g. `hubout/sloop.png`: the 4-track live page).

## 8. FM1VST: closest glue, effort, risks
Closest: **core-glue/felucca (the 1.0.1 hostsim-based glue)** - SLOOP has no web unit and its main.c is not
host-buildable, so the glue is the same shape: felucca_firmware.h-style include list (from ui_pages_test.c's
list, plus arranger, punch, upreset, project, console off, recovery off, splash), a HAL header like felucca_hal.h
(+ fm1_led_bg plane, 24 MHz ticks, fm1_led_key helpers, cpu1 inline double), and an emu_fw.c re-implementing
main.c's boot and loop (sloop's own).
**Effort: M-L** (new include list and loop; HALF_FRAMES 256 buffering; LED backlight plane; a params map).
**Risks**: the asset licence (icons.png); the 256-frame half vs the ABI's 128 block; the re-implemented main loop
drifts with each SLOOP release (no upstream emulator to keep it honest); NPART 3 + drums differs from the
Felucca-family Tier 2 assumptions (parts 1..4).
