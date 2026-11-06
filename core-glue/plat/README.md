# core-glue/plat: Charles Vestal's plat.h firmwares

| core | firmware | submodule | licence |
|---|---|---|---|
| `x0x` | X0X 1.1.0 | `cores/fm1-x0x` @ v1.1.0 | GPL-3.0-only |
| `fomni` | FoMni-1 0.4 | `cores/fm1-fomni` @ v0.4 | GPL-3.0-only |
| `fimba` | FiMba-1 0.6.1 | `cores/fm1-fimba` @ v0.6.1 | GPL-3.0-only |
| `fartmachine` | Fart Machine One 1.0.0 | `cores/Fart-Machine-One` @ V1.0.0 | GPL-3.0-only, **GATED** (the recordings) |
| `ravel` | Ravel 1.5.0 (De-Pattern) | `cores/Ravel_FM1` @ v1.5.0 | GPL-3.0-only |
| `trax` | FM1-TRAX 0.9.3 (jartificer) | `cores/fm1-trax` @ v0.9.3 | GPL-3.0-only (Terminus font: OFL-1.1) |
| `sidj` | SiDJ 1.4.0 (Thomas Jansson) | `cores/fm1-sidj` @ v1.4.0 | GPL-3.0-only (the SID engine GPL-2.0-or-later, the fonts OFL-1.1) |
| `penumbra` | Penumbra 1.0 (Lunar Eclipse Forge) | `cores/Penumbra` @ v1.0 | GPL-3.0-only (Barlow, Terminus fonts: OFL-1.1) |
| `mope1` | MOPE-1 0.3.0 (slothpallas) | `cores/mope-1` @ v0.3.0 | GPL-3.0-only (Barlow font: OFL-1.1) |
| `perf1` | PERF-1 0.16.0 (deamonSLINKY) | `cores/PERF-1` @ v0.16.0 | GPL-3.0-only (fonts: OFL-1.1) |
| `carnacos` | Carnac OS 0.9.6 (OrbVorgis) | `cores/carnac-os` @ v0.9.6 (X0X in its own `x0x-firmware/`) | GPL-3.0-only (Barlow, Terminus fonts: OFL-1.1) |

