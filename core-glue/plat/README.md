# core-glue/plat: Charles Vestal's plat.h firmwares

| core | firmware | submodule | licence |
|---|---|---|---|
| `x0x` | X0X 1.1.0 | `cores/fm1-x0x` @ v1.1.0 | GPL-3.0-only |
| `fomni` | FoMni-1 0.4 | `cores/fm1-fomni` @ v0.4 | GPL-3.0-only |
| `fimba` | FiMba-1 0.6.1 | `cores/fm1-fimba` @ v0.6.1 | GPL-3.0-only |
| `fartmachine` | Fart Machine One 1.0.0 | `cores/Fart-Machine-One` @ V1.0.0 | GPL-3.0-only, **GATED** (the recordings) |

The fm1-base cores (`core-glue/fm1base`, AMB-1) use the same HAL half and shared core.

## How a core is put together

These firmwares talk to the hardware only through `app/plat.h` (buttons by label, keys, encoders by role, LEDs,
MIDI, store objects). On the device `app/plat_fm1.c` implements it on Felucca's HAL, and `app/main_fm1.c` boots and
runs the main loop. A core keeps the device's own files and replaces only the HAL and the main loop:

- `plat_hal.h`, the HAL half: `fm1_ms` (the ABI's device ms), `fm1_ticks` (24 MHz), the debounced matrix `fm1_in`
  (a press at once, a release after 9 ms held, as Felucca's `fm1_input_tick`), `fm1_enc_take`, the LED planes
  `fm1_led` / `fm1_led_dim` / `fm1_led_dim_div`, `lcd_fill` / `lcd_blit` into the panel's RAM, usb.c's MIDI rings,
  and storage.c's `st_read` / `st_erase` / `st_prog` on `core-glue/common/nor.c`'s 1 MiB image.
- The firmware's own `app/panel.c` (the label -> matrix map, its calibration in `OBJ_PANEL`) and `app/plat_fm1.c`
  (the LED picture: lit, the glow, the dim keys, PLAY's green LED at column 8 / row bit 1), unchanged. That covers the
  forks' differences: FiMba's `plat_leds(buttons, keys, dim_keys)` comes with its own plat_fm1.c. What plat_fm1.c
  needs beyond the HAL half is stubbed in the core's unit (X0X: the second core runs its job inline, as x0x_host.c).
- `plat_glue.h`, the shared core: the ABI's entry points. `main_fm1.c`'s loop is one pass per device ms: MASTER
  through its IIR, the OCT- + OCT+ hold (UBOOT after 5 s: halted, `exit(3)`), the firmware's extras, then `ui_frame`
  every 16 ms or `ui_input_only`. The boot guard counts failed boots (four: UBOOT).
- `core_<id>.c`, the firmware unit: the include list of the device's unity build (`x0x.c`, `main.c`) without the
  hardware, as the firmware's own host simulator orders it, and the hooks `plat_glue.h` names (boot, render, MASTER,
  the lazy save, ...). The DSP files are their own units (`<id>_<file>.c` wrappers), as the firmware builds them.
- `plat_desc.h` + `core_<id>_desc.c`: the descriptor. No parameter map yet: every knob is a relative turn.

### Audio and time

The firmwares render 256-frame stereo int32 (Q23) blocks. The core renders them in `tick(ms)`, before the ms's UI,
each when its first frame falls due, as the host simulators' `run_ms` does; `audio()` takes 128-frame blocks from
that queue (Q23 >> 8, saturated). The UI reads engine state (a string's level, the sequencer's step), so rendering
on `audio()`'s pull (`common/fifo.c`) would put the screens 2.9 ms behind the simulators'. A flash erase silences
45 ms of blocks (`fm1nor_stall_take`); the frames rendered meanwhile are dropped.

### What is not emulated

- The splash and its 330..430 ms of delays: the UI starts at power-on, as in the firmware's own host simulator and
  browser emulator (the hub's "Play in browser").
- HARDWARE CALIBRATION (OCT- + OCT+ held at power-on: a blocking loop) and SAFE MODE (two failed boots).
- USB SysEx: the screen reader (`announce.c` links, nobody listens), the updater, X0X's sample upload (`editor.c`) and
  backup (`backup.c`). The render load (no wall clock): the overload guards see 0 %.

### Versions

Each firmware's version is a build-time define defaulting to "DEV" (`X0X_VERSION`, `OM_VERSION`, fm1-base's
`APP_VERSION`); the unit sets it from `FM1CORE_VERSION`, the hub's version given to `fm1_add_core(VERSION ...)`.

### Storage

storage.c's A/B sector objects (2 x 4 KiB each, payload <= 3,840 B) in the image: FoMni / FiMba / Fart Machine One
`OBJ_PROJ` 0xFC000 and `OBJ_PANEL` 0xDC000; X0X `OBJ_SET` 0xFC000, SOUND / PAT0 / PAT1 0x97000.., SONG 0x9D000,
PANEL 0xDC000, MOTION 0xDE000. `flash_sync` writes what AUTOSAVE would (stopped, quiet); while a firmware would not
erase (playing, frozen grains) it returns 0. Fart Machine One keeps no project ("predictable defaults on every
power-on").

**X0X's break-loop slots** (3 x 80 KiB at 0xA0000 + k x 0x14000: header, up to 16 IMA ADPCM zones) are read in place
from the image (`SMP_USER_XIP`) and scanned at power-on (`smp_user_scan`), as on the device. They persist with the
flash image (the plugin's state), so a staged image that holds slots (a device's flash) plays them. The core has no
way to upload a slot (the device's is `editor.c`'s SysEx over USB).

## Golden checks

`plat_golden.cmake` builds each firmware's host simulator from the submodule (`<id>_hostsim`, as its
`host/build_host.sh` or `build.py --host`) and `plat_golden_test` runs `golden_<id>.txt` (the simulators' own script
grammar) through both; each `shot` compares the core's screen with the simulator's PNG. All five match exactly
(0 pixels differ). The simulator's now_ms is the core's ms + 1 (its first frame is at now_ms 1, the core's at ms 0).

## For later

- Into `common/`: the clocked render queue (render by the device clock, read by `audio()`), a "pull or clock" mode
  for `fifo.c`; `plat_golden_test`'s PNG reader and script driver for any glue whose firmware has a host simulator.
- Velocity: these four read the keys on / off. fm1-base apps with `APP_TOUCH` get `key_vel` / `key_lean` through
  lib/touch.c's host model (core-glue/fm1base).
