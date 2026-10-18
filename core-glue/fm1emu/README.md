# core-glue/fm1emu: the emulated FM-1 as a core

`fm1emu.fm1core` is Joakim Eriksson's [fm1-emu](https://github.com/joakimeriksson/fm1-emu) (`cores/fm1-emu`, MIT,
pinned at 5fbb066, unmodified) behind FM1VST's core ABI (`core-api/fm1core.h`, ABI 6). Every other core is a firmware
compiled for the host. This one emulates the FM-1's SoC instruction by instruction, so it runs an FM-1 update package
(`.fwsc`) as the unit would: M-VAVE's stock firmware and the community firmwares, unchanged.

The core is a Rust `cdylib` in this folder (`Cargo.toml`, `src/`). It depends only on fm1-emu's `fm1-core` and
`fm1-soc` crates (and `fm1-isa` under them), all by path. It uses no crates.io dependency and not nih-plug.
`core.cmake` runs `cargo build --release` (with `CARGO_TARGET_DIR=<build>/fm1emu-cargo`) and copies the dylib to
`<build>/cores/fm1emu.fm1core`, next to the other cores, where CoreLoader finds them. The module exports
`fm1core_get` and `fm1core_latency_frames`.

| file | what |
|---|---|
| `src/lib.rs` | the descriptor, the entry points, the machine (fm1-emu's plugin's `Machine::boot`, `run` and `restore_flash`, reimplemented with attribution: they are private to `crates/fm1-plugin`) |
| `src/fifo.rs` | the audio stream on the device's time axis (see "Audio") |
| `src/abi.rs` | `fm1panel_t` and `fm1core_t` in Rust, with the layout asserted against the C header's at compile time |
| `src/font.rs`, `src/sum.rs` | the 5x7 font for the screen shown without firmware; CRC-32 and SHA-256 |
| `conformance_fm1emu_felucca.txt`, `conformance_fm1emu_mvave.txt` | `tests/core_conformance.cpp`'s scripts, one per package |
| `../../tests/fm1emu_test.cpp` | two instances, the image, latency, memory, the JIT off |
| `../../tests/fm1emu_bench.sh` | `core_bench` per package, fm1emu next to the native core |

## The descriptor