Ravel keeps its own portable platform (`firmware/app/plat.h`) over X0X's Felucca layer, adapted (`firmware/fm1`): the
unit is its device platform unit (`ravel_fm1.c`: `storage.c`, `ota.c`, `panel.c`, `plat_fm1.c` with `projstore.c`'s
compressed projects) on the HAL half, the instrument (`ravel_app.c`) its own unit. Its panel numbers the labels and
knobs its own way (`FM1CORE_PL_BTN_OF` / `_ENC_OF`) and draws a frame every 33 ms (`FM1CORE_PL_FRAME_MS`). Its
transfer page (projects and sound banks, `rv_xfer.c`, commands 0x60..0x64 in the update's framing) reads and writes
the NOR image's slots as on the device.

FM1-TRAX (a Mega Drive tracker on FM1-GM's platform, FoMni's layout) is its device unity build (`main.c`: the UI,
`settings.c`'s song slots and instrument library in `storage.c`'s chunk store, `link.c`) on the HAL half, with
`libc.c`'s string helpers (its memset / memcpy / memcmp renamed away); the chips and the tracker (`dsp/`, `trk/`) are
their own units (`trax_*.c`).

SiDJ (a C64 music deck on X0X's platform: a cycle-exact 6510 and reSID-derived SID chips playing the player's .sid
files, remixed live) is its device unity build (`sidj.c`: the UI, the settings, the song store in its pool
0x93000..0xF2FFF read in place from the image, the kept MIDI clocks in `OBJ_SOUND`) on the HAL half; the SID, the C64
and the engine are their own units (`sidj_*.c`). Its MIDI clock in is timed by a hook (`FM1CORE_PL_MIDI_IN_HOOK`: F8
into its own ring, never queued, as its usb.c does), its clock out leaves at the frame it was rendered for
(`FM1CORE_PL_TICK_HOOK`). Left out: USB audio (the five-channel stream), the TRS jack. A fresh flash gets the author's
two tunes the site and the browser emulator come with (`web/site/songs/`: Debut & Credit and TwinCross, unmodified, not
under the GPL: LICENSING.md's terms are a licence section of the .rfm1), compiled in from the submodule at configure
time (`<build>/core_sidj_tunes/sidj_tunes.h`) and put in a song store never written; the run test taps PLAY.

Penumbra (a space-drone instrument, a fork of X0X) is its device unity build (`penumbra.c`: the UI, `settings.c`'s
settings, journeys and loop in `storage.c`'s objects) on the HAL half, X0X's second-core and guard stubs, the DSP, the
clock, the journey, the looper and the engine their own units (`penumbra_*.c`, tools/build.py's O2_UNITS). Its gfx.c
has one palette (`palette_set` stubbed in the unit). The journey plays at power-on and never falls silent, so the
device's AUTOSAVE (a second of silence) would never write: `flash_sync` does what SAVE does (written at once).

MOPE-1 (a wavetable synth with a backing band, a FiMba-1 fork) is FiMba's glue over its own files: the device unity
build (`main.c`: the UI with `px.c` and `blob.c`) on the HAL half, the instrument (`dsp/synth.c`) its own unit
(`mope1_synth.c`). The band playing defers `flash_sync` (the device never autosaves under it).

PERF-1 (a fork of HYP-1, itself of AMB-1, on FoMni's layout) is its device unity build (`main.c` without the hardware:
UI, project, `plat_fm1.c`) on the HAL half, the instrument with Melodee's FM6 engine (`dsp/amb.c`) its own unit
(`perf1_amb.c`). Its second core (`EN_CORE2`) and USB MIDI host (`PERF_USB_HOST`) are built out (the reverb runs
inline); its sliced erase keeps the audio going through a write (`plat_store_seamless`), so the NOR image's erase
stall is 0 ms; the keys' second dim plane (`fm1_led_dim2`) is plat_glue.h's optional `FM1CORE_PL_DIM2_PLANE`. Its
SysEx (the web editor, the player's DX7 voices, BACKUP AND RESTORE: F0 7D 50 ..) is usb.c's `px_end` / `dx_end` and
main_fm1.c's `dx_service` / `px_service`, copied unchanged into `core_perf1.c`, on `plat_sx.h`'s transport; a pass
that hands a voice to the engine draws no frame (the engine takes it a render later, and autosave would store the
voices without it).

Carnac OS (a wavetable synth) replaces X0X's app with its own (`src/hardware/synth_main_fm1.c`: build_fm1.py cuts
X0X's `x0x.c` at its app) and carries X0X as a submodule of its own: the unit (`core_carnacos.c`) is X0X's platform
pieces (`storage.c`, `ota.c`, `slots.c`, `panel.c`, `plat_fm1.c`), the UI and screens, and `synth_panel.c` (the panel,
the settings and the preset store: the file the firmware's own browser emulator also runs); the DSP its own unit
(`carnacos_dsp.c`). Its PCM waves and the Foundations bank live in the store flash on a unit (uploaded by the
installer): a flash image without them gets the release's own at power-on (`carnacos_images.c`), then they are read in
place, as through XIP (which needs `common/nor.c`'s image 4-byte aligned). The 1.5 s splash is kept, as the device
and its browser emulator show it. Power-on always plays the factory's performance 0; a saved one loads from PRESETS.

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
- The updater: the update key (F0 22 24 35 7F F7) and UBOOT's (F0 22 24 35 7D F7) are not taken (see "The managers'
  SysEx"). The render load (no wall clock): the overload guards see 0 %.

### The managers' SysEx

`plat_sx.h` (on `core-glue/common/sysex.h`) is usb.c's SysEx path: SysEx packets go to one frame buffer (only the
frames a service reads; F0 refused while one waits: `midi_in` 0, retry), never into the app's MIDI queue; replies
leave ahead of the notes. `plat_glue.h` runs the unit's service where `main_fm1.c` runs it (after the UBOOT hold,
before the frame), with the firmware's own handlers compiled from the submodule:

| core | manager | handlers |
|---|---|---|
| `x0x` | `web/backup/index.html` (backup / restore), the break-loop page, a screen reader | `ota.c` (identity `FM-1_9010100`), `backup.c` (with `main_fm1.c`'s hooks redone in `core_x0x.c`), `editor.c` (INFO, PING, the sample slots), `announce.c` |
| `fomni` | the installer, a screen reader | `ota.c` (`FM-1_8000400`, via `common/sysex_ota.h`), `announce.c` |
| `penumbra` | the installer, a screen reader | `ota.c` (`FM-1_73010000`, via `common/sysex_ota.h`), `announce.c` |
| `fimba`, `fartmachine` | the installer | `ota.c` (`FM-1_8000601`, `FM-1_701`) |
| `carnacos` | the installer (`web/site/installer.html`: INFO, the PCM waves' PCM_ID / upload) | `ota.c` (`FM-1_900`), the editor (7D 46 4C) redone from `synth_main_fm1.c` with `pcm_store_fm1.c`, `wt_store_fm1.c` |
| `trax` | the web editor (`web/editor.html`: songs and library banks), the installer | `link.c` (F0 7D 46 44: HELLO, LIST, READ, WRITE / DATA, DELETE), `ota.c` (`FM-1_601000903`) |
| `sidj` | the songs page (`web/site/index.html#songs`, `fm1songs.js`: list, add, fetch, delete, play), `tools/fm1_sid.py`, the installer | `songs.c` (F0 7D 53 cmd: INFO, ENTRY, PUT_BEGIN / DATA / END, GET, DELETE, WIPE, PLAY, STATS, OUTPUT, RX, USB), `ota.c` (`FM-1_64010400`) |
| `mope1` | the installer | `ota.c` (`FM-1_6673000300`; its load and crash replies, cmd 0x12 / 0x13, left out) |
| `perf1` | the web editor (`web/editor`: parameters, DX7 voices), the installer (identity, BACKUP AND RESTORE) | `px_end` / `dx_end`, `dx_service` / `px_service` (F0 7D 50 ..; DX7 bulk / single voice), `ota.c` (`FM-1_7001600`) |

X0X's restore waits as the device does (`ota.c` read requests to the host, then RESTORE FROM USB? with SEL / HOME):
its service runs on a coroutine (`common/coro.h`, `FM1CORE_PL_SX_CORO`) that yields at `fm1_wdt_feed`; while it waits
the passes only resume it (no frame drawn, `flash_sync` deferred), the tick, the input and the audio go on. The
restore writes the NOR image as the device does (staged copies, the journal record at 0xE1000, `rs_boot` at power-on).
The update never runs: the identity is answered, the update key is not taken, so an installer times out and writes
nothing.

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
flash image (the plugin's state), so a staged image that holds slots (a device's flash) plays them. A slot is loaded
over SysEx as on the device (`editor.c`: SMP_BEGIN / WRITE / END, `tools/fm1_sample_upload.py`).

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
