# sloopDX 4.0 (zvenson/dxsloop, default branch @ eeec0a6, 2026-10-16)

Source: hubsrc/sloopdx. Host build tried: hubbuild/sloopdx.

## 1. License, tag, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` says GPL-3.0-only for code, with the Felucca GPL section-7 permission for the
  Felucca Assets (icon atlas etc. are "all rights reserved" Hügelton material). There are 95 `GPL-3.0-only` SPDX files
  and one Apache-2.0 file (the msfa / Dexed core).
- **No 4.0 tag**: the tags stop at v2.6 (the old numbering). 4.0 lives on the default branch only (`ui.c`:
  `FELUCCA_VERSION "sloopDX 4.0"`, "SLOOP 2.7, based on Felucca"). The README calls it **beta** ("4.0 ... has not been
  on a device yet"), while TODO.md's last commit says "4.0 on the FM-1 (installed, load test clean)". Pin a commit, not
  a tag.
- 236 files, 100 C/H, ~35.5 k lines.

## 2. Lineage
- **SLOOP fork, SLOOP is Felucca-based** (README: "A fork of SLOOP, which is based on Felucca"; `ui.c` "SLOOP 2.7").
  Not Dinghy, even though zvenson also wrote Dinghy.
- Felucca layout: `firmware/src/felucca.c` (the unity unit: libc, lcd, gfx, core.h, engines, drums, params, voice,
  slicer, fx, usb, midi_uart, arranger, seq, audio, cpu1, panel, ui*, icons, storage, upreset, project, ota, editor,
  console, recovery, splash, main.c), `tests/hostsim.c` (Felucca's DSP host), `firmware/hal/*`.
- One engine: DX7 (Dexed msfa ported to integer C, `dx7_core.c` / `eng_dx7.c`), plus FM drums.
- Dual core from SLOOP's dualcore branch (Andre Rezemini): `cpu1.h` job API (`cpu1_run` / `cpu1_wait` / `cpu1_on`),
  `cpu1.c` + `hal/fm1_cpu1.{h,S}` on the device, `tests/cpu1_host.c` on the host.

## 3. Host builds
- **No interactive emulator, no WASM sim** (`web/` holds the installer, editor and cheat sheets). The host programs are
  tests:
  - `tests/hostsim.c`: DSP render to WAV (`hostsim ENGINE PRESET MONO OUT.wav`, TRACKS=dir for a 4-track pattern).
  - `tests/ui_pages_test.c`: the real UI (panel.c, ui.c, ...) on a framebuffer with panel and flash doubles. It renders
    screens to PPM and runs a 20000-frame fuzz.
  - plus ~30 unit tests (`tests/run_tests.sh`).
- So, as for Felucca 1.0.1, a core has to assemble the firmware from hostsim.c's sound side plus `audio.c` plus the UI
  plus storage, and port `main.c`'s power-on and loop into the hook bodies. That is what FM1VST's `felucca_fw.c` did.
- Panel: Felucca's (`panel.c` maps labels to matrix ids; `fm1_in.notes` / `buttons`, `fm1_input_edges`,
  `fm1_enc_take`, `fm1_led` / `fm1_led_dim` / `fm1_led_bg` per column; this SLOOP line has `fm1_led_bg` and
  `fm1_remote_btn`, buttons held over MIDI). LCD 240x240 RGB565 via `lcd_fill` / `lcd_blit`.

## 4. Build steps
- `tools/build.py generate()`: `gen_font.py` (Terminus BDF -> `felucca_font.h`), `gen_icons.py` (`assets/icons.png` ->
  `felucca_icons.h`, needs Pillow), `gen_tables.py`, `gen_logo.py` (`sloopdx_logo.h`). The DX7 tables and bank are
  committed (`dx7_tables.h`, `dx7_bank.h`). Nothing is fetched. It took 0.1 s with the framework Python 3.11 (Pillow).
  It works with FM1VST's `fm1_add_core` GEN mechanism as is.
- Version regex: `firmware/src/ui.c` `^#define FELUCCA_VERSION "sloopDX ([^"]*)"`.

## 5. Blockers for a native macOS build
- `section(".pool" / ".lowram" / ".noinit")` attributes in 25 places. hostsim.c does `#define __attribute__(x)`, so they
  vanish, as in FM1VST's Felucca core.
- Dual core: the render hands units to core 1 through the `cpu1.h` API. On the host, `tests/cpu1_host.c` with `CORES=1`
  (the default) runs the job inline at `cpu1_wait`, which is deterministic and single-threaded and so fits the ABI.
  `CORES=2` would start a pthread and must not be used.
- Inline asm in non-HAL code: two `__asm__ volatile("" ::: "memory")` compiler barriers only.
- `main.c` (400 lines) is the device boot and cannot be built on the host. Its power-on order, the CPU1 start, UAC and
  the 15 ms frame loop must be rewritten in a `sloopdx_fw.c`, as `felucca_fw.c` did. SLOOP 2.7 has much more there than
  Felucca 1.0.1 (arranger, punch, editor_live, console, CPU1 boot).

## 6. Persistence and audio
- Felucca `storage.c` object store over `st_read` / `st_erase` / `st_prog` (4 KiB sectors, 256-byte header, A/B
  copies). Map (`storage.c`): settings `0xFC000` / `0xFD000`; projects `0x97000..0x9EFFF`; autosave A `0x9F000` / B
  `0xFE000`; 8 DX7 user banks `0xA0000..0xBFFFF`; bank in use `0xC0000..0xC1FFF`; MY KIT `0xC2000..0xC3FFF`; user
  presets `0xDC000..0xDFFFF`; OTA `0xE0000..0xE4FFF`. Total 1 MiB, the same as FM1VST's Felucca image.
- Audio: 44.1 kHz, `HALF_FRAMES` 256 (5.8 ms) in this SLOOP line (Felucca 1.0.1 / ChoralRoot use 128), stereo, 24-bit
  out (`OUT_SHIFT`). EMU_BLOCK is 128, so `emu_fw_audio`'s per-half loop needs `frames` to be a multiple of 256, or the
  glue must buffer.

## 7. Host sim tried
- `generate` ran in 0.1 s. `hostsim.c` built in 0.9 s and rendered `render.wav` (2 s, peak 10274). `ui_pages_test.c`
  built in 3 s and ran in 0.8 s: every UI check PASS, PPM screens written. No errors with Apple clang. There is no
  interactive or headless panel emulator to try.

## 8. FM1VST glue
- Closest: **felucca** (`core-glue/felucca`: hostsim.c + audio.c + UI + storage, hook bodies from main.c).
- Effort: **M**. The include list and HAL carry over, but `main.c` is 4x Felucca 1.0.1's; the new UI files (ui_song,
  ui_studio, ui_voice, ui_layers, punch, arranger), the CPU1 inline host, `HALF_FRAMES` 256 and `fm1_led_bg` must be
  handled. A Tier 2 map must be written fresh (DX7 parameters).
- Risks: no release tag for 4.0 and a beta status; the 256-frame half vs EMU_BLOCK 128; the main loop's extra duties
  (USB editor, console, CPU1 self-test) to stub. It shares the SLOOP 2.x base with ORBIT, SLOOP itself and other hub
  forks, so a "SLOOP glue" built once could serve several.
