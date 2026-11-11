# core-glue/fm1open: firmwares on M-VAVE's open APP (fm1_open)

| core | firmware | submodule | licence |
|---|---|---|---|
| `dx747` | DX747 0.1 (Esben von Buchwald) | `cores/fm1-dx747` @ v0.1 | GPL-3.0-only with the FM1 SDK linking exception (MSFA / Dexed: Apache-2.0; Open303, DaisySP, stmlib, emu2149, SEA: MIT; the factory DX7 bank: M-VAVE's V15, Apache-2.0) |

M-VAVE's open APP (m-vave/fm1_open) is an application (`app/`) over a binary SDK (`sdk/`: `libfm1_engine.a`, the
JieLi runtime, the headers in `sdk/include`). Its browser workbench builds the same APP for the host instead, with
the engine's open C source (`web/engine`, `web/synth`, `third_party/synth`) in the place of the library and
`web/engine/Simulator.c` as the platform layer (an in-memory LCD, LED levels, flash and MIDI queue; the controls as
a struct; `sim_*` calls for the harness). A firmware forked from it keeps that build (`tools/portable/browser-core.sh`).

The glue builds what that script builds (every `.c` under `app/` but `app/board/`, `web/`, `third_party/synth/`,
`FM1_HOST_TEST FM1_OPEN_WEB`, `-fwrapv -ffp-contract=off`, its include order), each file its own unit, and includes
`Simulator.c` unmodified in the core unit, where it drives it as the page does:

- **input**: the panel's held changes, taps (a press, then its release), detents and MASTER through `sim_control`, at
  the top of tick / frame / idle; the APP's matrix order is the FM-1's (`fm1p_panel_default`, `FM1P_KEYMAP`)
- **audio**: `sim_render` once per 128-frame block (its one block of latency; the control task every 10 ms of
  frames); the engine's 24-bit stereo as int16
- **MIDI**: USB-MIDI packets to `fm1_app_midi_received` / `fm1_app_sysex`; `fm1_midi_send` on the USB port (the
  capture Simulator.c keeps for its tests) back as packets
- **LEDs / LCD**: the 41 lamp levels (0..8) by key id; the native RGB565 pixels when the frame revision moved
- **flash**: `fm1_flash_*` renamed away in Simulator.c and served from `common/nor.c`'s image (the APP's
  `GROOVE_FLASH_BYTES` at 0); a blank image gets the factory sample bank as `sim_boot(1)` writes it; no erase stall

Left out (as in the browser): Bluetooth (its settings stay, scans and links fail), the DIN/TRS port, USB and
Bluetooth audio in (silence), the battery, the safe-boot chord. The proprietary `libfm1_engine.a` is never linked.