The descriptor has id `fm1emu`, name `FM-1 (emulated)`, version `fm1-emu 5fbb066` (the submodule's commit, passed in
by CMake), family `fm1emu`, licence `MIT`, gated NULL, and the source URL is the repo at that commit. The credits are
fm1-emu's README "Credits" plus its MIT notice. The keymap is the FM-1's factory wiring (`FM1P_KEYMAP_INIT`, the same
table as fm1-soc's private `periph.rs` KEYMAP). The panel table is Felucca's `PANEL_DEFAULT`, which is the hardware's
own. The button names are the Felucca glue's (`FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+`).
`params`, `knob_target`, `param_epoch` and `boot_options` are NULL.

On the licence: the emulator is MIT, and that is what the descriptor says. The glue in this folder is GPL-3.0-only,
like the rest of FM1VST, so the module as built is a GPL-3.0-only work containing MIT code. The firmware a user loads
carries its own licence.

## The firmware: a .fwsc through flash_stage (no ABI change)

`flash_size` is **0x210000**, the "fm1emu image":

| bytes | what |
|---|---|
| 0 .. 0x100000 | the live 1 MiB NOR (`soc.flash.mem`) |
| 0x100000 | the package slot: `FM1EMUPK`, u32 LE length, u32 LE CRC-32 of the package, the `.fwsc` bytes, then 0xFF (packages up to 0x10FFF0 bytes) |

`flash_stage(bytes, n)` and then `init` boot one of three things:

1. **A raw `.fwsc`** (one that `fwsc::unpack` accepts) boots on a fresh flash, as `Machine::boot(pkg, &[])` does.
2. **An fm1emu image** (n = 0x210000, the magic present, the CRC matching) boots the slot's package. Every 4 KiB sector
   of bytes 0..1 MiB that differs from that package's fresh flash is then restored into the NOR and the XIP window,
   as fm1-emu's plugin restores its saved sectors.
3. **Nothing, an erased image or an unrecognised one** means no firmware. If `FM1EMU_FWSC` names a readable package,
   that package boots (for tests and development only). Otherwise the core runs empty: the LCD shows `LOAD .FWSC`
   with the reason on a second line, the audio is silence, and `halted()` is 0.

SMK-37 packages (`Board::Smk37`) are refused and the core runs empty, because `fm1panel_t` is the FM-1's panel.
`options(path)` accepts NULL (case 3), `FM1CORE_FLASH_STAGED` (the staged bytes), or a file path. A file path is read
as staged bytes and never written back. `flash()` returns the image, refreshed from the NOR on each call and once
more at `shutdown`, so it stays valid after shutdown. `flash_dirty()` moves with `soc.flash.revision` and on every
boot. `flash_sync()` returns 1 unless an SPI command is in progress (chip select low): the firmware saves by itself.

The conformance test's "fresh flash" boots go through case 3 (`FM1EMU_FWSC`). Its flash round trip goes through
case 2.

## The clock

The clock is synchronous and starts no thread.

- **`tick(ms)`** first takes the panel's input:
  - key and button edges become `Action::Key` (ids 14..40 and 0..13);
  - a tap goes down now and comes up 20 ms of device time later;
  - `enc[]` detents become `Action::Enc` and are zeroed;
  - a change to `master` becomes `Action::Adc{ch: 4}`;
  - queued MIDI bytes become `Action::Midi`.

  It then runs `run_dual` / `run_blocks` until `soc.periph.cycles >= (ms + 1) * 240e6 / 1000`, at 240 MHz, with
  `skip_idle` and the JIT on as fm1-emu's plugin runs. Each call is given the cycles left, capped at 1 ms of
  instructions. An overshoot (an idle skip can jump tens of ms during a boot) is carried: the following ticks run
  nothing until the device clock is behind again. The machine never runs a fixed instruction count per tick.
- **`frame()`** copies the LCD and the LEDs out. **`idle()`** copies only the LEDs. Both read the input too.
- **`audio(out, 128)`** pops from the stream (see "Audio").

## Audio

The design specified a FIFO primed once with L frames of silence and capped at L + 2H, dropping frames above the cap.
That design failed in practice, for two reasons:

- **The machine runs ahead.** tick() may carry the machine well past its millisecond: SLOOP's first DMA burst came at
  ms 165 holding five halves that covered device time up to 207 ms. Primed at that moment, the FIFO was 42 ms early
  and ran dry 40 ms later. It never recovered, because nothing re-primes it, so SLOOP, sloopDX, No Man Sky and FM-106
  underflowed once per DMA half from then on (17..117 underflows in 10 s).
- **The cap clicks.** The stock V15's 64-frame halves put L + 2H inside the normal swing of the level, so the cap shed
  704 frames in 12 s, and each shed is a click on a held tone.

The replacement is `src/fifo.rs`, which keeps the stream on **the device's own time axis**:

- Every frame has its device frame index (`cycles / 5440`), counted in the I2S clock's frames: 24 MHz / 544 =
  **44117.647 Hz**. That is fm1-soc's `FRAME_T4` and Felucca's `DAC_TICKS`, and the reason the stream does not match
  44100.
- Before the first half arrives, tick() runs the machine in steps of 2400 instructions (10 µs) and stops when the
  half arrives. Its end is therefore known exactly, and it becomes the stream's origin. Later halves follow on
  contiguously. A burst that ends a half or more beyond where the stream ends (the DMA stopped) is placed at its own
  time, with silence in the gap (`gaps`; the stock V15 shows one 64-frame gap at boot).
- ABI frame k, counted from power-on, plays the device stream at index **(k − L) · 44117.647 / 44100**, through a
  4-point Lagrange interpolator (engine/resampler.h's). The 0.04 % difference between the two clocks is absorbed
  exactly: nothing creeps and nothing is dropped. The delay is exactly L frames however far the machine ran ahead.
- A frame is there only once its half has finished playing, which is up to H − 1 frames after its time. tick() runs
  1 ms ahead of audio(), so the stream needs **L ≥ H − 42**. A frame not there yet gives silence and is counted
  (`underflows`); one that arrives after it was due is dropped and counted (`dropped`). ui_info prints the margin
  (the frames beyond the last one read) as `margin lo..hi`.

### Latency

H was measured by the core itself: the frames of each burst divided by the DMA halves it held, the largest kept
(ui_info `H`).

| package | H (frames) |
|---|---|
| Felucca 1.6.1 | 128 |
| stock V15 | 64 |
| SLOOP 2.7.1, sloopDX 4.0, OberVave 1.5, No Man Sky 0.4, FM-106 | 256 |
| ChoralRoot 0.25.1 | none (no audio on fm1-emu, see below) |

**L = 256 frames (5.8 ms)**, the largest half. With L = 256 every package above ran 10 s or more with 0 underflows,
0 drops and a lowest margin of 63..306 frames. If a package's half is larger than L, that instance's L grows to cover
it, in whole blocks, when its first half arrives.

`fm1core_latency_frames()` is a plain C symbol, found with `dlsym` and not part of the descriptor (no ABI bump).
Before init it returns 256. After the first half it returns that instance's L. The host should call
`setLatencySamples(L * hostRate / 44100 + Device::latency_frames(hostRate))` (`Device::latency_frames` already
covers the 128-frame block and the resampler).

The end-to-end delay was measured in `fm1emu_test`. D4 at 3000 ms is first heard 576 frames after the key on fm1emu
Felucca, and 72 frames *before* the key's frame on the native felucca core (the native core renders the whole block
the key falls in). The difference is **648 frames (14.7 ms)**:

- L, 256 frames;
- the emulated hardware's own pipeline, about 390 frames: the firmware scans the key matrix and debounces, then
  renders into the DMA half after the one playing (H = 128 ahead), and fm1-soc hands a half over only once it has
  played (another H).

The real unit has the matrix scan and the double buffer too. The native cores skip both.

## LEDs

`soc.periph.led_take()` gives each key id's on-fraction since the last take. The LEDs are taken over at least 8 ms of
device time, because a shorter window flickers. Each fraction maps to `panel->led[col][row - 1]` through the keymap,
as `level = f / 0.08 * 255`, clamped.

The fractions measured with `FM1EMU_LED_TRACE=1`:

| LED | fraction | level |
|---|---|---|
| a steadily lit LED, Felucca and stock | 0.087..0.099 (one column in 11) | 255 (`FM1P_LED_LIT`) |
| the stock firmware's button backlight | 0.018..0.019 | about 60 (the DIM class) |

PLAY's green LED (column 8, row bit 1) has no key id in fm1-soc's matrix, so fm1-soc does not account for it:
`led_play_green` is always 0.

## MIDI

- **`midi_in(pkt)`** decodes a USB-MIDI event packet (byte 0 = cable << 4 | CIN, then the bytes; the plugin's
  `decodePacket` layout) and puts its 1..3 bytes onto the **MIDI IN jack** (UART1, 31250 baud, as fm1-soc models it).
  It returns 0 (no room) when more than 3000 bytes are waiting.
  Measured with a script's `midi 90 3e 64`: the note sounds within 300 ms on both Felucca 1.6.1 and the stock V15.
- **`midi_out_take`** always returns 0. fm1-soc has no MIDI or USB transmit path: the UART's TX DMA finishes at once
  into nothing, and the USB block is plain register storage.
- **`key_vel` / `key_lean`** are not modelled: the FM-1's matrix has no velocity. fm1-soc's velocity is the SMK-37's,
  and that board is refused.

## Halting and panics

`halted()` is 0x100 after `run_dual` returns an error (an instruction fm1-core cannot decode, for example) or after a
panic. Every `extern "C"` entry point catches panics. After a halt, `tick` does nothing and `audio` gives silence.
fm1-soc models no reset, so a firmware's reboot or UBOOT request reaches nothing: the conformance files declare
`reboot none`. A panic inside JIT-compiled code cannot unwind through the generated frames and would abort; none was
seen.

`ui_info` reports the mode, the package (identity, size, SHA-256 prefix, where it came from), the board, H, L, the
margin, the speed (device ms per wall ms spent in tick, over each second of device time), the device ms, underflows,
drops, gaps, and the JIT state. `stats(shed, cpu_pct)` gives `underflows + dropped` and `100 / speed`.

## Results

### Conformance

`core_conformance_fm1emu_felucca` and `core_conformance_fm1emu_mvave` **both PASS**, each with one SKIP (`reboot none`).
`tests/core_conformance.cpp` now accepts a flash image of at least 1 MiB in whole 4 KiB sectors, where it required
exactly 1 MiB.

| | Felucca 1.6.1 | stock V15 |
|---|---|---|
| screen (not blank) | at 121 ms | at 406 ms |
| D4 sounds | from 9 ms after the key, peak 6537 | from 77 ms, peak 2990 |
| flash round trip | the palette via HOME > DISPLAY > COLOR (the settings record, written by the firmware itself) | PRESETS to patch 002 EMU BASS, then SAVE ("All Data Saved": the USR area's sector 0xFB000) |
| after the reboot from the image (case 2) | the green palette | patch 002, not 001 |
| double load | passes | passes |

fm1-emu outputs 6 dB lower than the native Felucca core. fm1-soc takes the DAC's 24-bit sample (`>> 8`, Felucca's
`Q15 << 7`), which is the hardware's level. The native glue outputs Q15 at 0 dBFS.

### fm1emu_test

`fm1emu_test` **PASSES**:

- With no firmware, the LOAD .FWSC screen, silence, and the core keeps running.
- Case 1 (a package staged as bytes) boots. `flash()` carries the package in its slot, and that image boots the
  same package (case 2) with an identical NOR.
- **Two instances** (Felucca and the stock V15) in one process: both draw, and 6 s of both took 2.4 s of wall time.
  D4 on Felucca gives peak 6537 there and 0 on the stock instance. D4 on the stock instance gives peak 2990 there and
  0 on Felucca. Both kept running with the JIT on.
- Latency is as above. `fm1core_latency_frames` via dlsym returns 256.
- With the JIT off, Felucca runs on the interpreter.

### Speed (the real-time factor)

`tests/fm1emu_bench.sh` runs `core_bench`: 10 s of a held D4 F#4 A4 chord through `Device::render` at 48 kHz. It runs
on this Mac (Apple Silicon), one process at a time, against the native cores built in the same tree.

| package | native core (x real) | fm1emu, JIT on (x real) | its ui_info speed | fm1emu, JIT off (x real) | chord sounded on fm1emu |
|---|---|---|---|---|---|
| Felucca 1.6.1 | felucca 474.6 | **4.8** | 4.8x | 1.3 | yes |
| stock V15 | (none) | **4.4** | 4.5x | 2.8 | yes |
| SLOOP 2.7.1 | sloop 613.1 | **4.3** | 4.3x | 1.3 | yes |
| sloopDX 4.0 | sloopdx 654.9 | **4.9** | 4.9x | 1.3 | yes |
| OberVave 1.5 | obervave 178.9 | **3.4** | 3.4x | 1.2 | yes |
| No Man Sky 0.4 | nomansky 71.7 | **4.7** | 5.5x | 1.4 | yes |
| FM-106 Beta4 | fm106 141.5 | **2.3** | 2.3x | 1.1 | yes |
| ChoralRoot 0.25.1 | choralroot 310.6 | 122.6 (idle) | 128.7x | 100.5 | **no** (fm1-emu hangs at its splash) |

Every fm1emu row ran with 0 underflows and 0 drops. The stock V15 shows one 64-frame gap at boot.

`ui_info speed` is the core's own figure: device ms per wall ms inside tick, over the last second. The emulated FM-1
runs at **2.3x..4.9x real time** with the JIT on, so one instance takes 20..45 % of one CPU core, where a native core
takes 0.2..1.4 %. Two instances ran at full speed side by side (fm1emu_test).

### Firmwares that do not run on fm1-emu 5fbb066

These are limits of fm1-emu at this commit, not of the glue. fm1-emu's own CLI does the same.

- **ChoralRoot 0.25.1** stops at its splash. cpu0 spins at 0x02024E6A with no interrupts and no DMA, so there is no
  audio (the bench row is silent). fm1-emu's site lists ChoralRoot 0.22-beta1, which predates this boot path. The
  same hang shows in `fm1-emu --fwsc choralroot-0.25.1.fwsc --ms 3000`.
- **OberVave 1.5** runs and sounds, but releasing a key that was held for 2 s halts it at 5032 ms:
  `Decode(UnknownInstruction) at pc 0x020016ca`, an instruction fm1-core does not decode. It halts the same way with
  the JIT off and in `fm1-emu --fwsc obervave-1.5.fwsc --keys 3000-5000:n9`. The bench holds its chord through the
  whole measurement and releases it only after, so that row is valid. fm1-emu's site lists OberVave 1.3.1.

## Memory

Measured with `fm1emu_test ... --memory` in a fresh process (phys_footprint; each instance is Felucca, booted and run
for 2 s):

| | phys_footprint | per instance |
|---|---|---|
| before any instance | 3.8 MB | |
| 1 instance | 56.2 MB | +52.4 MB |
| 2 instances | 108.8 MB | +52.7 MB |
| 4 instances | 213.0 MB | +52.1 MB each |
| all 4 unloaded | 213.0 MB | 0 returned |
| 4 loaded again | 231.3 MB | +18.3 MB in all |

With the JIT off (`FM1EMU_JIT=0`) each instance costs 51.2 MB, so the compiled code is about 1 MB of it.

**About 52 MB per instance.** Most of it is fm1-core's decoded-instruction caches, which are allocated whole for each
of the two cpus (`icache` / `icache_x`, 256K lines each, plus `bmap`). Most of the rest is the SoC's RAM banks (1 MiB
+ 512 KiB + 256 KiB), the NOR, the XIP image and the frame buffer.

