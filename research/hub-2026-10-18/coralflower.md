# CoralFlower 0.2 (FicusFicusov/CoralFlower @ v0.2, 4542beb)

Source: hubsrc/coralflower. Host build tried: hubbuild/coralflower.

## 1. License, tag, size
- `LICENSE` GPL-3.0 text; `LICENSING.md` says "GPL-3.0-only as a whole"; 168 `GPL-3.0-only` SPDX files, 1 BSD-3-Clause,
  2 MIT; `LICENSES/` holds the third-party texts (Departure Mono OFL, Inter Tight OFL, the msfa / Dexed and SDK notes via
  `docs/LICENSING-choralroot.md`).
- Tag v0.2 ("Release notes 0.2"). **Version string mismatch**: `firmware/src/coralflower.c` still says
  `#define FELUCCA_VERSION "CoralFlower 0.1"` (the release build sets it with `--release`). Pass the version explicitly.
- 325 files, 181 C/H, ~78.5 k lines (`firmware/src` ~67 k).

## 2. Lineage
- **ChoralRoot FM-1 fork** (LICENSING.md: "CoralFlower is a fork of ChoralRoot FM-1 by Quixotic7 ... which is itself a
  fork of Felucca"). It keeps ChoralRoot's platform, engines (FM6, VA), effects, sound stores, the `tools/emu` emulator,
  the web emulator and the generators. Its own code: `firmware/src/cf_*.c/h` (chord model, player, arp, looper, FX
  chain, UI) and `firmware/src/flora/` (the plant / coral renderer).
- Which ChoralRoot: the 0.22 line (`choralroot.c` "ChoralRoot 0.22"; `CR_CPU1 1`: the bass part as the second core's
  job). Its `tools/emu/emu_hooks.h` sits **between** FM1VST's pinned ChoralRoot (44453d0) and ChoralRoot's current
  0.24 (268d3d6). It adds `led_mid`, `led_faint`, `pa2`, `strip_done`, `pump_mark`, `pump_off`, `emu_fw_stages`,
  `emu_fw_keys`; it lacks current CR's `key_vel`, `key_lean`, `knob_quick`.

## 3. Host builds
- **Native emulator**: `tools/emu/` (ChoralRoot's: `emu.c` SDL2 window + CoreMIDI, `emu_fw.c` the firmware unit
  through `emu_firmware.h`, `emu_hal_fw.h` the Mac HAL, `emu_hooks.h` the boundary), built by `tools/emu/build.sh`
  (needs Homebrew SDL2 for the window only).
- **Headless native**: `tools/emu/cf_headless.c` + `cf_headless.sh`. This is the browser emulator's firmware side
  (`tools/emu/web/emu_web.c`, `-DEMU_WEB`) built natively, driven by a script (`key`, `btn`, `hold`, `turn`, `clock`,
  `midi`, `shot`, `wav`, `dump`; `CF_FLASH=path` for a persistent image).
- **WASM**: `tools/emu/web/` (`emu_web.c`, `emu.js`, `worklet.js`, `index.html`, `build_web.sh`, Emscripten), which
  is the "Play it in your browser" page.
- Hooks: identical to ChoralRoot's (`emu_fw_options` / `boot_options` / `init` / `tick` / `frame` / `idle` / `audio` /
  `midi_in` / `midi_out_take` ...), so the FM1VST choralroot glue's structure applies one to one. Panel: `emu_hal_t`
  (keys, buttons, taps, `enc[7]`, master, `led` / `led_dim` (+ `led_mid` / `led_faint`), `lcd` big-endian RGB565, the
  `btn_id` / `enc_id` maps).

