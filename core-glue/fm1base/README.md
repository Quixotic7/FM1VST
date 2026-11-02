# core-glue/fm1base: fm1-base apps

| core | firmware | submodule | licence |
|---|---|---|---|
| `amb` | AMB-1 1.3 | `cores/fm1-amb` @ v1.3, with `base` = fm1-base @ 15dff6d, `base/bsp` = felucca-bsp @ 6b087c2 | MPL-2.0 |
| `loom` | LOOM-8 0.6.1 | `cores/fm1-loom` @ v0.6.1 (3f41391), with `base` = fm1-base @ 71128a3, `base/bsp` = felucca-bsp @ 33cac69 | GPL-3.0-only (fm1-base MPL-2.0); un-gated 2026-10-21 (the owner's call: the 909 hi-hat) |
| `mutabledreams` | Mutable Dreams 1.4 | `cores/mutable-dreams` @ v1.4 (0321551), fm1-base and felucca-bsp vendored in it (no submodules) | GPL-3.0-only combined (app also MIT; Mutable Instruments code MIT; fm1-base MPL-2.0) |
| `remotefm1` | RemoteFM1 0.3.1 (Quixotic7) | `cores/RemoteFM1` @ v0.3.1 (e6fa19d), with `base` = fm1-base @ 934e40d, `base/bsp` = felucca-bsp @ 2cd696a, both patched at build time | MPL-2.0; FM6's MSFA parts Apache-2.0; Yamaha's DX7 ROM voices compiled in from the builder's .syx files |
| `berlinan` | Berlinan 0.5.1 (Jiří Píša) | `cores/fm1-berlinan` @ v0.5.1 (afe2d93), with `base` = fm1-base @ 15dff6d, `base/bsp` = felucca-bsp @ 6b087c2 (AMB-1's) | GPL-2.0-or-later; Berkeley SoftFloat 3e BSD-3-Clause; fm1-base MPL-2.0 |

fm1-base is Charles Vestal's platform for FM-1 apps, X0X's `plat.h` generalised (an app is `app.h` + `app.c` and an
`fm1.toml`). A core uses the plat family's shared core (`core-glue/plat/plat_glue.h`, README there) with:

- `fm1base_hal.h`: the plat HAL half plus what fm1-base's `base/device/plat_fm1.c` reaches beyond it: the glow's two
  strengths (`fm1_led_dim_level`: DIM LO = `FM1P_LED_FAINT`, DIM HI = `FM1P_LED_DIM`), the analog rows (`fm1_an`), the
  data area read in place (`FL_XIP`: the NOR image), `plat_lock`'s interrupt switch.
- `core_<app>.c`: `base/device/unity.c`'s include list without the hardware: plat.h, the app's header, gfx.c,
  `fm1b_config.h`, storage.c (the app's `APP_STORE_MAP`), announce.c, panel.c, `radio_none.c` (no Bluetooth or WiFi),
  plat_fm1.c, lib/touch.c (with `APP_TOUCH`), app.c. The app's `o2` units are their own units.
- The generate step: fm1-base's `build.py generate()` after the globals its `main()` sets (`APP`, `CONF` from the
  app's fm1.toml, `GEN`), in one Python expression (`core_amb.cmake`).

## The managers' SysEx

AMB-1's page is fm1-base's installer: the M-UPGRADE identity (`FM-1_7010300`, felucca-bsp's `chip/ota.c` through
`common/sysex_ota.h`) and the crash report's log readout (`F0 7D 4C 47 F7`: `main_fm1.c log_frame` reads the
hardware's crash record and registers; the core answers "no crash recorded", then "end"); a screen reader talks to
`announce.c`. The transport and the service's place in the loop are the plat family's (`../plat/plat_sx.h`,
README there). The remote control (`7D 4C 43`, `tools/fm1_remote.py`) is not answered. The update and UBOOT keys are
not taken.

## Velocity and pressure

An app with `APP_TOUCH` (CTL-1, MONK-1; not AMB-1) reads its keys through lib/touch.c from the scan's analog
readings. The core feeds `fm1panel_t.key_vel` / `key_lean` into lib/touch.c's own host model, as fm1-base's host
simulator and browser emulator do: `app_host_key(k, vel, press)` when a key lands (velocity 100 when the host gives
none), again when its lean changes, `(k, 0, 0)` when it is let go, and `app_host_tick()` every ms while `plat_sense`
is on. `APP_TICK` apps get `app_tick()` every ms, before that ms's render. AMB-1 uses neither; the path is compiled by
`fm1base_touch_check` (core_amb.c with `APP_TOUCH`) so it keeps building.

## Licence

AMB-1, fm1-base and felucca-bsp are MPL-2.0 file by file; `base/device/announce.c` is MIT. No source file carries
the MPL's Exhibit B, "Incompatible With Secondary Licenses" (only the three LICENSE texts contain the words, as the
licence's own template; felucca-bsp's LICENSING.md says its files carry none), so the files combine with this
GPL-3.0 glue under MPL-2.0 section 3.3 and stay MPL-2.0. Not gated.

## LOOM-8

LOOM-8 (Charles Vestal's pocket groovebox: eight tracks on nine engines, a 64-step sequencer, the user sample bank) is
pinned at v0.6.1 (2026-10-22), its first release without "-beta" (the owner asked for LOOM-8 while every upstream
release was still a "-beta" tag; v0.6-beta was the pin before). Its fm1-base is newer than AMB-1's, so `fm1base_hal.h` has a second part (the mid
LED plane `fm1_led_mid`, usb.c's out-ring room, the data area's extra windows, felucca-bsp's erase and write on the
NOR image, unity.c's stand-ins for the screen mirror); AMB-1 compiles it unused.

- **One file read from the glue.** `app/app.c` includes `app/plat_loom.c` when `PLAT_HOST` is off: the second core's
  hardware (cpu1/'s mailbox registers and `.c1_text` entry, the PC limits, felucca-bsp's supplies and crash record).
  `core_loom.cmake` writes a clang VFS overlay into the build folder (`-ivfsoverlay`, which emcc takes too) so the
  compiler reads `loom_plat.c` in its place; every file of the submodule, app.c included, compiles unmodified, on
  the device's paths (plat_fm1.c; synth.c's engine order and cost model).
- **The second core** (app/cpu1/, X0X's design): `loom_plat.c` runs each job at once, in order, on the host's one
  thread, as LOOM-8's own host simulator (`app/host.c`) and browser emulator do: engine.c's split render (the upper
  half of the voices and the drum lines, then the lower half "on core 1", summed in the one-core order) gives the
  same samples as on a two-core unit. A release build (APP_VERSION not "DEV") starts with two cores; nothing gives
  them up here. The render's load is 0 % (simulated time, as the browser emulator gives it).
- **Version and identity:** build.py --release's: APP_VERSION "0.6.1", FELUCCA_ID `FM-1_10000601` (fm1.toml's
  `id_prefix` FM-1_10 + 00 06 01), derived from the cmake's version. USB product (fm1.toml `usb_product`):
  "LOOM-8 FM-1" (plugin/EditorPort's table).
- **The managers' SysEx** (core_loom.c's header): the screen reader, the log readout, the app's own frames
  (`app_sysex`: samples.c's 7D 46 4C INFO, PING and the user sample bank, tools/loom_samples.py and its samples
  page), the M-UPGRADE identity, and fm1-base's backup and restore (backup.c: 0x42 streamed, 0x41 pulled, then
  RESTORE FROM USB? SEL / HOME), on a coroutine (the restore waits as on the device). `sysex_test_loom` uploads a
  sample as loom_samples.py does, backs up, restores (HOME, then SEL) and checks both after a reboot.
- **The user sample bank** is raw flash in three windows (0xC1000.., 0xE5000.., 0x93000..), read in place from the
  NOR image (`plat_data`), so it is kept with the plugin's state.
- **flash_sync** is ui.c's autosave without its waits: stopped, not recording, nothing asked, into the project's slot;
  AUTOSAVE off or a project with no slot: only SAVE writes.
- **Licence: un-gated (the owner's call, 2026-10-21).** The code is GPL-3.0-only on MPL-2.0 fm1-base / felucca-bsp;
  Braids, PSXVerb and NuSaw are MIT (LICENSING.md, carried whole in the credits with the OFL texts of its fonts); the
  four factory breaks are free loops by u/FCTRYBEATS with their written permission; the recorded instruments are VSCO 2
  CE (CC0). The factory 909 kit is rendered from 9W9 (GPL-3.0), its hi-hat (`app/samples/909_hat.wav`) from 9W9's hat
  samples (ER-99), the question that gated LOOM-8 with X0X. The owner decided to ship it unchanged: "ship the original,
  it is from the emulated 909 on the schwung firmware for Ableton Move that Charles Vestal made"; the credits name
  that origin. X0X stays gated (its own question, CORES-PLAN.md 6).

## Mutable Dreams

Mutable Dreams (Kid Bunka / Christoph Link: six synth tracks or a Lo-Fi kit on Plaits, Rings and Elements models,
Biomes, granular effects, User Banks) is pinned at v1.4 (2026-10-31). fm1-base and felucca-bsp are copies inside the
repository, not submodules. `core_mutabledreams.c` follows `core_loom.c` (fm1.toml `backup = true`, the same newer
fm1-base: plat_fm1.c's host reads need chip/ota.c before it), with the platform's screens in `FONT_S`
(`draw_text_box`, no `sys_text.c` in this fm1-base).

- **The engine is C++** (fm1.toml `o2`: `app/dsp/dreams.cpp` and twelve Mutable Instruments units). They are
  `EXTRA_SOURCES` straight from the submodule, as KARMA's are, with build.py's and `tools/build_local.py`'s flags:
  `-std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -DTEST`, `app/compat` first (its freestanding
  `<cmath>`, `<algorithm>`, `<new>`: no C++ library in the module), and `PLAT_HOST`, as fm1-base's host and browser
  builds give the o2 units (dreams.cpp's `.pool` section off).
- **The managers' SysEx:** sysex_dispatch.h's order: the screen reader, the log readout, the app's own (`app_sysex`,
  7D 4D 44: the diagnostics and the live editor's transfer), then, only for a frame none of them took, ota_service and
  usb_cmd (backup 0x42, restore 0x41 with RESTORE FROM USB? SEL / HOME).
- **Saving:** only SAVE (the Biome and the prefs) and the bank page (User Banks) write; `flash_sync` has nothing pending.
- **Not built:** the overload hook (`APP_OVERLOAD`: no audio timing on the host, the ECO step never fires), the splash
  and the boot chime's screen (the plat glue skips the splash; the chime plays), the update itself, Bluetooth / WiFi
  (fm1.toml has neither), USB audio in.
- Version and identity: build.py --release 1.4.0's, "1.4.0" and FM-1_710010400 (DEVICE-TEST-REPORT-1.4.txt).
- Licence texts in the .rfm1: its LICENSE (GPL-3.0), LICENSING-GPL.txt, LICENSE-MIT.txt, Mutable Instruments' MIT
  notice (`mutabledreams_mi_mit.txt`: the files' own header, gathered), fm1-base's and felucca-bsp's MPL-2.0 with
  LICENSING.md / NOTICE, the Barlow and Nunito OFL. `presets/` (JSON catalogs) is already compiled into
  `app/dsp/preset_bank.inc` upstream; nothing of it is shipped as a file.

## RemoteFM1

RemoteFM1 (Quixotic7) is the firmware that makes an FM-1 the panel of a host running .rfm1 files; here it is an
.rfm1 itself. Without a host, 0.3.x is an instrument of its own, and that is what the core runs: a DX7 (saadet's FM6,
`firmware/sa/sa_synth.c`, its own unit with `PLAT_HOST` as build.py gives the o2 units), Yamaha's eight ROM
cartridges and the user cartridges, the MIDI controller and its REMOTE mode, 16 loops, the arp, the scales and the
screens.

- **fm1-base and felucca-bsp patched, as build.sh does.** `core_remotefm1.cmake` copies `base/` (include, device,
  lib, host, ble, tools, bsp/chip, bsp/boards) into the build folder and applies `firmware/patches/fm1-base/*.patch`
  and `firmware/patches/felucca-bsp/*.patch` with `git apply` (exactly, or the configure stops). The unit reads
  fm1-base from there. The copy is made again when a patch or a pin changes; the submodule is never touched.
- **Two files through a clang VFS overlay:** `src/rfm_dev.c` (the port on the FM-1: usb.c's rings, the vendor bulk
  pair, the panel's DMA) is `remotefm1_port.c`, and `src/rfm_main.c` is the submodule's file with `rf_run` taking one
  turn a pass (it spins on the tick counter for 2 ms otherwise, and simulated time does not move inside a pass).
- **The port:** the host's MIDI is the USB port. USB SysEx in goes byte by byte to `rf_sx_tap`, as usb.c hands it, so
  a DX7 32-voice dump sent to the core becomes a user cartridge (rfm1_run `--sysex rom2a.syx`: bank 9, USER), and
  RemoteFM1's own F0 7D 52 46 messages reach its parser. SysEx out goes to the USB out ring. Nothing is ever full or
  waited for. No vendor bulk pair, no Bluetooth (`radio_none.c`), one core (`FM1B_CPU2` 0), no managers' SysEx.
- **LEDs:** `plat_lights_levels` (patch 0007, felucca-bsp's `fm1_led_lv`) shows as the mid plane.
- **The ROM voices:** `gen_dx7_rom.py --dir FM1_DX7_ROM_DIR` (a cache path, default `$ENV{DX7_ROM_DIR}`) writes
  `firmware/build/gen/dx7_rom.h` in the submodule's ignored build folder. Without the eight `.syx` files the configure
  fails when the build selects cores (`FM1_ONLY_CORES`) or makes `.rfm1` files; a native build of every core leaves
  RemoteFM1 out with a warning. `core_remotefm1.c` does not compile against a header without the eight cartridges,
  so an INIT VOICE-only build is never made. The `.syx` files and the header are never committed here.
- Version and identity: build.py --release 0.3.1's, "0.3.1" and FM-1_52000301. Not gated.

## Berlinan

Berlinan (Jiří Píša: a generative Berlin School MIDI sequencer, the C translation of Berlinan for macOS) is pinned at
v0.5.1 (afe2d93). Its fm1-base and felucca-bsp are AMB-1's (15dff6d, 6b087c2), so `core_berlinan.c` follows
`core_amb.c`.

- **It makes no sound.** `app_render` writes silence; the sequencer (app/runtime.c, from the 1 kHz `app_tick` and
  `app_loop`) sends its notes, CC, the MIDI clock, Start and Stop through `plat_midi_out` to the USB-MIDI out ring, and
  follows an incoming clock (EXT) from the host's MIDI in. rfm1_run's chord RMS is therefore 0: `RUN_ARGS --press
  PLAY@2000 --min-rms 0`, and `--midi-out` shows the Demo trio playing on channels 1-3.
- **The engine** (fm1.toml `o2`: app/engine/*.c and Berkeley SoftFloat 3e, app/engine/softfloat/*.c) is
  `EXTRA_SOURCES` straight from the submodule, with build.py's host flags (`-std=c99 -w -DPLAT_HOST`, each file's own
  directory, app/engine).
- **One file read from the glue:** app.c includes `app/libc/libc.c` (the device's memmove, strcmp, abs and 64-bit
  divides for the freestanding toolchain) when `PLAT_HOST` is off; a clang VFS overlay reads `berlinan_nolibc.c`
  (empty) in its place.
- **The managers' SysEx:** main_fm1.c's order: the screen reader, the log readout, the remote control (no lease in
  this fm1-base); no `app_sysex`; then ota_service (the M-UPGRADE identity) for a frame none of them took.
- **Saving:** only SAVE writes (pressed while playing, at the stop); `flash_sync` waits while a SAVE is pending.
- Version and identity: build.py --release 0.5.1's, "0.5.1" and FM-1_23000501. USB product (fm1.toml
  `usb_product`): "FM-Berlinan". Not gated: GPL-2.0-or-later; SoftFloat BSD-3-Clause; the Barlow OFL.

## A next fm1-base app

A `core_<app>.c` like `core_amb.c` (the app's hooks: its `sync` reads its own UI state), a descriptor, a cmake with
the app's GEN_ROOT, a conformance script and a golden script. BLE / WiFi apps would need the radio stand-ins of
`base/host/plat_host.c` instead of `radio_none.c`.
