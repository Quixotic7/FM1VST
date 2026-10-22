# core-glue/fm1base: fm1-base apps

| core | firmware | submodule | licence |
|---|---|---|---|
| `amb` | AMB-1 1.3 | `cores/fm1-amb` @ v1.3, with `base` = fm1-base @ 15dff6d, `base/bsp` = felucca-bsp @ 6b087c2 | MPL-2.0 |
| `loom` | LOOM-8 0.6-beta | `cores/fm1-loom` @ v0.6-beta (e88f2e0), with `base` = fm1-base @ 71128a3, `base/bsp` = felucca-bsp @ 33cac69 | GPL-3.0-only (fm1-base MPL-2.0), **GATED** (the 909 hi-hat) |

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
pinned at v0.6-beta although the pins are otherwise stable releases: the owner asked for it, and every upstream
release so far is a "-beta" tag. Its fm1-base is newer than AMB-1's, so `fm1base_hal.h` has a second part (the mid
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
- **Version and identity:** build.py --release's: APP_VERSION "0.6-BETA", FELUCCA_ID `FM-1_10000600` (fm1.toml's
  `id_prefix` FM-1_10 + 00 06 00), derived from the cmake's version. USB product (fm1.toml `usb_product`):
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
- **Licence: gated.** The code is GPL-3.0-only on MPL-2.0 fm1-base / felucca-bsp; Braids, PSXVerb and NuSaw are MIT
  (LICENSING.md, carried whole in the credits with the OFL texts of its fonts); the four factory breaks are free loops
  by u/FCTRYBEATS with their written permission; the recorded instruments are VSCO 2 CE (CC0). The factory 909 kit is
  rendered from 9W9 (GPL-3.0), and its hi-hat from 9W9's hat samples (ER-99): the same unverified provenance that gates
  X0X (they may be TR-909 ROM recordings). So LOOM-8 is gated with X0X (CORES-PLAN.md 6), and an answer for one
  un-gates both.

## A next fm1-base app

A `core_<app>.c` like `core_amb.c` (the app's hooks: its `sync` reads its own UI state), a descriptor, a cmake with
the app's GEN_ROOT, a conformance script and a golden script. BLE / WiFi apps would need the radio stand-ins of
`base/host/plat_host.c` instead of `radio_none.c`.