**Unloading does not give the memory back to the OS, but the process reuses it:**

- `shutdown()` drops the machine. malloc's bytes in use fall from 52.4 MB to 3.4 MB (measured with
  `malloc_zone_statistics`). The allocator keeps the pages, so phys_footprint does not fall, and the next instances
  reuse them: 4 more cost only 18 MB.
- What stays per unloaded instance:
  - the 2.06 MB image buffer, because `flash()` must stay valid for the module's lifetime;
  - the module itself: dyld keeps a Rust dylib mapped after `dlclose`, because of its thread-local variables. All 4
    copies were still mapped after unloading.
  - the JIT's code pages. fm1-core's `CodeCache` maps 64 MiB of MAP_JIT address space per cpu, touches about 1 MB,
    and has no `Drop`, so it is never unmapped.

That comes to about 4..6 MB kept per load/unload cycle. Freeing the JIT pages would need a `Drop` in fm1-core (the
submodule stays unmodified) or an `mmap` interposer in the glue. The interposer was judged not worth it for about
1 MB.

## JIT

- **It works in a dlopen'd copy, and twice in one process.** Each copy has its own `Cpu`s and its own code caches.
  The two-instance test ran Felucca and the stock V15 side by side with the JIT on, each at its single-instance speed.
- **When MAP_JIT is refused** (a hardened-runtime host without `com.apple.security.cs.allow-jit`): fm1-core's
  `CodeCache::new` returns None and the cpu keeps `jit = None`. But `run_fast` calls `Jit::new` again at every block
  while `jit` is None, which means a failing 64 MiB mmap per block. The glue therefore probes MAP_JIT itself before
  booting (a 16 KiB map and unmap) and boots with the JIT off if the probe fails. ui_info then says
  `JIT unavailable (MAP_JIT refused): interpreter`. `FM1EMU_JIT=0` forces the same path.