## 4. Build steps
- `tools/build.py generate()`: `gen_aa_font.py` (Inter Tight -> `ui_fonts.h`), `gen_pixfont.py` (Departure Mono ->
  `cf_fonts.h`), `gen_aa_icons.py`, `gen_aa_keycaps.py`, `gen_ui_palettes.py`, `gen_tables.py`,
  `gen_fm6_patches.py`, `gen_samples.py`. It needs Pillow + fontTools (FM1VST's `CR_PYTHON` works) and took 1.6 s.
- `gen_samples.py` makes generated material only when `assets/samples-cc0` is empty (it is). `tools/fetch_cc0.py` would
  download CC0 samples but is not part of `generate()`. Nothing is fetched by default.
- GEN list for `fm1_add_core`: `ui_fonts.h cf_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h
  felucca_fm6.h felucca_samples.h`. (`tools/emu/build.sh` also checks for `melodee_cz1.h`, which `generate()` does not
  write, but the unit compiled without it.)
- Version regex on `firmware/src/coralflower.c` gives "0.1" at v0.2. Override it.

## 5. Blockers for a native macOS build
- None. It is a Mac-first emulator like ChoralRoot's. The section attributes are handled the way ChoralRoot's host
  build handles them.
- Dual core (`CR_CPU1`): on the host the stub core (`firmware/src/cr_cpu1.h`) runs the bass job inline when handed.
- `emu_hal_fw.h` uses an `os_unfair_lock` / pthread CPU lock (realtime mode), as ChoralRoot's. FM1VST's choralroot
  glue already lives with that.
- **The ABI clash**: `core-api/fm1core.h` includes FM1VST's pinned `cores/ChoralRootFM1/tools/emu/emu_hooks.h`, and
  CoralFlower's `emu_fw.c` includes its own `emu_hooks.h` with a different `emu_hal_t`. Both are `#pragma once` but
  different files, so the typedef is redefined. A CoralFlower glue must either rename the firmware's struct
  (`#define emu_hal_t cf_emu_hal_t`, `#define emu_hal cf_emu_hal` around the include) and copy fields to and from the
  ABI's `emu_hal` each tick / frame, or FM1VST must move the ABI to a hal of its own (an ABI bump) that every
  ChoralRoot-family fork maps into.

## 6. Persistence and audio
- ChoralRoot's 1 MiB image (`emu_hal_fw.h` `EMU_FLASH_SIZE 0x100000`): settings `0xFC000`, projects / loops
  `0x97000..0x9EFFF`, and the CR sound stores (`storage.c` map). Staging via the same `emu_flash_path` stdio hooks, so
  the choralroot glue's `fopen` / `fread` redirection works unchanged.
- Audio: 44.1 kHz, `HALF_FRAMES` 128 (= EMU_BLOCK), stereo, s16 out of `emu_fw_audio`.

## 7. Host sim tried
- `generate` ran in 1.6 s. `cf_headless` (`emu_web.c` + `cf_headless.c`) built in 4.6 s with Apple clang, no errors.
  `tools/emu/emu_fw.c -c` built in 3.8 s.
- Ran a script (C4 + E4, PRESETS +2, shot, dump) headless in 0.13 s: `out.wav` (peak 7717), the screenshot shows the
  CHORD SOUND list, the keycap row and the plant. `dump` reports 51 chord / 13 bass sounds. Works.

## 8. FM1VST glue
- Closest: **choralroot** (`core_choralroot.c` includes the submodule's `tools/emu/emu_fw.c` as is).
- Effort: **S** if FM1VST's ABI hal matches (copy `core-glue/choralroot`, swap the submodule, new button names and
  descriptor, drop or redo the Tier 2 map). **S-M** with the `emu_hal_t` adapter while the ABI stays on the old
  ChoralRoot header.
- Risks: `emu_hal_t` drift (every CR-family fork has a slightly different one); the Tier 2 param map
  (`core_choralroot_params.c`, 1133 lines) targets ChoralRoot's statics and will not compile against CoralFlower's
  `cf_*` UI, so start with `params = NULL`; the version string; `HALF_FRAMES` 128 vs others' 256 is fine here.
