# core-glue/elasticat: Elasticat FM-1, a Fantasy CFW

| core | firmware | submodule | licence |
|---|---|---|---|
| `elasticat` | Elasticat FM-1 0.4.1 (E1-E6 + the UX redesign, Quixotic7) | `cores/elasticatfm1` (github.com/Quixotic7/elasticatfm1, `main`) | GPL-3.0-only |

Elasticat, the norns sample-mangling sequencer by Quixotic7, rewritten in C for the FM-1 as a host-only `.rfm1`
([docs/VIRTUAL-CARTS.md](../../docs/VIRTUAL-CARTS.md)): `fm1_add_rfm1(elasticat VIRTUAL NEEDS fs1 WANTS ain1 FOLDERS
audio=r data=rw MEMORY_MIB 128)`. 0.4.0 (E6) uses the host's live audio input when it has one (`"wants": ["ain1"]`,
VIRTUAL-CARTS.md 8: the INPUT machine, REC sampling into a pool slot, the takes saved in `/data/rec/`); without it
the cart runs as before. Samples come from the shared RemoteFM1 folder, `RemoteFM1/audio/` (WAV / AIFF, subfolders
allowed).

- **Native C against the ABI:** the firmware implements `fm1core_get` itself (`src/plat/ec_core.c`); its units are
  this glue's `EXTRA_SOURCES` (every `src/{plat,io,ui,dsp,seq,app}/*.c` but the repo's native file-API stand-in
  `src/io/ec_fs_native.c`), with `-Wall -Wextra -ffp-contract=off`. `core_elasticat.c` and `core_elasticat_desc.c` are
  empty placeholders (fm1_add_core wants a unit and a descriptor file). Natively the file API is
  `../common/rfm1_host.c` (no folder: the core plays its built-in loop); in the `.rfm1` it is the `rfm1_host_1` imports.
- **Version:** `src/plat/ec_version.h` (`EC_VERSION`).
- **Memory:** 128 MiB, most of it the sample store's static arena (`EC_SS_ARENA_MIB` 96, int16 stereo, ~9.5 min);
  the engine 16.7 MiB (`EC_DSP_MEM_MAX` 18 MiB), the app + its memorize copy 4.8 MiB; 4.9 MiB stay free.
- **The test** (`ctest -R rfm1_elasticat`): `test_folder/audio/testloop_bpm120.wav` (4 s, two bars of a synthesized
  drum loop, written by the repo's `host/make_testloop.py`) and `test_project.flash`, a saved project (7 KiB: the
  flash header + the project record; the core pads the rest) built through panel input by the repo's
  `host/glue_project.txt` + `host/trim_flash.py`: four tracks on the test loop (loop with trigs, p-locks and a region
  lock; granular; slice poly; chopped with a ratchet). PLAY at 1.5 s; the chord's RMS must be >= 0.02 and `ui_info`
  must say `ec0.4.1 play:1 file:testloop_bpm120.wav ready:1 missing:0 seq:on` (trigs fired). E6: the test loop is
  also fed as the live input (`rfm1_run --audio-in`, latency 256): `ain:1 lat:256`, and REC at 2 s + OCT+ at 2.2 s
  open the REC pop-up and arm a take (`recpop:1 rec:armed`; synced on PLAY, already playing, it waits, so nothing is
  written into test_folder). `rfm1_elasticat_ain_off` turns the input off at 5 s (`ain:0 lat:0`).
- **Speed** (wasm3, M-series Mac): 2.3x real time (0.4.0, the same with the input fed; 2.1x in 0.2.0) while playing the worst case (the repo's `ec_make_heavy`: six busy
  tracks across the E3 warp modes and R.POLY, an insert on every track, both sends, master COMP, LFOs everywhere);
  5.9x with the repo's v020 demo project.

## Building

```sh
cmake -S . -B build-rfm1 -DFM1_RFM1=ON -DFM1_RFM1_ONLY=ON -DFM1_ONLY_CORES=elasticat -DFM1_PUBLIC_BUILD=ON \
      -DFM1_EMCC=/opt/homebrew/bin/emcc -DCR_PYTHON=<RFM1Manager>/.venv/bin/python
cmake --build build-rfm1 -j8 --target elasticat_core rfm1_elasticat rfm1_run
ctest --test-dir build-rfm1 -R rfm1_elasticat -V
```

To build from a working tree instead of the submodule's pin: `-DEC_SOURCE=/path/to/elasticatfm1`.

## The submodule

`.gitmodules` points at `https://github.com/Quixotic7/elasticatfm1` (branch `main`), the URL it will have once the repo
is pushed. Until then the checkout was cloned from the local repo; to move the pin to new local commits:

```sh
git -C cores/elasticatfm1 -c protocol.file.allow=always fetch <path>/elasticatfm1 main
git -C cores/elasticatfm1 checkout FETCH_HEAD && git add cores/elasticatfm1
```

A fresh clone of this branch needs the GitHub repo pushed first (`git submodule update --init cores/elasticatfm1`).

No `conformance_elasticat.txt` yet: the native plugin build's conformance tests have not been run for this core.
