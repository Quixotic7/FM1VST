# No Man Sky FM 0.4 (hericdk/nomanskyFM, main)

Clone: `scratchpad/hubsrc/nomansky` @ `4304597` (2026-10-16, "Site: the floating screen recorder ..."). **The repo
has no tags**; the version is `FELUCCA_VERSION "NMS FM 0.4"` (firmware/firmware/src/ui.c) and
`release/nmsfm-0.4.fwsc`. Pin a commit.

## 1. License, size
- Root `LICENSE` and `firmware/LICENSE`: **GPL-3.0-only** (README: "like SLOOP and Felucca"); `firmware/LICENSING.md`
  is Felucca's / SLOOP's (Hügelton assets excluded; SLOOP's kit is CC0 `assets/samples-cc0`); msfa Apache-2.0 for
  `fm6_core.c`. The README adds a credit request ("Features from No Man Sky FM by Hericdk") on top of the GPL.
- 227 files (many site videos in `media/`, 50 MB clone); 85 .c/.h, **31,228 C lines** (`ui_game.c` 2,985,
  `ui_nms.c` 1,713 lines of game and screens).

## 2. Lineage
**SLOOP** (isod89/sloop-fm1): `firmware/SLOOP-BASE.txt` = sloop main `d315ad8` (2026-10-13, "firmware = v2.6"); the
README says based on **SLOOP 2.7.1** ("Merged from SLOOP 2.6 -> 2.7.1: the sound on both cores ..."). Layout is
SLOOP's: `firmware/firmware/{hal,loader,src}`, device unit `src/felucca.c`, `cpu1.c` (second core), arranger,
punch, studio, plus `ui_nms.c` / `ui_game.c`. Comments partly in Portuguese.

## 3. Host builds
- **No native host simulator** and **no tests/ directory** (SLOOP's tests/hostsim.c is not in this repo).
- **WASM**: `firmware/wasm/nms_wasm.c` (525 lines) is a complete host HAL for the whole firmware (time, IRQ, input,
  ADC, an ST7789 command model behind `lcd.c` into a 240x240 RGB565 framebuffer, audio, the NOR image, then the
  firmware sources in felucca.c's order). Built by `firmware/build-wasm.sh` with **zig cc**
  (`python3 -m ziglang cc --target=wasm32-freestanding -nostdlib`; uses the firmware's own libc.c). The site player
  is `sim/{sim.js, fm1-worklet.js, fm1-3d.js, nms.wasm, index.html}` (a 3D FM-1 faceplate).
- Entry points: `emu_init()` (persist_boot, settings_init, lcd_init, splash, panel_init, felucca init),
  `emu_start()` (after the splash), `emu_frame()` (master / battery ADC, ui_input, ui_leds, ui_draw, autosave_tick,
  sections_flush), `emu_render(n)` (mix_block per CTL block, advances fm1_ms by rendered samples),
  `emu_input(notes, buttons)`, `emu_enc(e, steps)`, `emu_adc_set`, `emu_midi(pkt)`.
- Panel model: matrix ids as Felucca (FM1_KEYMAP, 0..13 buttons, 14..40 keys), 7 encoders (enc_steps), ADC MASTER /
  battery; LEDs `fm1_led`, `fm1_led_dim`, `fm1_led_bg` and a per-LED brightness array `fm1_led_lv[41]` (key light
  effects: waves, rain, comet, fireworks) that emu_hal_t cannot carry beyond on / dim (fold into led / led_dim as
  the Melodee glue folds its planes). No velocity sensing (step velocity only).

## 4. Build steps
`tools/build.py generate()`: gen_font, gen_tables, gen_samples (numpy, via sampleio.py; CC0 kit + synthesised
drums), gen_drumkits (32 kits), gen_fm6_patches, and eight NMS sprite generators from `assets/*.png` (Pillow).
**`assets/abertura.png` is missing** from the repo: gen_abertura writes a stub (`NMS_ABERTURA 0`); the opening still
runs (seen in the probe) but without that image. With the libraqm python + numpy 2.4: **generate OK in ~1 s**.

## 5. Native macOS blockers
- `__attribute__((section(".pool" / ".noinit" / ".lowram")))` everywhere: Mach-O rejects them; compile with the
  attribute neutralised (`-D'__attribute__(x)='`, as the hostsim builds do) or a per-section macro.
- Dual core: `FELUCCA_CPU1 0` path exists for the simulator (cpu1_run / wait / on stubs: everything renders on one
  core). `hal/fm1_cpu1.S`, other .S files: device only, not included by nms_wasm.c.
- Freestanding: the unit brings its own memcpy / memset / memcmp (libc.c); on macOS the object exports them, which
  links (two-level namespace) but a core should hide them (`-fvisibility=hidden`, as fm1_add_core does).
- The wasm exports cast pointers to uint32_t (`emu_fb_ptr` etc.): truncated on 64-bit; a core reads the statics
  directly instead.

## 6. Persistence, audio
- NOR image in the HAL: `EMU_FLASH_BASE 0x90000`, `EMU_FLASH_SIZE 0x70000` (the persisted areas 0x97000..0xFEFFF:
  projects, autosave, settings, user presets, user sample slots read in place via `SMP_USER_XIP` / `ST_XIP_PTR`);
  storage.c / upreset.c / project.c on st_read / erase / prog hooks; `fl_jedec_ram()` 0x856014. A core keeps the full
  1 MiB image and points the window at offset 0x90000.
- Audio: 44,100 Hz, HALF_FRAMES 256 on the device; the sim renders `mix_block` in CTL-sample sub-blocks into
  stereo int32 at Q15 (/32768).

## 7. Native probe (done; no upstream native sim)
`cc -O2 -ffreestanding -fno-builtin -w -D'__attribute__(x)=' -c wasm/nms_wasm.c`: compiles in 4 s with Apple clang
(1 MB object, only `___stack_chk_*` undefined). A 30-line driver in `scratchpad/probe/nms/` (emu_init, render 128
frames per 2.9 ms, emu_start at 0.5 s, a key at 3 s, emu_frame every 16 ms, 8 s): **runs headless natively**, audio
peak 0.55, 660 k non-zero samples, screen shows the game with the "HOW TO FLY" box (`probe/nms/out.png`).

## 8. FM1VST core
- Closest glue: **felucca** (SLOOP is a Felucca fork: same storage hooks, fl_jedec_ram, irq_save, SMP_USER_XIP, the
  tick-debounce, flash_stage / exit machinery), with `wasm/nms_wasm.c` as the template for the include list
  (it is NMS's "emu_firmware.h"). A SLOOP core, if another group builds one, would be closer still.
- Effort: **M**.
- Risks: no tags (pin a commit); no host tests upstream (the WASM sim is the only reference); the missing
  abertura.png; `fm1_led_lv` brightness effects lost in emu_hal_t; zig-only upstream build (the core uses clang);
  the game uses held / double-tap gestures on OCT- / OCT+ / HOME (button timing must stay real-time-accurate);
  50 MB repo (media) as a submodule: use a sparse or shallow checkout.
