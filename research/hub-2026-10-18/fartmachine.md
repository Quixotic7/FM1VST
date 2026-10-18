# Fart Machine One 1.0.0 (adamcharleswilsonmusic-code/Fart-Machine-One, tag V1.0.0)

Clone: `scratchpad/hubsrc/fartmachine` @ `784347a` (2026-10-11, "Add files via upload": uploaded through the GitHub
web UI, so history is thin). Note the tag is upper-case `V1.0.0`.

## 1. License, size
- `LICENSE` GPL-3.0 text; `LICENSING.md`: "a derivative of FoMni, licensed **GPL-3.0-only**"; FoMni's licensing
  appended; `LICENSES/AC79-SDK-LICENSE.txt` (Apache-2.0, for the SDK blobs in the package only).
- **Audio licence caveat**: the current firmware embeds eleven user-supplied recordings (`samples/originals/*.wav`
  -> `firmware/src/dsp/sample_bank.h`) reported to come from Pixabay; `samples/ATTRIBUTION.md` says their individual
  licences and redistribution permission are **not independently verified** and the GPL does not relicense them.
  Pixabay's terms restrict standalone redistribution of the audio.
- 145 files; 58 .c/.h, **14,769 C lines** (incl. the generated 653 KB `sample_bank.h`, committed).

## 2. Lineage
**FoMni fork** (`host/omni_host.c`, `dsp/omni.c` renamed engine, plate reverb kept as `dsp/plate.h`), plat.h
identical to FoMni's; new playback engine, importer (`tools/import_samples.py`), UI and cartoon artwork
(`app/cartoon.h`). Platform = Felucca via X0X via FoMni.

## 3. Host builds
- **Native host simulator**: `host/omni_host.c` + `dsp/omni.c`, `host/build_host.sh` (cc, `-DOM_HOST`;
  `FMO_TEST_BANK=1` swaps in a test bank). Headless script runner as FoMni's; extra tests in `tests/fmo/`
  (dsp, sequencer, ui, names; two emu_test .mjs).
- **WASM**: `web/emu/omni_web.c`, `build.sh`, worklet.js, index.html (FoMni's).
- Drivers: `boot()`; `run_ms`: `om_render(blk, 256, master)`; `ui_frame` every 16 ms.
- Panel: FoMni's plat.h (label buttons, keys, encoder roles, MASTER; plat_leds + glow + play_red). No velocity.

## 4. Build steps
`tools/gen_font.py` (Pillow + libraqm; `requirements-build.txt` pins Pillow==11.3.0) -> `felucca_font.h`. The sample
bank is committed (regenerate with `tools/import_samples.py` only if samples change). Nothing fetched.

## 5. Native macOS blockers
None (as FoMni). 653 KB of int16 sample tables: fine.

## 6. Persistence, audio
storage.c objects as FoMni (OBJ_PROJ 0xFC000, OBJ_PANEL 0xDC000). Includes a step sequencer (`tests/fmo/sequencer*`)
whose state lives in the project object. 44,100 Hz, 256-frame blocks, stereo int32 Q23.

## 7. Host sim build / run (done)
**Built in 0.4 s**. Headless script (two key taps, 1.5 s WAV, shot, leds): runs, WAV peak 4,551 (sound).

## 8. FM1VST core
- Closest glue: the plat glue (X0X / FoMni family).
- Effort: **S** after the family glue.
- Risks: mainly **legal**: shipping a core embeds the Pixabay recordings whose redistribution terms are unverified
  (a plugin bundle is a standalone redistribution). Build from source with the test bank, or ask the owner / author
  before distributing. Thin git history (web uploads) makes pinning by tag the only anchor.
