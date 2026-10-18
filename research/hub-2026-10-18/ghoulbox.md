# GHOULBOX 2.3.2 (hub id: ghoulbox)

Source: https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth tag `ghoulbox-v2.3.2` (71ac3f8,
2026-10-17). Clone: `hubsrc/ghoulbox`. `GHOULBOX_VERSION "2.3.2"` in firmware/src/core.h; the Felucca base string
`FELUCCA_VERSION "v1.4.1"` in felucca.c. Not in FM1VST today.

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` Felucca 1.4.1's + GHOULBOX's rows: **GPL-3.0-only**; DaisySP / Rings
  (MIT), msfa (Apache-2.0); fonts **VT323** (UI) and **Pirata One** (wordmark) OFL, InterTight (in tree, unused),
  DotGothic16 (web); CC0 samples. Browser-only vendor code in `web/emu/vendor/` (libflac.js MIT, libFLAC
  BSD-3-Clause, LAME LGPL-2.0, wasm-media-encoders MIT): **not part of the firmware**, never in a core.
- 340 tracked files, ~78.0 k lines of C/H.

## 2. Lineage
**Felucca 1.4.1** fork (README: "a fork of Felucca by Hügelton Instruments (1.4.1)"), tuned for dungeon synth:
GURDY engine (hurdy-gurdy drones, RING), DREAM / EVOL generative mode, ambience, keys/latch, its own art
(`gen_ghoulbox_art.py`), fonts. Its web emulator is **Felucca 1.6.x's** (web_power_on / web_frame / web_ms
identical to Felucca 1.6.1's), plus project capture / import exports.

## 3. Host builds
- **WASM + native**: `web/emu/felucca_web.c` (542 lines), worklet.js, index.html, build.sh (emcc), emu_test.mjs,
  io_test.mjs, audio export / session JS, `native_check.c`. Driven exactly as Felucca 1.6.1's unit:
  `web_render(n)` -> `web_ms()` -> audio ISR per 128-frame half, `web_frame()` every 15 ms.
- Exports: Felucca 1.6.1's set + `web_project_capture / size / validate / restore / import_buffer /
  import_capacity` (a project as bytes: useful for plugin presets). No second core (stub).
- Native host tests: hostsim.c, ui_test.c and ~80 more (ghoulbox_compat_test, gurdy_test, dream_test,
  ghoulbox_shots.c renders screens).
- Panel: FM-1 (14 buttons, 27 keys, 7 encoders + MASTER, LCD 240x240 RGB565 BE, LEDs lit / dim / breath / mid +
  the LED animation).

## 4. Build steps
- `tools/build.py generate()`: ui_fonts.h (`--preset ghoulbox`, VT323), ui_icons.h, ui_keycaps.h,
  ui_palettes.h, felucca_tables.h, felucca_fm6.h, felucca_samples.h, **ghoulbox_art.h** (Pirata One wordmark,
  skull art; also writes build/gen/fukiai_fitted.ttf). Python 3 + Pillow + fontTools. Everything in repo.

## 5. Blockers for a native macOS build
None found (no inline asm in firmware/src, no dual core in 1.4.1, web unit builds with plain `cc`).

## 6. Persistence, audio
- 1 MiB NOR, Felucca 1.4.1 map (= 1.6.1's): settings 0xFC000; projects 0x97000..0x9EFFF; user sample slots
  0xA0000..0xDBFFF; user preset banks 0xDC000..0xDFFFF; FM6 user patches A 0x9F000 / B 0xFE000; autosave A 0xE5000
  / B 0xE6000; FM6 voice bank 0xE7000; OTA staging 0xE0000..0xE4FFF.
- Audio 44.1 kHz, HALF_FRAMES 128, stereo, NPART 4, **NVOICE 12** (Felucca: 8).

## 7. Native try (done)
- generate: OK. `hubtools/webcheck.c` (includes web/emu/felucca_web.c): built in 5.8 s, ran headless in 0.15 s:
  HOME screen (part 1 GURDY "HURDY GURDY", BUZZ / WHL / DLVL / REL cards, torches; `hubout/ghoulbox.png`), heavy
  song + PLAY: peak 0.261, RMS 0.085.

## 8. FM1VST: closest glue, effort, risks
Closest: the **shared Felucca-web-unit glue** proposed in felucca.md (today's core-glue/felucca is 1.0.1-shaped
and would need the same rework). GHOULBOX's unit is Felucca 1.6.1's almost line for line.
**Effort: S** (playable core, no Tier 2 map) / **M** with a params map (GURDY / DREAM parameters).
**Risks**: NVOICE 12 raises host CPU per instance a little (irrelevant on a Mac); the web unit lacks MIDI out /
SysEx; DREAM / EVOL use randomness (check its seed source: deterministic replay tests need a fixed seed).
