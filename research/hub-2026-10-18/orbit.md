# ORBIT 0.7.1 (dspaudio/orbit @ v0.7.1, c07bd1d)

Source: hubsrc/orbit. Host builds tried: hubbuild/orbit.

## 1. License, tag, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` (Felucca's text, kept): code GPL-3.0-only, with the Felucca Assets
  (`assets/icons.png` etc.) "all rights reserved" under the GPL section-7 combination permission. 130 `GPL-3.0-only`
  SPDX files; `fm6_core.c` is Apache-2.0 (msfa); 2 MIT.
- **Licence risk**: `assets/op1-cluster/{tables.json, factory.json}` is "official OP-1 #246 data extracted ... remains
  original Teenage Engineering material. It is not relicensed" (LICENSING.md "Contributions";
  `assets/op1-cluster/provenance.json` names the op1_246.op1 firmware package and its hashes). The CLUSTER engine
  (`op1_cluster_core.c`, "the verified native cluster C translation") is a translation of OP-1 firmware DSP. There is
  no build switch to leave it out: `engines.c` includes `eng_op1_cluster.c` unconditionally. Shipping it inside FM1VST
  carries ORBIT's legal exposure; exclude CLUSTER or get advice.
- Tag v0.7.1 ("Release ORBIT 0.7.1 with verified firmware package"). 287 files, 134 C/H, ~44.4 k lines (`firmware/src`
  ~26.3 k .c).

## 2. Lineage
- **SLOOP / Felucca** (README: "Based on SLOOP/Felucca, with original ORBIT engines and graphics inspired by the
  original OP-1's four encoder roles"; `README-SLOOP.md` is SLOOP's README kept). The storage map, FM6 bank and punch /
  slicer / arranger / ui_vis files are SLOOP 2.4+ (`fm1_flash.h` FL_FM6 "SLOOP 2.4").
- Felucca layout: `firmware/src/felucca.c` unity unit (46 includes) ending with `main.c` (326 lines), `tests/hostsim.c`.
  ORBIT's own: `eng_orbit.c`, `eng_cloud.c`, `eng_op1_cluster.c`, `orbit_tape.h`, and `orbit_ui.c` / `orbit_modes.c` /
  `orbit_demo.c` (included from `ui_studio.c` / `ui_draw.c` / `ui_input.c`).
- No CPU1 (no `cpu1` references).

## 3. Host builds
- **Native preview**: `tools/orbit_host.c` (38 lines) over `tests/orbit_host_support.h` (hostsim.c + the real UI + panel
  / flash doubles + `mix_block`). It reads commands on stdin (`button N`, `knob N D`, `render FRAMES KEYMASK`), writes
  `screen.ppm` and `chunk.wav`, and answers "OK sel view module". `tools/orbit_preview.py` wraps it in a local HTTP
  bridge for a browser page (`preview/index.html`). It is headless, but **not a device clock**: it renders audio in
  chunks between UI steps, has no 1 ms tick and no main.c, and its demo pattern is preview-only.
- **No WASM** in the repo.
- Felucca-UI test hosts: `tests/hostsim.c` (DSP to WAV), `tests/ui_pages_test.c` and many unit tests
  (`tests/run_tests.sh`).
- Panel: Felucca / SLOOP (`panel.c` label -> matrix; `fm1_in`; `fm1_input_edges`; `fm1_enc_take`; `fm1_led` /
  `fm1_led_dim` / `fm1_led_bg` per column; 240x240 RGB565 `lcd_fill` / `lcd_blit`).

## 4. Build steps
- `tools/build.py generate()`: `gen_font.py` (Terminus BDF), `gen_icons.py` (`assets/icons.png`, Pillow),
  `gen_tables.py`, `gen_samples.py` (from `assets/samples-cc0` + generated waves), `gen_drumkits.py`,
  `gen_fm6_patches.py`, `gen_op1_cluster.py` (from the OP-1 JSON, SHA-256 pinned). It took 1 s with Pillow, offline.
  (`tools/fetch_cc0.py` downloads more CC0 samples but is not in `generate()`.)
- GEN for `fm1_add_core`: `felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h
  felucca_fm6.h felucca_op1_cluster.h`.
- Version: `firmware/src/ui.c` `#define FELUCCA_VERSION "ORBIT 0.7.1"`, regex `"ORBIT ([^"]*)"`.

## 5. Blockers for a native macOS build
- None for the host builds: `hostsim.c`'s `#define __attribute__(x)` drops the 16 section attributes, and both hosts
  built with Apple clang without diagnostics.
- `main.c` is device boot: a core needs its own `orbit_fw.c` with main.c's power-on and loop, as `felucca_fw.c`.
- Asm only as compiler barriers in core.h / voice.c (the HAL asm is not built). No CPU1.

## 6. Persistence and audio
- Felucca / SLOOP `storage.c` objects over `st_read` / `st_erase` / `st_prog`: settings `0xFC000` / `0xFD000`;
  projects `0x97000..0x9EFFF`; autosave A `0x9F000` / B `0xFE000`; user sample slots `0xA0000..0xDBFFF`
  (`eng_sample.c`, XIP-read in place on the device, so it needs `SMP_USER_XIP`-style mapping onto the image as in the
  Felucca glue); user presets `0xDC000..0xDFFFF`; FM6 bank `0xE5000` / `0xE6000`; OTA `0xE0000..0xE4FFF`. 1 MiB.
- Audio: 44.1 kHz, `HALF_FRAMES` 256 (SLOOP line), stereo, 24-bit out.

## 7. Host sims tried
- `generate` 1 s. `orbit_host` built in 4 s; driven headless with `render 44100 1`, `button 6`, `knob 3 2`,
  `render 22050 0`: it answered OK per command, wrote `chunk.wav` (peak 13542) and `screen.ppm` (the OSCILLOSCOPE
  visualiser). `hostsim` built in 1.4 s and rendered 2 s (peak 17994). Works.

## 8. FM1VST glue
- Closest: **felucca** (hostsim.c + audio.c + UI + storage; hook bodies from `main.c`). `orbit_host_support.h` is a
  useful map of what the UI needs on the host.
- Effort: **M** (the SLOOP-line `main.c`, 256-frame halves, `fm1_led_bg`, sample slots XIP). Shared with sloopDX: build
  one SLOOP-family glue.
- Risks: **the OP-1-derived CLUSTER engine and data** (legal, the top risk); the Felucca Assets permission (the same as
  FM1VST's existing Felucca core); a 0.x project with an `.omo/` agent workspace in the tree.
