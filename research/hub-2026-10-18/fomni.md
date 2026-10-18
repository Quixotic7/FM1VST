# FoMni 0.4 (charlesvestal/fm1-fomni, tag v0.4)

Clone: `scratchpad/hubsrc/fomni` @ `6ecd227` (2026-10-15, "Screen reader: the screen as text for fm1-reader").

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md`: **GPL-3.0-only**, "built on Felucca's platform layer (through X0X)".
  Chordian data (patterns, waves, drums, strum voicing: `dsp/om_data.h`, `om_drums.h`) is CC0. Fonts OFL.
- 108 files; 50 .c/.h, **10,497 C lines** (the instrument: `dsp/omni.c` 782, `app/ui.c` 1,034).

## 2. Lineage
Felucca platform via **X0X**: same `firmware/src/app/plat.h` (a subset: no slots, no second core, no perf), same
`plat_fm1.c` / `panel.c` / `main_fm1.c` from X0X, `host/omni_host.c` "From X0X's x0x_host". Platform files
(`hal/`, `loader/`, `src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`) are Felucca's as changed for X0X. An
Omnichord-style instrument (chord buttons on the keys, strum plate, rhythm). Ancestor of FiMba-1 and Fart Machine One.

## 3. Host builds
- **Native host simulator**: `host/omni_host.c` (632 lines) + `firmware/src/dsp/omni.c`, `host/build_host.sh`
  (cc, `-DOM_HOST`). Headless script runner (`omni_host SCRIPT OUTDIR`; wait / press / tap / key / strum / turn /
  spin / master / midi / wav / shot / leds / expect / reboot / listen / said / speakdump).
- **WASM**: `web/emu/omni_web.c` (includes omni_host.c), `web/emu/build.sh` (emcc STANDALONE_WASM), worklet.js.
- Drivers: `boot()`; `run_ms`: `om_render(blk, 256, master)` every 256 frames; `ui_frame` every 16 ms else
  `ui_input_only`.
- Panel model: plat.h as X0X (14 label buttons, 27 keys, 7 encoder roles, MASTER 0..4096), LEDs
  `plat_leds(buttons, keys)` + `plat_glow` + `plat_play_red`; LCD 240x240 RGB565 via lcd_fill / lcd_blit + gfx.c.
- No velocity / aftertouch sensing.

## 4. Build steps
`tools/gen_font.py` (Pillow + libraqm) -> `felucca_font.h`; `tools/gen_words.py` -> `ann_words.h`. Nothing else,
nothing fetched. `tools/build.py generate()` runs exactly these two.

## 5. Native macOS blockers
None: `BARRIER()` is an empty asm memory clobber; `OM_POOL` section empty on host; single core; no malloc in the
app. Device-only files are not built.

## 6. Persistence, audio
- storage.c objects: `OBJ_PROJ` at 0xFC000, `OBJ_PANEL` (calibration) at 0xDC000, 2 x 4 KiB each (A/B), payload
  <= 3,840 B; X0X's magic, the project told apart by its own magic. Host sim keeps objects in RAM.
- 44,100 Hz, 256-frame blocks, stereo int32 Q23.

## 7. Host sim build / run (done)
`sh host/build_host.sh` (with the libraqm python): **built in 0.6 s**. Headless script (key held, strum across 11
white keys, 1.5 s WAV, shot, leds): runs; WAV stereo 44.1 kHz, peak 22,765 (sound), LEDs printed.

## 8. FM1VST core
- Closest glue: the X0X "plat" glue proposed in x0x.md (felucca's HAL half + a plat_* layer on emu_hal_t).
- Effort: **S** once the X0X / plat glue exists (M alone).
- Risks: the 256-frame render vs 128-frame ABI calls; `OM_VERSION` passed at build time (default "DEV"); the
  screen reader stub. Small param map (`om_param_info`, P_* in project) makes Tier 2 easy.
