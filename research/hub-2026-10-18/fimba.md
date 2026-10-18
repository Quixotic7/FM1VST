# FiMba-1 0.6.1 (jadamsowers/fm1-fimba, tag v0.6.1)

Clone: `scratchpad/hubsrc/fimba` @ `0485ed0` (2026-10-12, "Reverb: the plate stops at Reverb 0 ..."); `VERSION`
file = 0.6.1. Has `CLAUDE.md` (AI-assisted project) and a GitHub Pages workflow.

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md`: **GPL-3.0-only**, "built on FoMni (and through it X0X and Felucca)".
  Fonts OFL. No third-party samples (the kalimba is synthesised).
- 123 files; 47 .c/.h, **10,462 C lines** (`dsp/kalimba.c` tine / body / buzz / grain / delay + Dattorro plate).

## 2. Lineage
**FoMni fork** by a third author (Adam Sowers, GitHub jadamsowers): `app/{panel,plat_fm1,main_fm1}.c`, `main.c`,
`host/kalimba_host.c`, the frame of `ui.c` and `project.c` adapted from FoMni; platform is Felucca's as changed for
X0X and FoMni (USB names changed). plat.h identical to FoMni's except LEDs:
`plat_leds(buttons, keys, dim_keys)` (3 args) and no `plat_glow` / `plat_play_red`.

## 3. Host builds
- **Native host simulator**: `host/kalimba_host.c` + `dsp/kalimba.c`, `host/build_host.sh` (cc, `-DOM_HOST`,
  honours `$PYTHON`). Headless script runner as FoMni's; `tests/scenarios/*.kal`.
- **WASM**: `web/emu/kalimba_web.c`, `web/emu/build.sh`, worklet.js, plus `make_standalone.py` / `inline_lib.py`
  (a single-file standalone page).
- Drivers: `boot()`; `run_ms`: `km_render(blk, 256, master)` every 256 frames, `ui_frame` every 16 ms.
- Panel: plat.h (label buttons, 27 keys, 7 encoder roles, MASTER). The host notes "the keys are not velocity
  sensitive: a key plucks at 100". LCD 240x240 RGB565 via lcd_fill / lcd_blit.

## 4. Build steps
Only `tools/gen_font.py` (Pillow + libraqm) -> `felucca_font.h`. No words file / screen reader in this one.
`requirements.txt` lists Pillow (build) and mido / python-rtmidi / pyusb (install tools only).

## 5. Native macOS blockers
None (empty-asm `BARRIER()`, pool section macro empty on host, single core).

## 6. Persistence, audio
storage.c objects: OBJ_PROJ 0xFC000, OBJ_PANEL 0xDC000 (as FoMni). 44,100 Hz, 256-frame blocks, stereo int32 Q23.

## 7. Host sim build / run (done)
**Built in 0.7 s**. Headless script (two key taps, 1.5 s WAV, shot, leds): runs, WAV peak 14,476, key LED shown.

## 8. FM1VST core
- Closest glue: the plat glue (X0X / FoMni) with the 3-argument `plat_leds`.
- Effort: **S** after the family glue.
- Risks: third-party maintainer (pin the tag); plat.h divergence (LED signature) means the family glue should take
  per-firmware small adapters rather than one fixed plat.h.
