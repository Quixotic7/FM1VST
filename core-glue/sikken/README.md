# core-glue/sikken: SikkenWorks' native FM-1 firmwares

| core | firmware | submodule | licence |
|---|---|---|---|
| `etch` | ETCH 1.88 (Joppe Sikken, SikkenWorks) | `cores/ETCH` @ V1.88 (2eaaad8) | GPL-3.0-only (Felucca's HAL and drivers: GPL-3.0-only) |
| `rupture` | RUPTURE 1.48 (Joppe Sikken, SikkenWorks) | `cores/RUPTURE` @ V1.48 (bea4bfe) | GPL-3.0-only (Felucca's HAL and drivers: GPL-3.0-only) |

ETCH is a drawn-wavetable synth (three 128-point waves A, B and C drawn on the panel, A/B morphing, wave effects,
modulation, eight voices, an arpeggiator, a sequencer, 63 screens in its ADVANCED workflow). RUPTURE is a mono bass
synth (Cycle Exchange Synthesis or a band-limited saw, two sub-oscillators, a ladder filter, Channel EQ, eight
effects, a step sequencer and an arpeggiator) built from ETCH's app: its source is
`vendor/etch-v1.88-source/firmware/fm1-native`. Neither is a Felucca fork with a browser unit (so not felucca_web):
each is a native FM-1 application (ETCH's `firmware/fm1-native`) on Felucca's HAL (`hal/fm1_*.h`), with no emulator
of its own. The glue is Tonitor's kind (`core-glue/tonitor`), with one difference: the firmware's own reset entry
runs, not a twin of it. One glue serves both:

| file | what |
|---|---|
| `sikken_hal.h`, `hal/fm1_flash.h` | the host HAL, read in place of the device's `hal/fm1_*.h` |
| `ota_trusted_loader.h` | the JieLi update loader's array, all zero |
| `sikken_glue.h` | the ABI entry points after `bringup.c` (`fm1core_sk_*`) |
| `sikken_desc.h` | the descriptor (each core's `core_<id>_desc.c` names it) |
| `sikken.cmake` | `sikken_vfs` (the clang VFS overlay), `sikken_units` (build.py's other units' flags) |
| `core_<id>.c`, `core_<id>.cmake`, `conformance_<id>.txt` | each core: its unit, its build, its conformance script |

- **The unit** (`core_<id>.c`): `bringup.c`, which is one translation unit on the device too (its reset entry
  `fm1_cstart`, the main loop, the UI, `usb.c`, `ota.c`, the store's flash side, the audio ISR), then
  `sikken_glue.h`. The other units `build.py` links (the DSP, the effects, the codecs, the factory sounds: 13 for
  ETCH, 15 for RUPTURE) are `EXTRA_SOURCES`, as they are, with `build.py`'s settings for the release (`--storage
  --updater --multi-project`; `RUPTURE_BUILD` for RUPTURE). USB audio out is left off.
- **The HAL** (`sikken_hal.h`, `hal/fm1_flash.h`): a clang VFS overlay (`sikken.cmake`) reads them in place of the
  device's `hal/fm1_*.h`. `lcd.c` (the ST7789 driver) is built unmodified over an SPI half that decodes CASET, RASET
  and RAMWR into the panel's RAM. The flash is `common/nor.c`'s image, under the device's range rules.
- **The coroutine**: `fm1_cstart` runs from its first line on `common/coro`. It yields where the device waits for its
  interrupts (`fm1_wdt_feed`, `fm1_delay_ms`), but not on the feed straight after a flash operation, because the
  store's 256-byte reads would stretch the boot over seconds. `frame()` and `idle()` resume it once per device ms.
  `tick` stands in for TIMER5 (etch_ms, the input), `audio` for ALNK0 (one 128-frame DMA half per block, Q15 << 7 to
  s16), and `midi_in` / `midi_out_take` for EP1, on `usb.c`'s own rings.
- **Input**: a button press is held at least as long as the main loop's button poll, by level, so a shorter tap is not
  lost: 40 ms for RUPTURE (it reads them every 25 ms), 120 ms for ETCH (its 100 ms control tick). A note key: 40 ms
  (the audio ISR reads them every block).
- **Overlays of firmware files**: RUPTURE's `bringup.c` has one pi32v2 instruction (`csync` in `ru_trs_service`),
  a compiler barrier here; `core_rupture.cmake` checks the line at configure time. ETCH's has none: its `bringup.c`
  is the submodule's. `ota_trusted_loader.h` is an all-zero array of its size, so the JieLi update loader
  (Apache-2.0) stays out of both: the update request is dropped as it arrives.
- **SysEx**: the M-UPGRADE identity answers each firmware's package identity (`RELEASE-MANIFEST.json`
  firmware_identity, the same for every release): `FM-1_1500` for ETCH, `FM-1_968` for RUPTURE. `ota.c` answers
  ETCH's live-preset commands (0x60 to 0x64) in both. ETCH's
  [Project-Preset-Manager](https://sikkenworks.github.io/ETCH/ETCH-Project-Preset-Manager.html) accepts only
  `FM-1_1500` identities, so it works with the ETCH core (probe, RAM snapshot read), not RUPTURE's.
- **ETCH's cold boot**: ETCH asks for a workflow (SIMPLE or ADVANCED, K1 chooses, SEL confirms) at every cold boot
  and is silent until then; `rfm1_run`'s RUN_ARGS and the `sound` script press SEL.
- **RUPTURE's first sound**: about 5.7 s after power-on, as on the device. The firmware's own staged output startup
  (`native_output_startup.h`, `audio_target.c` `native_output_poll`) keeps the DMA at digital zero through the splash
  (40 ms + 24 x 56 ms + 675 ms, ~2.06 s), USB state stable for 2 s, PC6 then 1 s of analog settle, 500 ms of a quiet
  screen and the PLAY frame, then a 186 ms fade in. A key released before then is not heard (one held over it is);
  after it a key sounds within a few ms. The conformance key goes down at 6.3 s (`settle_ms 6000`), `rfm1_run`'s
  chord at 6 s (RUN_ARGS `--settle 6000`). ETCH has the same gate, after its workflow prompt.
- **The version**: the tags are `V<major>.<minor>` with a capital V (`V1.88` is 1.88). The cmake reads it from
  `RELEASE-MANIFEST.json`: `"release_version"` (ETCH), `"version"` (RUPTURE).
- **Not here**: the TRS jack, USB audio, the update session and the USB UBOOT key (both dropped, printed). OCT- + OCT+
  held 5 s, or OCT- held at power-on, halts the core (exit 3).
