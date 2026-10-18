# AMB-1 1.3 (charlesvestal/fm1-amb, tag v1.3)

Clone: `scratchpad/hubsrc/amb` @ `188b0a4` (2026-10-17). Submodules fetched with
`git submodule update --init --recursive --depth 1`: `base` = charlesvestal/fm1-base @ `15dff6d`, `base/bsp` =
hugelton/felucca-bsp @ `6b087c2`. (A plain `--depth 1` clone leaves `base/` empty: the build refuses.)

## 1. License, size
- `LICENSE` = **MPL-2.0** (`SPDX-License-Identifier: MPL-2.0` per file). LICENSING.md: AMB-1 0.9 and earlier were
  GPL-3.0-only on Felucca's platform; that code is gone, the platform is fm1-base + felucca-bsp, both MPL-2.0
  (Felucca's author relicensed it: felucca-bsp#4). MPL-2.0 is GPL-compatible, so a GPL-3.0 FM1VST core may include
  it (the MPL files stay MPL).
- App only: 53 files, 9 .c/.h, **4,105 C lines** (`app/app.c`, `ui.c`, `project.c`, `host.c`, `dsp/amb.c`).
  fm1-base: 11,150 C lines (plus felucca-bsp).

## 2. Lineage
**Charles Vestal's fm1-base platform** (the generalised successor of X0X's plat.h): `base/include/plat.h`, device
side `base/device/{unity,plat_fm1,main_fm1,panel,storage,gfx,lcd,announce,...}.c` on **felucca-bsp** (the MPL
hardware layer from Felucca); app contract `app_init / app_ui_init / app_frame / app_input_only / app_render /
app_say` + optional hooks (APP_TICK, APP_BOOT, APP_LOOP, APP_KEY_SENSE, APP_SYSEX ...). `app/fm1.toml` declares the
build (no BLE, USB audio in). fm1-base's other apps: CTL-1 (BLE MIDI), Radio (WiFi); its DESIGN.md lists X0X /
NuSaw / SPH-16 / XY-1 as second-core users.

## 3. Host builds
- **Native host simulator**: `base/host/plat_host.c` (1,011 lines: plat.h on the host, includes `app.c` and the
  app's units; `-DPLAT_HOST`) + `app/host.c` (AMB's own expect / commands / speakdump). Built by
  `python3 base/tools/build.py app --host` (`./build.sh --host`) -> `app/build/host/amb_host`. Headless script
  runner (wait / press / tap / key [VEL [PRESS]] / strum / turn / spin / master / midi [PORT] / ble / wav / shot /
  leds / out / expect / reboot / data / listen / said ...). `base/host/build_host.sh` is a stale FoMni leftover.
- **WASM**: `base/web/emu/emu_web.c` via `build.py app --web` (emcc), plus AMB's `web/app/{amb_api.c,build.sh,
  worklet.js}`.
- Drivers (plat_host.c): `boot()` (palette_set, app_init, app_boot?, app_ui_init); `run_ms`: per ms app_tick?,
  app_loop?, `app_render(blk, APP_BLOCK, APP_GAIN(master))` per block, `app_frame()` every 16 ms else
  `app_input_only()`.
- Panel model: label buttons (B_*), 27 keys, 7 encoder roles, MASTER 0..4096; LEDs
  `plat_lights(buttons, keys, dim_buttons, dim_keys, glow)` (+ legacy plat_leds / glow / play_red); MIDI ports USB /
  BLE / TRS (`plat_midi_in(pkt, &port)`, `plat_midi_send(pkt, to)`); LCD 240x240 RGB565 via gfx.c (canvas sized by
  APP_CANVAS_PX).
- **Velocity / pressure**: fm1-base carries Charles Vestal's key sensing (`lib/sense.c` "from fm1-velocity, MONK-1
  and CTL-1": velocity from the carbon pill's ADC readings, release velocity, poly pressure; `lib/touch.h`,
  `APP_TOUCH` / `APP_KEY_SENSE`). **AMB-1 does not use it** (no APP_TOUCH / APP_KEY_SENSE in app.h). Any fm1-base app
  that does would need emu_hal_t to carry velocity / pressure (an ABI bump).

## 4. Build steps
`build.py generate()` (needs APP / CONF from main(): the app dir and its fm1.toml): `base/tools/gen_font.py`
(Pillow + libraqm) -> `felucca_font.h`; `gen_words.py app/words.tsv` -> `ann_words.h` (also writes
`app/web/words/<hash>.json`); the app's own `gen = [...]` lines (AMB has none). Nothing fetched for --host
(docker / podman only for the device firmware).

## 5. Native macOS blockers
None for AMB (no asm, single core, `PLAT_POOL` empty on host). fm1-base BLE / WiFi parts are compiled out unless
fm1.toml asks (`FM1B_BLE`, `FM1B_WIFI`); the host has stand-ins.

## 6. Persistence, audio
- `APP_STORE_MAP {0xFC000, 0xDC000}`: OBJ_PROJ (the project: every piece's params, pool, lights; format 4) and
  OBJ_PANEL; fm1-base `device/storage.c` A/B objects (st_read / st_erase / st_prog hooks), payload <= 3,840 B. Plus
  `plat_data` (0x97000..0xE0000) for read-in-place app data (unused by AMB). Host sim keeps objects in RAM.
- 44,100 Hz, `APP_BLOCK` 256 frames, stereo int32 24-bit, gain from MASTER (`APP_GAIN`).

## 7. Host sim build / run (done)
`<raqm python> base/tools/build.py app --host`: **built in 1 s**. Headless (wait 1.5 s, shot, tap PLAY, 4 s WAV,
leds): runs; WAV peak 1,455 (quiet ambient, it plays from power-on), all key LEDs lit.

## 8. FM1VST core
- Closest glue: felucca (HAL half) + a new **fm1-base glue** whose firmware half is `base/host/plat_host.c` minus
  the script runner (plat.h on emu_hal_t, `device/storage.c` on the 1 MiB image).
- Effort: **M** for the first fm1-base core; after that every fm1-base app (CTL-1, Radio, future ones) is **S** (a
  descriptor and button names), unless it uses BLE / WiFi / key sensing.
- Risks: fm1-base is "beta: expect plat.h and fm1.toml to change before 1.0": pin each app's submodule commit; the
  generate step needs fm1.toml parsing (fm1_add_core's `build.generate()` hook does not set APP; call
  `build.py app --host`'s generate path or a small wrapper); recursive submodules in FM1VST's checkout;
  MPL-2.0 file notices must be kept.
