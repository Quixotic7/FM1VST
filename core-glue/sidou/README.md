# core-glue/sidou: SIDOU

| core | firmware | submodule | licence |
|---|---|---|---|
| `sidou` | SIDOU 1.1.0 (Denis Sablić and the SIDOU contributors) | `cores/sidou` @ v1.1.0 (1a75687), with `third_party/libresidfp` (host tests and the table generator only: not built here) | GPL-3.0-or-later; the image GPL-3.0-only (Felucca's HAL, usb.c, ota.c); reSIDfp-derived tables and GoatTracker 2's player GPL-2.0-or-later, FM-1 SID's PSID design BSD-2-Clause, Rings' reverb MIT |

SIDOU is bare-metal firmware on Felucca's HAL (`platform/fm1/hal`, vendored), not a Felucca fork: no `web/sim`, no
`firmware/src`, so neither felucca_web nor plat fits it. Its own CMake builds a portable instrument (`app/`, `core/`:
the C a host test builds too) and a device layer (`platform/fm1`). The glue builds both as SIDOU's CMake does, over
this folder's HAL:

- **The units:** `app/` and `core/` as their own units, the lists read from their `add_library` lines
  (`core_sidou.cmake`); `platform/fm1`'s `main.c` (in `core_sidou.c`), `midi.c` (with Felucca's `usb.c`, `ota.c`,
  SIDOU's `usb_audio.c` and `usb/midi_uart.c`: `sidou_midi.c`), `audio.c`, `store_fm1.c`, `input.c`, `ui.c`, each in
  a one-line wrapper. `lcd.c` (the ST7789's SPI driver) is replaced by `sidou_lcd.c` (the panel's RAM).
- **The HAL (`hal/`):** every `platform/fm1/hal/fm1_*.h` the firmware includes, on the host: TIMER4 is the simulated
  clock, a wait yields, the NOR is `common/nor.c` read in place for the store's XIP window, the key scan's state is
  written by the tick, the USB SIE answers and never runs. Read in the headers' place through a clang VFS overlay,
  also where `usb.c` and `midi_uart.c` include them by a relative path.
- **The overlays** (`core_sidou.cmake`, each replaced line checked at configure time, GPL notice added): `main.c` (the
  main loop's `idle` is a yield, an update request is not run, `fm1_cstart` is never run), `audio.c` (no top-level
  `.set`, the ring's wait yields), `store_fm1.c` (no `cli`/`sti`, ALNK0's pending register a variable), `input.c` (the
  wait for a scan frame yields). `sidou_pre.h` turns the firmware's section attributes into alignments (Mach-O
  rejects `.pool`), in every unit but one that names a function `section` (`app/chip_view.c`, which places nothing).
- **The main loop** runs on a coroutine (`common/coro.h`; Asyncify in the `.rfm1`): `fm1_cstart`'s boot guard, then
  `sidou_main`, resumed four passes a device ms. The audio block (128 frames, SIDOU's own) is `audio_fill`; it ends at
  the next ms's start, so the store's write window (250 us after a block) opens in the pass after it.
- **MIDI:** in through `usb.c`'s `midi_in_event` (SIDOU's raw hook into its own parser and ring, `usb.c`'s SysEx
  assembly for the update protocol), out from `usb.c`'s rings (the update protocol's replies first). USB is up and
  configured from the boot. The M-UPGRADE identity answers `FM-1_964` (SIDOU's release identity, its `ci.yml`); SIDOU's
  own SysEx (`7D 53 44`: INQ, DIR, GET, PUT, the Songs page) answers. An update request (`F0 22 24 35 7F F7`) is
  ignored. The TRS jack and USB audio out are not there.
- **flash_sync:** the store has no work in hand and the settings' debounced save (5 s) has been written.
- **A fresh flash** plays the full boot once (the C64 prompt, then the title card, which waits for a key); its
  capture takes the first button. A note ends it too, and plays: `rfm1_run`'s chord needs no `RUN_ARGS`.

`conformance.txt`: the native conformance test passes (a USER sound kept in LAB and the settings; the short boot after
the reboot; the boot guard's UBOOT).