- **The interpreter's speed** is the JIT-off column of the speed table: 1.1x..1.4x real time for the community
  firmwares, 2.8x for the stock V15. One instance then takes 70..90 % of a core, and FM-106 has 10 % headroom left.
  It works, but a host without allow-jit is not a practical home for this core.

## What the host's "Load .fwsc…" entry must do

1. Read the file and call `Device::boot_from(bytes, n)` on an fm1emu core (a fresh load, or a reload). This stages the
   package and powers on (case 1).
2. Persistence then needs nothing new. The image `flash()` returns carries the package in its slot, so plugin state,
   presets and `flash.bin` store the package with the NOR. `boot_from(image)` restores both (case 2).
3. After boot, `dlsym(handle, "fm1core_latency_frames")` gives L. Report
   `L * hostRate / 44100 + Device::latency_frames(hostRate)` with `setLatencySamples`. CoreLoader's handle is private;
   the test reopens the copy with `RTLD_NOLOAD`. A `LoadedCore::symbol(name)` accessor would be the clean way.
4. Not done here: the core is not in `FM1_CORE_IDS`. The bundles, `core_smoke`, `param_test` and `core_bench_run`
   loop over that list and expect a core that sounds without a package. Bundling it means listing `fm1emu_core` with
   the others (its module is `<build>/cores/fm1emu.fm1core`) and giving `core_smoke` a package.

