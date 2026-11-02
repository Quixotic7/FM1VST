# core-glue/melodee: Melodee and its forks

| core | firmware | submodule | licence |
|---|---|---|---|
| `melodee` | Melodee 1.5.1 (Kerem Kilic, Ellic Studio) | `cores/melodee` @ v1.5.1 (6a608ba) | GPL-3.0-only (MIT, BSD-3, Apache-2.0, OFL parts) |
| `tessera` | Tessera 3.2.2 (Theo Dainis) | `cores/tessera` @ v3.2.2 (80bf086) | GPL-3.0-only (MIT, Apache-2.0, OFL parts) |

Melodee is Felucca's fork with its own engines (Prophet, FM6, CZ-1, SID, the x0x 909), storage, web editor and
updater. It has no emulator of its own: the glue builds its unmodified sources as one translation unit, the way its
host tests do (`tests/hostsim.c` for the sound side, then the UI, the storage and the SysEx), over a host HAL.

Each core's files: `core_<id>.cmake` (fm1_add_core / fm1_add_rfm1), `core_<id>.c` (the unit), `<id>_fw.c` (the
`emu_fw_*` hooks: power-on, the main loop on a coroutine, the 1 ms tick, audio, MIDI), `<id>_firmware.h` (the include
list and the build options), `core_<id>_desc.c` (the descriptor's name and repo), `conformance_<id>.txt`.

Shared by the family:

- `melodee_hal.h`: the HAL on the host (time, the CPU lock, the LEDs as the firmware's own arrays with their three dim
  planes, the LCD, the audio DMA halves, storage.c's NOR hooks on `common/nor.c`).
- `melodee_glue.h`: the ABI entry points `fm1core_me_*` (the panel in and out, the LEDs as levels, the flash, the
  halt), included by each unit after its firmware side.
- `melodee_desc.h`: the descriptor; `MELODEE_DESC_NAME`, `MELODEE_DESC_URL` and `MELODEE_DESC_PARAMS` (1: the unit
  has a Tier 2 parameter map) come from `core_<id>_desc.c`.

A core's main loop and power-on are its own (`<id>_fw.c` follows its `main.c`), and so is its include list: the
forks drop and add sources (Tessera: no CZ-1 or Prophet, its generators `gen.c`, `tessera.c`, `pid.c`, `cord.c`,
`splash.c`).

**melodee**: the 1.5.1 release's options (`melodee_firmware.h`), with the CZ-1 and Prophet-5 factory sounds compiled
in (un-gated by the owner, 2026-10-19; no raw .syx). The Tier 2 parameter map is `core_melodee_params.c`. The
managers' SysEx: editor.c (FL), the general preset frames, the Prophet's Sequential frames, ota.c's M-UPGRADE identity
`FM-1_900`.

**tessera**: Tessera, a generative firmware (thirteen generators on four tracks, EVOLVE / FORM, cords), grown from
Melodee (Tess). Its 3.2.2 dual-core release's options (`tools/build.py build_app`, `release.yml`): FM6, ORACLE, TRIO,
PHYS, DRUM in; SID, SID KIT and MACRO (Plaits) out; the Mono look; help texts packed. The version is the tag's (the
source's `MELODEE_VERSION` default is stale; `build.py --release` stamps it), and so is the M-UPGRADE identity,
`FM-1_83773022` (`FM-1_8377` + X YY Z). The managers' SysEx: editor.c (FL: the web editor at
rastapopolix.github.io/tessera/webapp/editor/, which looks for a port named Tessera), the general preset frames,
ota.c's identity. No parameter map yet (every knob relative). Its waits in `splash.c` run on hostsim's no-op
`fm1_delay_ms`, so the boot mosaic is laid in a few device ms. The USB port is "Tessera" (usb.c STR2).

Not in either core: USB audio, the USB host, the serial console, the update session, the USB UBOOT key (dropped,
printed), the TRS jack (its parser is built, nothing arrives). OCT- + OCT+ held 5 s, or two failed boots in a row,
halts the core (exit 3).
