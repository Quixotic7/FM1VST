# core-glue/rupture: SikkenWorks' native FM-1 firmwares

| core | firmware | submodule | licence |
|---|---|---|---|
| `rupture` | RUPTURE 1.48 (Joppe Sikken, SikkenWorks) | `cores/RUPTURE` @ V1.48 (bea4bfe) | GPL-3.0-only (Felucca's HAL and drivers: GPL-3.0-only) |

RUPTURE is a mono bass synth (Cycle Exchange Synthesis or a band-limited saw, two sub-oscillators, a ladder filter,
Channel EQ, eight effects, a step sequencer and an arpeggiator). It is not a Felucca fork with a browser unit (so not
felucca_web): it is a native FM-1 application, `vendor/etch-v1.88-source/firmware/fm1-native` (by way of SikkenWorks'
ETCH), on Felucca's HAL (`hal/fm1_*.h`), with no emulator of its own. The glue is Tonitor's kind
(`core-glue/tonitor`), with one difference: the firmware's own reset entry runs, not a twin of it.

- **The unit** (`core_rupture.c`): `bringup.c`, which is one translation unit on the device too (its reset entry
  `fm1_cstart`, the main loop, the UI, `usb.c`, `ota.c`, the store's flash side, the audio ISR). The glue comes after it.
  The other 15 units `build.py` links (the DSP, the effects, the codecs, the factory sounds) are `EXTRA_SOURCES`, as
  they are, with `build.py`'s settings for the release (`--storage --updater --multi-project`, `RUPTURE_BUILD`). USB
  audio out is left off.
- **The HAL** (`rupture_hal.h`, `hal/fm1_flash.h`): a clang VFS overlay (`core.cmake`) reads them in place of the
  device's `hal/fm1_*.h`. `lcd.c` (the ST7789 driver) is built unmodified over an SPI half that decodes CASET, RASET
  and RAMWR into the panel's RAM. The flash is `common/nor.c`'s image, under the device's range rules.
- **The coroutine**: `fm1_cstart` runs from its first line on `common/coro`. It yields where the device waits for its
  interrupts (`fm1_wdt_feed`, `fm1_delay_ms`), but not on the feed straight after a flash operation, because the
  store's 256-byte reads would stretch the boot over seconds. `frame()` and `idle()` resume it once per device ms.
  `tick` stands in for TIMER5 (etch_ms, the input), `audio` for ALNK0 (one 128-frame DMA half per block, Q15 << 7 to
  s16), and `midi_in` / `midi_out_take` for EP1, on `usb.c`'s own rings.
- **Input**: a press is held at least 40 ms. The main loop reads the buttons every 25 ms, by level, so a shorter
  tap would be lost.
- **Overlays of firmware files**: `bringup.c`'s one pi32v2 instruction (`csync` in `ru_trs_service`) becomes a
  compiler barrier; `core.cmake` checks the line at configure time. `ota_trusted_loader.h` is an all-zero array of
  its size, so the JieLi update loader (Apache-2.0) stays out of the core: the update request is dropped as it arrives.
- **SysEx**: the M-UPGRADE identity answers `FM-1_968`, which every RUPTURE release uses (`RELEASE-MANIFEST.json`
  firmware_identity). `ota.c` also answers ETCH's live-preset commands (0x60 to 0x64). RUPTURE's site has no manager
  page, and ETCH's Project-Preset-Manager refuses any identity that is not `FM-1_1500`.
- **The version**: the tags are `V<major>.<minor>` with a capital V (`V1.48` is 1.48). `core.cmake` reads it from
  `RELEASE-MANIFEST.json` `"version"`.
- **Not here**: the TRS jack, USB audio, the update session and the USB UBOOT key (both dropped, printed). OCT- + OCT+
  held 5 s, or OCT- held at power-on, halts the core (exit 3).
