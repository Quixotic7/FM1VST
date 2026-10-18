# X0X 1.1.0 (charlesvestal/fm1-x0x, tag v1.1.0)

Clone: `scratchpad/hubsrc/x0x` @ `27f6fac` (2026-10-15, "1.1.0: the screen reader in What's new ..."). The owner's
local `../fm1-x0x` is older: `v1.0-3-gfe6b0ae` (2026-10-10).

## 1. License, size
- `LICENSE` = GPL-3.0 text; `LICENSING.md`: **GPL-3.0-only** ("a fork of Felucca and keeps Felucca's licence").
  Ported parts: 909 (9W9 / ER-99, GPL-3.0), 808 (8W8), 303 (schwung-303: Open303 MIT parts), TB-3PO (Hemisphere),
  break generator (schwung-breakbeat, used by permission, no licence file upstream). Fonts OFL. No Hügelton assets.
- 247 tracked files; 91 .c/.h, **27,430 C lines** (app/ui.c alone 4,685).

## 2. Lineage
Felucca fork, but **Charles Vestal's own platform**: the app (`firmware/src/{app,dsp,seq}`) only talks to
`firmware/src/app/plat.h` (buttons by label, keys, encoders by role, LEDs, MIDI, store objects, sample slots, second
core, perf). Felucca's platform files (`hal/`, `loader/`, `src/{libc,lcd,gfx,usb,storage,ota}.c`, tools) sit under
it; `app/plat_fm1.c` implements plat.h on Felucca's HAL (device unity build `firmware/src/x0x.c`). This plat.h is the
ancestor of FoMni / FiMba / Fart Machine's plat.h and of fm1-base's (AMB).

## 3. Host builds
- **Native host simulator**: `host/x0x_host.c` (1,290 lines) = plat.h implemented on the host + the whole app
  (`#include` gfx.c, slots.c, announce.c, project.c, ui.c) + separately compiled DSP/seq units. Built by
  `host/build_host.sh` (cc, `-DX0X_HOST`, `-ffp-contract=off`). **Headless, script-driven** (`x0x_host SCRIPT
  OUTDIR`: wait / press / tap / key / turn / spin / master / midi / wav / shot / leds / expect / reboot ...). Writes
  WAV, PNG, MIDI log. Uses `libproc` on macOS for an instruction counter.
- **WASM**: `web/emu/x0x_web.c` (includes x0x_host.c, exports web_render / web_buttons / web_keys / web_enc /
  web_fb / web_store ...), `web/emu/build.sh` (emcc, STANDALONE_WASM), `worklet.js`, `index.html`.
- Host-side drivers: `boot()` (engine_init, project_load / defaults, ui_init) and `run_ms(ms)`: per ms `now_ms++`;
  every 256 frames `engine_render(blk,256)` + `engine_volume`; `ui_frame()` every 16 ms else `ui_input_only()`.
- Panel model (plat.h): 14 buttons as label bits (B_FX..B_OCTUP), 27 note keys (F3..G5), 7 encoders by role
  (EN_SELECT, ALGO, PRESET, K1..K4; detents), MASTER 0..4096. LEDs: `plat_leds(buttons, keys)` + `plat_glow`
  (unlit buttons dim) + `plat_keys_dim` + `plat_play_red` (PLAY green / red). LCD 240x240 RGB565 (byte-swapped as
  the panel takes it) through `lcd_fill` / `lcd_blit` under gfx.c. `plat_fm1.c` shows the LED -> matrix column
  mapping (FM1_KEYMAP, PLAY green at col 8 row PA5): reusable for emu_hal.led / led_dim.
- No key velocity / aftertouch sensing (keys are on/off).

## 4. Build steps for a host build
- `tools/gen_font.py` -> `build/gen/felucca_font.h` (Pillow **with libraqm**: font features; plain Pillow fails with
  "not supported without libraqm". With `/opt/homebrew/lib/libraqm` via DYLD_FALLBACK_LIBRARY_PATH it works).
- `tools/gen_drum_samples.py` -> `x0x_drum_samples.h`, `x0x_drum_tables.h` (stdlib only).
- `tools/gen_words.py firmware/words.tsv web/words build/gen/ann_words.h` (screen reader link; also writes a JSON
  into web/words).
- `tools/gen_builtin_break.sh`: compiles `tools/gen_builtin_break.c` + drum909.c with the host cc and runs it ->
  `x0x_break_bank.h` (388 KB). Nothing fetched. `tools/build.py generate()` does all four (font, drum samples,
  words, break bank via `sh tools/gen_builtin_break.sh`, which needs `cc`), so fm1_add_core's `build.generate()`
  hook fits as is. Note `sh` strips DYLD_* (SIP): the font step must run under a python that gets the env.
- Version: `X0X_VERSION` defaults to "DEV"; the release passes it (`-DX0X_VERSION="1.1.0"`): fm1_add_core's
  VERSION_FILE regex will not find it: pass it as a define.

## 5. Native macOS blockers
None in the app: only compiler barriers (`__asm__ volatile("" ::: "memory")` in engine.c / motion.c). `.pool`
sections behind `X0X_POOL` (empty on host). **Dual core**: the 909 / 808 render on CPU1 via `plat_cpu2_*`; the host
runs the job inline (`plat_cpu2_run` calls fn at once). Static tables: drum samples + break bank ~0.9 MB of headers,
fine. Device files (hal/*.S, x0x.c, plat_fm1.c, usb.c, ota.c) are simply not built.

## 6. Persistence, audio
- Flash: Felucca `storage.c` A/B sector objects (st_read / st_erase / st_prog hooks, pure C, also used by
  tests/storage_test.c): OBJ_SET 0xFC000, SOUND / PAT0 / PAT1 0x97000.., SONG 0x9D000, PANEL 0xDC000, MOTION
  0xDE000 (2 x 4 KiB each, max 3,840 B payload); user break slots at 0xA0000 + k*0x14000 (3 slots, read in place,
  `SMP_USER_XIP`). Nothing above 0xDFFFF. The host sim keeps objects in RAM arrays instead: a core should build
  `storage.c` on the 1 MiB image (as the Felucca glue does) so the image is the device's.
- Audio: 44,100 Hz, **256-frame** blocks, stereo int32 Q23 (`/8388608`), master gain in engine_volume.

## 7. Host sim build / run (done)
`PATH=<python3 wrapper with DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib> sh host/build_host.sh`: **built in 3 s**
(Apple clang 21, no warnings shown). Headless run of a script (wait, shot, tap PLAY, wav 2 s, leds, expect playing 1):
OK, PNG of the 909 page, WAV written (empty default pattern, so silent), `expect playing 1` ok, 275 host
instructions / sample.

## 8. FM1VST core
- Closest glue: **felucca** for the HAL half (NOR image + storage hooks, tick debounce, exit / flash_stage
  machinery); the firmware half is a new "plat" glue modelled on `host/x0x_host.c` (plat_* over emu_hal_t: buttons
  via panel.c's matrix ids, `plat_fm1.c`'s LED mapping, `fm1_enc` per role, MIDI rings, store on storage.c).
- Effort: **M** (first of the family; then FoMni / FiMba / Fart Machine are S each).
- Risks: render is 256 frames vs the ABI's 128-frame audio calls (buffer one block: +2.9 ms latency); UI frame 16 ms
  vs the ABI's 15 ms (drive ui_frame from tick); break slots / IMA ADPCM import (web_slot_* exist as a model); the
  screen reader (`ann_wire_send`) needs a stub; version define; Tier 2 params would map onto `engine.h` targets.
