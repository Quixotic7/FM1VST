# ChoralRoot 0.25.1 (hub id: choralroot)

Source: local `../ChoralRootFM1`, tag `v0.25.1` = 15ca8d0 ("0.25.1, the release tree"). Extracted with
`git archive v0.25.1` into `hubsrc/choralroot` (the local repo was not touched).
FM1VST pins 44453d0 (`git describe`: v0.13-36-g44453d0; 525 commits behind v0.25.1).

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` is Felucca's (GPL-3.0-only, Hügelton copyright kept) with ChoralRoot's
  additions; `LICENSES/` as Felucca's plus Melodee's ports it carries (CZ-1 uPD933 BSD-3-Clause, Prophet factory).
  **GPL-3.0-only**.
- 870 tracked files, ~136 k lines of C/H (the largest of the group).

## 2. Lineage
Felucca 1.0.x fork (firmware/src/storage.c, project.c, ui*.c, tests/hostsim.c are Felucca's), with Melodee's CZ-1,
Prophet and FM6 pieces ported (gen_cz1_factory.py, gen_prophet_factory.py, `melodee_*.h` headers), its own chord
UI (cr_*.c), a VA engine, QUAD, loops, the dual-core audio split (cr_cpu1.h), analog key sensing (cr_sense.c),
and its own emulator (tools/emu). FM1VST's emu_hooks.h / emu_hal_t come from here.

## 3. Host builds
- **Native emulator**: `tools/emu/` - emu.c (SDL2 window / audio / CoreMIDI, `--headless` simulated time, scripts),
  emu_fw.c (879 lines: the emu_fw_* hooks), emu_firmware.h (the include list), emu_hal_fw.h (the HAL: time, CPU lock,
  input, LCD, LEDs, the NOR image), keymap.c, pool_legacy.c, emu_smk_hal.h (SMK-37 board). Built by
  `tools/emu/build.sh` (clang + SDL2 from Homebrew) -> build/host/emu. Runs headless.
- **WASM**: `tools/emu/web/` - emu_web.c (258 lines, the same emu_fw hooks driven by the worklet), build_web.sh
  (emcc from ~/emsdk), emu.js, worklet.js, keymap.js, index.html, an SMK variant (smk/).
- Driving functions: the emu_hooks.h ABI (emu_fw_options / boot_options / init / tick(ms) / frame / idle /
  audio(s16 stereo, multiple of 128) / midi_in / midi_out_take / dump / stats / ui_info), unchanged in meaning; new:
  `emu_fw_stages`, `emu_fw_keys`, `emu_fw_layout`.
- Panel model (emu_hal_t): 27 keys (+ **key_vel[32], key_lean[32]**: velocity and aftertouch for keys 0..19, the
  analog rows), 14 buttons, 7 encoders + MASTER, LCD 240x240 RGB565 BE, LEDs **four levels**: led (lit),
  **led_mid** (fm1_led_dim2), led_dim, **led_faint**; PLAY green; `pa2` (panel dead when high).

## 4. Build steps
- `tools/build.py generate()`: ui_fonts.h, **cr_fonts.h**, ui_icons.h, ui_keycaps.h, ui_palettes.h, felucca_tables.h,
  **cr_layouts.h** (gen_layout.py), felucca_fm6.h, melodee_cz1.h, **melodee_prophet_factory.h**, felucca_samples.h.
  Python 3 + Pillow + fontTools; fonts and samples in repo; nothing fetched. 2.1 s here.
- FM1VST's `CR_GEN_NAMES` lacks cr_layouts.h and melodee_prophet_factory.h (they are generated anyway, but not
  dependency-tracked; tools/emu/build.sh regenerates both itself with `python3`).
- **Version file moved**: FELUCCA_VERSION "ChoralRoot 0.25.1" is now in `firmware/src/choralroot.c`, not cr_ui.c
  (FM1VST's `VERSION_FILE firmware/src/cr_ui.c` would yield "dev").

## 5. Blockers for a native macOS build
None (the emulator is the native build). Dual core: cr_cpu1.h's host stub runs core 1's job **inline** by default;
`EMU_C1=thread` (getenv in emu_fw_init) starts a pthread (CR_C1_THREAD 1 on non-emscripten) - in a plugin an
inherited env var would start a thread that is never joined before the module unloads (guard it). Other getenvs:
EMU_OUTPUT, EMU_CHORUS, EMU_MASTER, EMU_SHED_*, EMU_STAGES. `proc_pid_rusage` for the stage profile (only with
EMU_STAGES).

## 6. Persistence, audio
- 1 MiB NOR (emu_hal_fw.h `emu_flash[0x100000]`). Map (storage.c): settings 0xFC000; projects area 0x97000..0x9EFFF
  = VA patch store 0x97000, FM6 patch store 0x99000/0x9B000, CZ-1 tone store 0x9D000; user sample slot area
  0xA0000..: CZ-1 store 17..32 at 0xA0000/0xA1000, CZ banks 0xA2000..0xB1FFF, QUAD store 0xB2000/0xB3000, loops
  0xC8000..0xDBFFF; user preset banks 0xDC000..0xDFFFF; FM6 bank A 0x9F000 / B 0xFE000.
- Audio 44.1 kHz, 128-frame halves, stereo.
- New in the model: page programs mask ~1 ms (EMU_PROG_US), erase holes only when the output is audible.

## 7. Native try (done)
- generate: OK. `tools/emu/build.sh`: built in 7.7 s (SDL2 present). `emu --headless --script` (wait, key C4 down,
  expect sound): ran in 0.02 s, "expect: 1 ok", 207 non-silent blocks, CPU estimate 9 % avg. Screen
  `hubout/choralroot.png`, audio `hubout/choralroot.wav`.

## 8. FM1VST: what changed since 44453d0, what breaks
Closest: **core-glue/choralroot** (core_choralroot.c includes `tools/emu/emu_fw.c` unmodified).
Compiling the pinned glue against v0.25.1 (`gluetry/`, its fm1core.h pointing at 0.25.1's emu_hooks.h):
- core_choralroot.c's firmware side + exit/fopen redirection: **compiles**.
- core_choralroot_desc.c: compiles.
- core_choralroot_params.c: **2 errors** - `CU_FX[e].g[k]` (cr_ui.c's CU_FX table is now {name, send}; the
  per-effect cells moved to `CU_FXP[]` pages with xfp ids `XP_*` and `FC_*` codes: Sends / Filter pages, master
  filter 0.22). Beyond the compile errors, 525 commits of UI change (knob acceleration 0.22, chord-keys layouts
  0.25, pages) mean the map's targets and knob_target() need a review against cr_pages.c / cr_ui.c.
- **ABI break: emu_hal_t layout changed** (emu_hooks.h is part of FM1CORE_ABI 4): key_vel[32] and key_lean[32]
  inserted before led[] (every later field moves), led_mid[], led_faint[], pa2, strip_done / pump_mark function
  pointers, pump_off, knob_quick, keys_classic appended. Bumping the submodule means **FM1CORE_ABI 5**, and the
  Felucca / Melodee glue rebuilt against the same header; the host (engine, PanelComponent, RemoteLink) must zero
  the new fields (function pointers NULL), feed key_vel (MIDI / mouse velocity; 0 = 100) and draw led_mid /
  led_faint.
- Labels: `emu_fw_layout()` (Options > Chord Keys: FM-1 = 1 default since 0.25.1, Classic = 0) changes the panel's
  key labels: the descriptor's static button_names / keymap cannot follow it without an ABI hook.
- CMake: VERSION_FILE -> firmware/src/choralroot.c; GEN list + cr_layouts.h, melodee_prophet_factory.h; the
  reference emulator build (tools/emu/build.sh) still works.
**Effort: S** (params fix + review, ABI 5 plumbing for velocity / LED levels / layout).
**Risks**: replay_test baselines all move (0.25 sounds and screens differ); the ABI bump touches every core and
the plugin state format; FM-1 vs Classic key layout default changes what a saved plugin state's keys mean.
