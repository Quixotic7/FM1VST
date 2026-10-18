# JIANT FM 0.8.5 (hub id: jiant)

Source: https://github.com/juanjiant-bit/JIANT commit `c524e3c` (2026-10-17, "0.8.5: merge DRUM-X octaves";
the short SHA is not fetchable directly: cloned with `--filter=blob:none` and checked out). Clone: `hubsrc/jiant`.
`FELUCCA_VERSION "v0.8.5"` ("JIANT's (based on Felucca 1.1.5.1)"). Not in FM1VST today.

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` Felucca's + JIANT's rows: **GPL-3.0-only**; msfa (Apache-2.0);
  **CMU Pronouncing Dictionary (BSD-2-Clause)**: a subset compiled into the firmware (VOICE's English words,
  `felucca_lyrics.h`); fonts Chakra Petch, IBM Plex Mono, Michroma, InterTight, DotGothic16 (OFL), Fukiai (MIT);
  klattsch (design reference only). No DaisySP / Rings rows (PHYS not carried). An `android/` probe app (not
  firmware).
- 337 tracked files, ~87.2 k lines of C/H. NPART 6.

## 2. Lineage
**Felucca 1.1.5.1** fork (README), with **SLOOP's song system** (README: "takes its song system from SLOOP"),
dual-core render (`hal/fm1_multicore.h`, JIANT 0.6.71: "what starts it ... comes from Melodee and fm1-x0x"), a
song generator (GEN, 43 genres), VOICE (sing.c, lyrics), DRUM-X, six parts. Web emulator: Felucca 1.6's design,
credited to X0X (charlesvestal/fm1-x0x).

## 3. Host builds
- **WASM + native**: `web/emu/felucca_web.c` (582 lines), worklet.js, index.html, build.sh (emcc),
  emu_test.mjs, stems.js / stems_test.mjs, gen_bench.mjs, `native_check.c`. Driven as Felucca 1.6.1's unit
  (`web_render` -> `web_ms`), plus JIANT's boot (prefs_update, splash signature `splash_write`, `session_dice(seed)`)
  and `song_poll()` in the frame.
- Exports: Felucca 1.6.1's set + `web_gen*` (the generator), `web_seed`, `web_song_*` (bars, rows, select, play,
  end, cur), `web_bk_*` (a backup buffer), `web_beat_samples`, `web_trk_live`, `web_mix_skip`, `web_play`.
- Dual core: `FELUCCA_DUAL_CORE 1` in felucca.c; the web unit stubs `fm1_core1_stop` and builds it off
  ("FELUCCA_DUAL_CORE 0").
- Native host tests: hostsim.c, ~60 tests (drumx, gen, animal, startup ...), cpu_vg (valgrind CPU model).
- Panel: FM-1 (14 buttons, 27 keys, 7 encoders + MASTER, LCD 240x240 RGB565 BE, LEDs lit / dim / breath / mid +
  animation levels).

## 4. Build steps
- `tools/build.py generate()`: ui_fonts.h (`--preset jiant-lab`), ui_icons.h, ui_keycaps.h, ui_palettes.h,
  **ui_shapes.h** (gen_ui_shapes.py), felucca_tables.h, **felucca_lyrics.h** (gen_lyrics.py: phonemes, 2095-word
  English dictionary, ES/EN/JA banks), felucca_fm6.h (no samples header: no SAMPLE engine data). Python 3 + Pillow +
  fontTools. Everything in repo.

## 5. Blockers for a native macOS build
None for the web unit (dual core off in it). No inline asm in firmware/src. Randomness: `session_dice(seed)` and
the generator take the page's seed (`web_seed`): pass a fixed seed for deterministic replays.

## 6. Persistence, audio
- 1 MiB NOR, Felucca's records; map: settings 0xFC000; projects 0x97000..0x9EFFF; **0xA0000..0xDBFFF = JIANT's
  songs** (Felucca's user sample slots); user preset banks 0xDC000..0xDFFFF; FM6 user patches A 0x9F000 /
  B 0xFE000; autosave A 0xE5000 / B 0xE6000; FM6 voice bank 0xE7000.
- Audio 44.1 kHz, HALF_FRAMES 128, stereo, **NPART 6**, NVOICE 8.

## 7. Native try (done)
- generate: OK. `hubtools/webcheck.c` (includes web/emu/felucca_web.c): built in 9.1 s, ran headless in 0.13 s;
  at 1.5 s it still shows the JIANT signature splash (longer boot: 30 ms + SPL_WRITE + SPL_HOLD;
  `hubout/jiant.png`), heavy song + PLAY: peak 0.267, RMS 0.073, 5 flash writes (it saves at boot).

## 8. FM1VST: closest glue, effort, risks
Closest: the **shared Felucca-web-unit glue** (felucca.md). JIANT's unit adds boot / frame calls but the export
surface the glue would use is the same.
**Effort: S** (playable core) / **M** with a params map (six parts, GEN, VOICE, DRUM-X).
**Risks**: NPART 6 (the host's Tier 2 / per-part assumptions are 4); fast-moving pre-1.0 firmware (pin commits,
expect breakage each release); seeded randomness; the boot writes flash (flash_dirty moves at power-on).
