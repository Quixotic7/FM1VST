# .rfm1 files: your firmware for host apps

An `.rfm1` file is an FM-1 firmware built to run inside an app (RemoteFM1 for iOS first) instead of on the FM-1. It is
the same build FM1VST makes for the plugin, your firmware's sources and FM1VST's glue for it, compiled to
WebAssembly. The app runs it in an interpreter, so one file works on every platform, and users can add it themselves:
download it from your release page and put it in the app's firmware folder.

The format is in [RFM1-FORMAT.md](RFM1-FORMAT.md). In short: a Wasm module with a JSON manifest (id, name, version,
licence, the source URL with its commit, authors), your credits, the licence texts, and a fixed set of exports.

## What you need

- Your firmware as a core in FM1VST: a glue folder in `core-glue/` with its `fm1_add_core` call (see
  `core-glue/common/README.md`, "Adding a core"). If FM1VST already builds your firmware, this is done.
- [Emscripten](https://emscripten.org) (`emcc`, and its `emnm` and `wasm-opt`; tested with 6.0.13), CMake 3.22 or
  newer, and the Python 3 your generate step uses (Pillow and fontTools for the Felucca-family generators).

## Build one

1. In your core's glue file, after its `fm1_add_core`, one line:

   ```cmake
   fm1_add_rfm1(<id> AUTHORS "Your Name")
   ```

   Without `-DFM1_RFM1=ON` this line does nothing, so it can stay in the glue for every build.

2. Configure a build of just your core, with the .rfm1 on (no plugin, no JUCE), and build it:

   ```sh
   cmake -S . -B build-rfm1 -DFM1_RFM1_ONLY=ON -DFM1_PUBLIC_BUILD=ON -DFM1_ONLY_CORES=<id>
   cmake --build build-rfm1 --target rfm1_<id>
   ```

   The file is `build-rfm1/rfm1/<id>-<version>.rfm1`. (`FM1_ONLY_CORES` takes a glue folder name or a
   `core_<name>.cmake` file's name, so only your firmware's submodule has to be checked out. `rfm1_all` builds every
   core whose glue has the line.)

`fm1_add_rfm1` builds the same units as the plugin's core, with the same defines, includes and generate step, plus
`core-glue/common/rfm1_shim.c` (the exports). It takes the manifest's id, name, version, licence and source URL from
your descriptor, so they always match. Its options (cmake/FM1Rfm1.cmake has them all):

| Option | What |
| --- | --- |
| `AUTHORS <names..>` | the manifest's authors |
| `MEMORY_MIB <n>` | the module's fixed memory (default 8, at most 32): raise it if the link says the data does not fit |
| `STACK_KIB <n>` | the C stack inside that memory (default 1024) |
| `LICENSE_FILES <name=path..>` | the licence texts to carry. Default: your `LICENSE` (or `COPYING`), every file in `LICENSES/`, and `LICENSING.md` |
| `SOURCE_URL <url>` | instead of the descriptor's `source_url` |
| `DEFINES`, `FLAGS`, `INCLUDES` | more, for the Wasm build only |
| `RUN_ARGS <args..>` | options for the test's `rfm1_run` (`--sysex bank.syx`, `--settle 5000`) |

To try a core without editing its glue: `-DFM1_RFM1_EXTRA_CORES=<id>` (or `all`).

### What does not work in Wasm

The module runs on one thread, has no file system, and may only use the WebAssembly that wasm3 0.5.0 runs (no SIMD,
threads, reference types, tail calls or exceptions; the build sets the feature flags). So:

- A glue on the coroutine (`core-glue/common/coro.h`: a firmware main loop that never returns, as Dinghy's, Melodee's,
  Tonitor's and LOOM-8's fm1base glue run it) builds too, with nothing to add to the glue. Natively the coroutine is
  `coro.c` (`ucontext`); in an .rfm1 it is `core-glue/common/coro_asyncify.c`, the same API on Binaryen's Asyncify,
  driven from inside the module (no JavaScript, no import). The link (`tools/rfm1/rfm1_link.cmake`) tells the two
  kinds of core apart by their objects (`emnm`: does a unit call `fm1coro_*`?). A core that does not links exactly as
  before. A core that does gets `coro_asyncify.c` and a names section, then Emscripten's `wasm-opt` with
  `--asyncify` (only the functions that can reach the yield are instrumented; the resumer, `fm1coro_rfm1_switch`, is
  in the removelist), the build's features, the Asyncify exports removed, `-O3` and the names stripped. A warning
  from `wasm-opt` (a removelist name that is not there) or anything named `asyncify` left in the module fails the
  build. The coroutine's C stack (512 KiB) and its Asyncify buffer (64 KiB) are allocated inside the fixed memory.
  (S1Vave, and OberVave, Frodhes and GR!MM tried this way: 6.5-7x real time in `rfm1_run` on an M5 Mac. S1Vave's
  .rfm1 plays the same samples as its native core, bit for bit, over `rfm1_run`'s whole run.)
- A native-only header your firmware's host build includes can get a stand-in in `core-glue/common/wasm_compat/`
  (on the include path of .rfm1 builds only). ChoralRoot's `<os/lock.h>` is there.
- `FM1CORE_RFM1` is defined (to 1) in .rfm1 builds, and `__EMSCRIPTEN__` as always with emcc, if your glue needs to
  tell.

## Check it

```sh
python3 tools/rfm1_check.py build-rfm1/rfm1/<id>-<version>.rfm1     # the file against the format
build-rfm1/rfm1_run build-rfm1/rfm1/<id>-<version>.rfm1             # boot it, hold a chord, report
ctest --test-dir build-rfm1 -R rfm1_                                # both, for every .rfm1 built
```

`rfm1_check` (Python 3, standard library only) reads the whole module: the manifest's fields, the imports (only the
format's WASI list), the exports and their signatures, the memory (fixed, at most 32 MiB), and every instruction (it
finds SIMD, atomics and the rest). The build runs it on every file it makes.

`rfm1_run` runs the file in wasm3 as an app would: a fresh flash, `init`, then the device clock for 4 s, then D4 F#4 A4
(velocity 100) held for 5 s. It prints the chord's RMS, the LCD writes and how many times faster than real time it ran,
and fails if the module trapped, halted, stayed silent or never drew. `--wav out.wav` saves what it played; `--help`
has the rest. Interpreted, a core runs 10-25 times slower than native (FM1VST's own cores: 8-50x real time on an M5
Mac with wasm3).

## Attach it to your releases (GitHub Actions)

A workflow in your firmware's repository that builds the .rfm1 from the released commit and attaches it to the
release. It checks out FM1VST, puts your repository where FM1VST's glue expects it (`cores/<Name>`, the submodule
path), and builds only your core. Replace `Felucca` / `felucca` with your folder and id, and pin FM1VST to a release
that has `fm1_add_rfm1` and your glue.