## Licences

| part | licence |
|---|---|
| fm1-emu (`cores/fm1-emu`) | MIT, (c) 2026 Joakim Eriksson. Its `crates/fm1-core/src/jit_a64.rs` is vendored from esp32sim, MIT, (c) 2026 Joakim Eriksson and Alice. |
| crates linked (`cargo tree`) | `fm1-core`, `fm1-isa`, `fm1-soc`, all fm1-emu's own (workspace licence MIT); no crates.io dependency |
| not used | nih-plug and the plugin, editor, web, live and gdbstub crates |
| the glue (this folder) | GPL-3.0-only |
| the descriptor's credits | fm1-emu's MIT notice and its credits line |

## Running it

```sh
cmake --build build-fm1emu/cmake --target fm1emu_core core_conformance fm1emu_test core_bench
ctest --test-dir build-fm1emu/cmake -R fm1emu            # the two conformance tests and fm1emu_test
build-fm1emu/cmake/fm1emu_test MODULE FELUCCA.fwsc FM-1.fwsc felucca.fm1core OUT --memory
tests/fm1emu_bench.sh [BUILD_DIR] [SECONDS]               # FM1EMU_JIT=0: the interpreter
```

The packages come from CMake cache variables, `FM1EMU_FELUCCA_FWSC` and `FM1EMU_MVAVE_FWSC`. They are machine-local
files, not in the repo.

Environment variables:

| variable | effect |
|---|---|
| `FM1EMU_FWSC` | case 3's package |
| `FM1EMU_JIT=0` | the interpreter |
| `FM1EMU_LED_TRACE=1` | prints the LEDs' on-fractions |
