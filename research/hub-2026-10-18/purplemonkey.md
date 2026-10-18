# PurpleMonkey FM-1 0.11 (Quixotic7/PurpleMonkeyFM1, tag v0.11)

Clone: `scratchpad/hubsrc/purplemonkey` @ `7590d4a` (2026-10-09, "0.11: the TALK letter always shows ...").
Owner's local checkout: `/Volumes/Q7Media-2025/Projects/Github/PurpleMonkeyFM1/PurpleMonkeyFM1` at
`v0.11-1-gb574e75` (one later commit, site only: "the four pets in their play pose"), clean, with a built
`build/pm_emu`. The outer `/Volumes/.../PurpleMonkeyFM1/` is a workspace (ChoralRootFM1, Felucca, Designer copies),
not a repo.

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md`: **GPL-3.0-only**, derived work of ChoralRoot FM-1 (Felucca fork);
  `LICENSES/` carries msfa Apache-2.0, DaisySP / Rings / Fukiai MIT, uPD933 BSD-3, Inter Tight OFL. SLOOP 2.4.1
  drum synth (GPL-3.0-only). Artwork made with ChatGPT by the owner, distributed under GPL-3.0-only. LPC speech frames
  are analysis numbers (no Reed samples, no TI ROM data).
- 1,399 files; 203 .c/.h, **80,194 C lines** (mostly ChoralRoot / Felucca; PurpleMonkey's own `pm_*.c` +
  `purplemonkey.c` ~3,850 lines).

## 2. Lineage
**ChoralRoot** `44453d0` (2026-10-07) by `git archive` (docs/UPSTREAM.md), with SLOOP 2.4.1's `drum_synth.c` ->
`pm_drum_synth.c` and its kit generator; Felucca 1.0.1 as ancestor. Device unit `firmware/src/purplemonkey.c`
(FELUCCA_SEQ 0, FM6 only at 8 voices, no VA / CZ / SAMPLE / GRAIN / DRUM / SLICER).

## 3. Host builds
- **Native emulator: ChoralRoot's** (`tools/emu/emu.c`, keymap.c, emu_img.c, emu_midi.c: SDL2 + CoreMIDI + zlib)
  with PurpleMonkey's firmware side: `tools/emu/pm_emu_fw.c` (the emu_fw_* hooks) + `tools/emu/pm_firmware.h` (the
  ONE include list: tests/hostsim.c with FELUCCA_* off, emu_hal_fw.h, pm_engine.c, pm_out.c with a `mix_block`
  shim, audio.c, gfx.c, panel.c, pm_sound.c, pm_ui.c). Built by `tools/emu/build_pm.sh` -> `build/host/pm_emu`.
  **Headless**: `--headless --script FILE [--frames N] [--shot x.ppm] [--wav out.wav]`, simulated time.
- **WASM**: `tools/emu/web/{build_web.sh, emu_web.c, worklet.js}` (ChoralRoot's web emulator).
- `tools/emu/emu_hooks.h` is **byte-identical** to the one FM1VST's core-api/fm1core.h includes
  (`cores/ChoralRootFM1/tools/emu/emu_hooks.h`), so the emu_fw_* entry points already match the core ABI.
  `emu_hal_fw.h` differs from FM1VST's pinned ChoralRoot copy (older base): build against PurpleMonkey's own.
- Panel model: emu_hal_t directly (keys, matrix buttons, encoders, MASTER, emu_hal.led / led_dim, LCD 240x240
  RGB565 big-endian). Controls: keys play; FX = DRUMS, SEL = SYNTH, PLAY = BEAT, HOME = knobs back to normal;
  SELECT / K1..K4 = PET, SPEED, BUSY, BOUNCE, SQUISH; PRESETS / ALGORITHM = SOUND, WORLD.
- No key velocity sensing (MIDI CC1 / channel aftertouch only, from ChoralRoot's core.h).

## 4. Build steps
`tools/gen.sh`: ChoralRoot / Felucca `tools/build.py generate()` (fonts, icons, keycaps, palettes, tables,
felucca_fm6.h, melodee_cz1.h, felucca_samples.h from synthesised waves) + `gen_drumkits.py --kits
VINTAGE,LATIN,808,JAZZ` -> pm_drumkits.h, `gen_pm_patches.py` -> pm_fm6.h, `gen_pm_rig.py` -> pm_rig.h (from
assets/purplemonkey/rig-64*), `gen_pm_sprites.py --names-only` -> pm_sprites.h. Needs Pillow + fonttools + libraqm
(gen.sh exports DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib itself, but `sh` strips DYLD_* under SIP, so the
python it runs must get the env another way: `PYTHON=<wrapper>`). Nothing fetched (`tools/fetch_cc0.py` exists but
gen.sh does not call it).

## 5. Native macOS blockers
None: it is a Mac emulator already. No CPU1 use (the ChoralRoot base predates the dual-core line). `emu_hal_fw.h`
uses os_unfair_lock + pthread for the realtime mode; uncontended under the core ABI's single thread (as the
ChoralRoot core).

## 6. Persistence, audio
**Nothing is stored**: purplemonkey.c builds the flash driver for the update only; `emu_fw_options` forces RAM
only. A core's flash image stays erased (flash_dirty never moves). Audio: 44,100 Hz, HALF_FRAMES 128 (matches the
ABI block), stereo; FM6 + drum synth + LPC speech.

## 7. Host emulator build / run (done)
`PYTHON=<raqm python> sh tools/emu/build_pm.sh`: **built in ~5 s** (SDL2 from Homebrew was already installed).
Headless `pm_emu --headless --script tools/emu/scripts/pm_midi.txt --shot --wav`: 4 / 4 expects ok, 271
non-silent blocks, device CPU estimate 7 % avg / 9 % max per block, no windows opened.

## 8. FM1VST core
- Closest glue: **choralroot**: `core_choralroot.c` includes `tools/emu/emu_fw.c`; a PurpleMonkey core includes
  `tools/emu/pm_emu_fw.c` instead, same exit / atexit / flash_stage redirections, include path
  `build/gen, firmware/src, tools/emu`.
- Effort: **S**.
- Risks: fm1_add_core's GEN hook runs only `build.generate()`: the four pm_* generators need an extra custom
  command (or run tools/gen.sh); version string from `FELUCCA_VERSION` (pm_firmware.h defaults "EMU"); a Tier 2
  map is small (5 knobs + sound / world); `emu_fw_options` ignores flash paths (fine: nothing to persist).
