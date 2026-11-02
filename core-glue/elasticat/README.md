# core-glue/elasticat: Elasticat FM-1, a Fantasy CFW

| core | firmware | submodule | licence |
|---|---|---|---|
| `elasticat` | Elasticat FM-1 0.0.1 (E0, Quixotic7) | `cores/elasticatfm1` (github.com/Quixotic7/elasticatfm1, `main`) | GPL-3.0-only |

Elasticat, the norns sample-mangling sequencer by Quixotic7, rewritten in C for the FM-1 as a host-only `.rfm1`
([docs/VIRTUAL-CARTS.md](../../docs/VIRTUAL-CARTS.md)): `fm1_add_rfm1(elasticat VIRTUAL NEEDS fs1 FOLDERS audio=r
data=rw MEMORY_MIB 128)`. Samples come from the shared RemoteFM1 folder, `RemoteFM1/audio/` (WAV / AIFF, subfolders
allowed).

- **Native C against the ABI:** the firmware implements `fm1core_get` itself (`src/plat/ec_core.c`); its units are
  this glue's `EXTRA_SOURCES` (every `src/{plat,io,ui,dsp,seq}/*.c` but the repo's native file-API stand-in
  `src/io/ec_fs_native.c`), with `-Wall -Wextra -ffp-contract=off`. `core_elasticat.c` and `core_elasticat_desc.c` are
  empty placeholders (fm1_add_core wants a unit and a descriptor file). Natively the file API is
  `../common/rfm1_host.c` (no folder: the core plays its built-in loop); in the `.rfm1` it is the `rfm1_host_1` imports.
- **Version:** `src/plat/ec_version.h` (`EC_VERSION`).
- **Memory:** 128 MiB, most of it the sample store's static arena (`EC_SS_ARENA_MIB` 96, int16 stereo).
- **The test** (`ctest -R rfm1_elasticat`): `test_folder/audio/testloop_bpm120.wav` (4 s, two bars of a synthesized
  drum loop, written by the repo's `host/make_testloop.py`): the scan finds it, it loads, PLAY at 1.5 s starts the
  loop; the chord's RMS must be ≥ 0.02 and `ui_info` must say `file:testloop_bpm120.wav load:done play:1`.

## Building

```sh
cmake -S . -B build-rfm1 -DFM1_RFM1=ON -DFM1_RFM1_ONLY=ON -DFM1_ONLY_CORES=elasticat -DFM1_PUBLIC_BUILD=ON \
      -DFM1_EMCC=/opt/homebrew/bin/emcc -DCR_PYTHON=<RFM1Manager>/.venv/bin/python
cmake --build build-rfm1 -j8 --target elasticat_core rfm1_elasticat rfm1_run
ctest --test-dir build-rfm1 -R rfm1_elasticat -V
```

While the engine is being written, build from the working tree instead of the submodule's pin:
`-DEC_SOURCE=/path/to/elasticatfm1` (its `src/dsp` and `src/seq` join through the glob).

## The submodule

`.gitmodules` points at `https://github.com/Quixotic7/elasticatfm1` (branch `main`), the URL it will have once the repo
is pushed. Until then the checkout was cloned from the local repo; to move the pin to new local commits:

```sh
git -C cores/elasticatfm1 -c protocol.file.allow=always fetch <path>/elasticatfm1 main
git -C cores/elasticatfm1 checkout FETCH_HEAD && git add cores/elasticatfm1
```

A fresh clone of this branch needs the GitHub repo pushed first (`git submodule update --init cores/elasticatfm1`).

No `conformance_elasticat.txt` yet: the native plugin build's conformance tests have not been run for this core.
