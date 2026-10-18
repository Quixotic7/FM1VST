# Rainbow Mode 1.1.5-rb (hub id: rainbow)

Source: https://github.com/joshbartlettband-web/felucca-rainbow `main` (c893b18, 2026-10-16; no tags; the firmware
says `FELUCCA_VERSION "v1.1.5"`). Clone: `hubsrc/rainbow`. Not in FM1VST today.

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` is Felucca 1.1.5's: **GPL-3.0-only**; DaisySP / Rings (MIT), msfa
  (Apache-2.0), InterTight (OFL), DotGothic16 (OFL, web emu), Fukiai (MIT), CC0 samples. No asset reservation.
- 270 tracked files, ~66.8 k lines of C/H (kid.c alone 3.7 k).

## 2. Lineage
**Felucca 1.1.5** fork (README: "this fork of Felucca", then Felucca's own README verbatim; the unit is
felucca.c). Adds Rainbow mode, a toddler toy that runs from power-up (`firmware/src/kid.c`, FELUCCA_KID: 94
"friends" with pictures, sounds, skies, beats), its art (`tools/gen_kid_art.py -> kid_art.h`), and an **SMK-37
port** (`BOARD_SMK37`, hal/smk_compat.h, smk_input.h, fm1_flash_map.h per board; SMK37.md). The web emulator
(`web/emu/`) is Felucca 1.6's (back-ported: same exports + kid extras).

## 3. Host builds
- **WASM + native**: `web/emu/felucca_web.c` (527 lines), worklet.js, index.html, build.sh (emcc), emu_test.mjs,
  kid_test.mjs, `native_check.c`. Same design as Felucca 1.6.1's unit (see felucca.md): hostsim.c sound side,
  audio.c, UI files + `kid.c`, storage/upreset/project, master.c, main.c; driven by `web_render(n)` -> `web_ms()`
  (device ms: audio halves, then `web_frame()` every 15 ms: master_poll, **kid_master / kid_frame** (kid mode can
  take the whole frame), ui_input, settings_poll, autosave_poll, ui_leds, ui_draw; ui_input in between unless kid
  mode is on).
- Exports: Felucca 1.6.1's set (render, boot, nor, buttons/keys/enc/master/midi, screen, lit/dim/breath/mid LEDs,
  anim levels, test_heavy) + `web_kid*` (scene, friend, beat, stars, hat, copy-game state..), `web_boost_q12`,
  `web_lim_t`, `web_master_q12`, `web_p_idx`, `web_step`, `web_voices`. No second core (1.1.5 has none).
- Native host tests: tests/hostsim.c, ui_test.c, ~50 more (Felucca 1.1.5's, + smk_* tests).
- Panel model: FM-1 (14 buttons, 27 keys, 7 encoders + MASTER, LCD 240x240 RGB565 BE, LEDs lit / dim / breath /
  mid + animation levels). The SMK-37 board model is a separate build (not the FM-1 core).

## 4. Build steps
- `tools/build.py generate()`: ui_fonts.h, ui_icons.h, ui_keycaps.h, ui_palettes.h, felucca_tables.h,
  felucca_fm6.h, felucca_samples.h, **kid_art.h**. Python 3 + Pillow + fontTools. Art and samples in repo
  (assets/). Nothing fetched.

## 5. Blockers for a native macOS build
None found: no inline asm in firmware/src, no dual core, the web unit builds with plain `cc`.

## 6. Persistence, audio
- 1 MiB NOR, Felucca 1.1.5/1.2 map: settings 0xFC000; projects 0x97000..0x9EFFF; user sample slots
  0xA0000..0xDBFFF; user preset banks 0xDC000..0xDFFFF; user-preset FM6 patches A 0x9F000 / B 0xFE000; autosave
  A 0xE5000 / B 0xE6000; OTA staging 0xE0000..0xE4FFF (FL_* ranges per board in hal/fm1_flash_map.h).
- Audio 44.1 kHz, HALF_FRAMES 128, stereo, NPART 4, NVOICE 8.

## 7. Native try (done)
- generate: OK. `hubtools/webcheck.c` (includes web/emu/felucca_web.c): built with Apple clang in 5.2 s, ran
  headless in 0.19 s: boot drew 293 times, full-screen "HI!" rainbow + duck scene (`hubout/rainbow.png`), heavy
  song + PLAY: peak 0.234, RMS 0.064.

## 8. FM1VST: closest glue, effort, risks
Closest: none of today's glues directly; the right base is a **shared "Felucca web unit" glue** (see felucca.md):
a core unit that `#include`s the firmware's own `web/emu/felucca_web.c` and drives it through its exports
(`web_boot`, `web_render`, `web_buttons/keys/enc/master/midi`, `web_screen`, LED getters, `web_nor`). The same
glue serves Felucca 1.6.1, Rainbow, GHOULBOX and JIANT; per firmware only the id / names / version and an
optional params map change.
**Effort: S** for a playable core with no Tier 2 map (params NULL / 0 and knob_target NULL are allowed by the
ABI: every knob relative); +S..M for a params map (kid mode has few parameters: probably none wanted).
**Risks**: kid mode takes over the frame (kid_frame) and the LEDs (colour-per-note art on the LCD; the panel's LED
model is one colour); MIDI out and SysEx are not in the web unit; the unit's fm1_ticks are 24 MHz ticks; no
upstream tags (pin a commit).
