# core-glue/fm1base: fm1-base apps

| core | firmware | submodule | licence |
|---|---|---|---|
| `amb` | AMB-1 1.3 | `cores/fm1-amb` @ v1.3, with `base` = fm1-base @ 15dff6d, `base/bsp` = felucca-bsp @ 6b087c2 | MPL-2.0 |

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

## A next fm1-base app

A `core_<app>.c` like `core_amb.c` (the app's hooks: its `sync` reads its own UI state), a descriptor, a cmake with
the app's GEN_ROOT, a conformance script and a golden script. BLE / WiFi apps would need the radio stand-ins of
`base/host/plat_host.c` instead of `radio_none.c`.