```yaml
name: rfm1
on:
  release:
    types: [published]
permissions:
  contents: write
jobs:
  rfm1:
    runs-on: macos-15
    steps:
      - name: FM1VST (the glue and the tools)
        uses: actions/checkout@v4
        with:
          repository: Quixotic7/FM1VST
          ref: v0.7.0                       # an FM1VST release with fm1_add_rfm1 and your glue
          path: fm1vst
      - name: This firmware at the release's commit, where FM1VST's glue expects it
        uses: actions/checkout@v4
        with:
          path: fm1vst/cores/Felucca
          submodules: recursive
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: 6.0.13
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - run: python -m pip install Pillow fonttools
      - name: Build and check the .rfm1
        working-directory: fm1vst
        run: |
          cmake -S . -B build -DFM1_RFM1_ONLY=ON -DFM1_PUBLIC_BUILD=ON -DFM1_ONLY_CORES=felucca \
                -DCR_PYTHON="$(which python)"
          cmake --build build --target rfm1_felucca rfm1_run
          ctest --test-dir build -R '^rfm1_felucca$' --output-on-failure
      - name: Attach it to the release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release upload "${{ github.event.release.tag_name }}" fm1vst/build/rfm1/*.rfm1 --clobber \
            --repo "${{ github.repository }}"
```

The version in the file name and the manifest is the one your glue reads (`VERSION_FILE` / `VERSION_REGEX` in
`fm1_add_core`). A glue that gives a fixed `VERSION` gives the same version for every release: move it to
`VERSION_FILE`, or update FM1VST's glue with your release. This template has not run on GitHub yet; the commands in it
are the ones tested locally (macOS, emcc 6.0.13).

## Licences

An .rfm1 is a binary of your firmware, so your licence's rules for binaries apply to it.

- **GPL firmware** (most FM-1 firmwares, and FM1VST's glue, which is GPL-3.0-only): the .rfm1 must come with its
  source. The manifest's `source_url` is your repository at the exact commit the file was built from, and `built_by`
  names the FM1VST version and commit whose glue it contains: link both from the release that carries the file, and
  keep those commits public. The app shows `source_url`, so a user can get the source.
- The file carries the licence texts: your `LICENSE`, `LICENSES/` and `LICENSING.md` (or `LICENSE_FILES`), FM1VST's
  licence (the glue), and the notices of the Emscripten runtime compiled into it (MIT: Emscripten, musl libc). Your
  descriptor's credits (`fm1_add_core CREDITS` / `CREDITS_FILES`) go into the `rfm1.credits` section, which the app
  shows in its About screen.
- A core FM1VST marks `GATED` (rights not settled: CORES-PLAN.md 6) is never built as an .rfm1.
