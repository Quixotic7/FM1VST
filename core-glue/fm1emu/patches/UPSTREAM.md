# fm1-emu patches

`web/engines/emu/build.sh` builds fm1-emu from a clean copy of the commit the submodule pins
(`web/vendor/fm1-emu`, upstream joakimeriksson/fm1-emu) with these patches applied in order. The
submodule stays at upstream's commit. When upstream merges a fix, bump the pin and delete its patch.

| Patch | Fixes | Upstream |
| --- | --- | --- |
| `0001-fm1-soc-model-the-P33-power-domain-bridge-and-its-re.patch` | Melodee 1.4.4 never draws | PR not opened yet (text below) |
| `0002-fm1-core-the-wasm-JIT-compiles-the-FPU-float-if-bloc.patch` | Melodee 1.4.4 can't keep up at 360 MHz in the browser | PR not opened yet (text below) |
| `0003-fm1-soc-the-TIMER4-count-without-a-u128-division.patch` | 1.5 % of a busy firmware's time in one division | PR not opened yet (text below) |
| `0004-fm1-web-run-for-a-span-of-emulated-time-fm1_run_fram.patch` | X0X 1.1.0 sounds garbled in Chrome (bursty rendering) | PR not opened yet (text below) |

0002-0004 are one performance pass (2026-10-18), measured in headless Chromium with a real
AudioWorklet (`web/engines/emu/bench-browser.mjs`) and in Node, profiled with V8's profiler and
the JIT's own counts. The cycles went to: compiled code ~40-50 %; the dispatcher (`run_fast`
between compiled regions) 14-25 %; helper calls out of compiled code, the FPU's above all; and
`step` / `exec_syntax` for `call R`. Idle time is not where they went: the web build already has
`skip_idle` on (`idle` and TIMER4 polls skip to the next event), which takes 2-2.5 % of the cycles,
and the firmwares' main loops never wait. A probe that looked for main-loop passes that change
nothing (same registers, every write storing what memory held, no peripheral read) found such
passes in ChoralRoot 0.25.1 only, 0.3 % of its cycles (its idle loop renders GRAIN slices and
keeps counters; Felucca reads TIMER4 every pass; X0X and Melodee change memory every pass), so
no idle patch is proposed: an exact one has nothing left to skip.

## 0001: the P33 power-domain bridge

### Problem

Melodee 1.4.4 (the Hub's package, sha256 `d349a44d…`) never drew a frame in fm1-emu. The screen
sequence stayed 0, there was no audio, and the emulator didn't halt, at 360 and at 640 MHz.

- cpu0 spins on `goto .` at `0x01C4A0D0`, called through a register from `fm1_cstart` (XIP
  `0x02039F40`, rets `0x02039F42`). That RAM function is Melodee's `fm1_core1_failure()`
  (`firmware/hal/fm1_sys.h`): `cli`, `fm1_core1_stop()`, `for (;;)`. cpu1 was never released.
- `fm1_cstart` (`firmware/src/main.c`) calls it because `fm1_power_init()`
  (`firmware/hal/fm1_power.h`) returned 0. That function replays stock V15's supply setup over P33
  and then reads `P3_ANA_CON0/5/6/9` back to check each field. This package is a no-fallback
  build, where a failed check halts the boot. With the fallback, it would run on one core.
- P33 (`JL_P33` at `0x13E00`: `PMU_CON`, `RTC_CON`, `SPI_CON`, `SPI_DAT`, SDK `WL82.h`) was
  unknown MMIO in fm1-emu, so every read returned 0. The check failed before `lcd_init`.

Other firmwares only write P33: the watchdog, the reboot and the stock V15 power setup. None of
them checks what it reads back, so the missing block went unnoticed.

### Fix

`crates/fm1-soc/src/p33.rs` models the bridge (SDK `p33.h` `p33_cs_h`/`p33_cs_l`, and the FM-1
firmwares' `fm1__p33_xfer`):

- `SPI_CON` bit 0 is the chip select, and bit 8 picks the RTC bank (bit-15 addresses).
- Writing `SPI_CON` with bit 4 set sends the byte in `SPI_DAT`. Transfers finish at once: the busy
  bit 1 reads 0, as before.
- A frame is three bytes: `op << 5 | addr[9:8]` (bit 7 marks a read; op 0 write, 1 or, 2 and,
  3 xor), then `addr[7:0]`, then the data or, for a read, a dummy. A read's value then shows in
  `SPI_DAT`.
- The registers are 2 × 1 KiB of storage that resets to 0, so a firmware that only writes sees no
  change. Two strobes don't read back: `P3_WDT_CON` bit 6 (the watchdog clear) and `P3_PR_PWR`
  bit 4 (the soft reset).
- `FM1_P33_TRACE` prints each frame. Stock V15's frames decode to sensible P3 registers (WDT_CON,
  PMU_CON0..2, LP_TMR, ANA_CON4/5), which supports this frame format.
- The patch adds a unit test and a row in `docs/stock-firmware.md`.

### Pull request text (ready to paste)

Title: `fm1-soc: model the P33 power-domain bridge (Melodee 1.4 boots)`

Body:

> JL_P33 (0x13E00: PMU_CON, RTC_CON, SPI_CON, SPI_DAT) was unknown MMIO, so every P33 read
> returned 0. Melodee 1.4 replays the stock V15 supply setup over P33 and checks each field it
> wrote (P3_ANA_CON0/5/6/9). The check failed, so the firmware never started CPU1 and, in the
> Hub's 1.4.4 build, halted in `fm1_core1_failure()` before the LCD was set up: no screen, no
> sound, no halt reported.
>
> This adds `crates/fm1-soc/src/p33.rs`. The bridge shifts three-byte frames (op and address
> bits 9:8, address bits 7:0, data) into a register file with a P33 bank and an RTC bank
> (SPI_CON bit 8). Write, or, and and xor update a register, and a read returns it in SPI_DAT.
> Transfers finish at once (the busy bit reads 0, as before). Registers reset to 0, so firmwares
> that only write P33 run as before. The watchdog-clear and soft-reset strobes don't read back.
> `FM1_P33_TRACE=1` prints the frames.
>
> Checked: Melodee 1.4.4 boots, draws, plays a chord on both cores (cpu1 completes the audio
> jobs) and saves to flash. The browser build passes its test for Felucca 1.6.1, X0X 1.1.0,
> SLOOP 2.7.1, sloopDX 4.0, ORBIT 0.7.1, ChoralRoot 0.25.1, FM-106 Beta4 and stock V15, with
> unchanged real-time factors. Stock V15's own P33 traffic decodes to the expected P3 registers.
> `cargo test -p fm1-soc` passes and includes a new test of the frame format.

### Reproduce

```
# native: before the fix cpu0 ends on `goto .` at 0x01C4A0D0 and cpu1 is never released
cargo build --release -p fm1-emu
fm1-emu --fwsc melodee-1.4.4.fwsc --ms 3000 --snap 2900   # work/lcd_2900.bmp: the home screen
FM1_P33_TRACE=1 fm1-emu --fwsc melodee-1.4.4.fwsc --ms 300 | grep ^p33   # the supply setup's frames

# RemoteFM1's browser build, in Node
web/engines/emu/build.sh && node web/engines/emu/test.mjs --mhz=360 --only=melodee
EMU_PATCHES=0 web/engines/emu/build.sh web/build/emu-base   # the unpatched build, for comparison
node web/engines/emu/test.mjs --mhz=360 --only=melodee --wasm=web/build/emu-base/fm1_web.wasm
```

### Not modelled (still open)

- `C1_CON` (`0x1EEE004`) bit 1. Melodee and the SDK's `EnableOtherCpu` hold CPU1 with `|= 2` and
  release it with `|= 8; &= ~2`. fm1-emu models only the stock IPC's pause (`|= 4`) and resume
  (`|= 8`). Melodee holds CPU1 only on a fault, a retirement or a reboot, so a normal session is
  unaffected.
- The registers have no analog effects: no watchdog reset, no P33 soft reset, and the reset
  source reads 0 (no power-on bit).

## 0002: the wasm JIT compiles the FPU, float if-blocks and calls through a register

### Pull request text (ready to paste)

Title: `fm1-core: the wasm JIT compiles the FPU, float if-blocks and calls through a register`

Body:

> Profiling the browser build (V8's profiler and the JIT's helper counts) showed three kinds of
> instruction keeping busy firmwares out of compiled code:
>
> - Every FPU op (`3f e5`) was a helper call: ~18 M a second in Melodee 1.4.4 with a chord held.
> - An `iff (R op R) {` block with a float compare was a helper call, and an if-else one was not
>   compiled at all (its then-part ran interpreted).
> - `call R` and `goto R` had no micro-op, so each went through `step` and `exec_syntax` and ended
>   the compiled run (Melodee's UI calls through function pointers, ~2 M a second).
>
> The FPU ops now compile to the wasm float instructions `fpu_d` itself compiles to (f32
> add/sub/mul/div; for ops 7 and 8 a multiply then an add or subtract, not fused; the saturating
> truncations; the conversions), so the results are the same bits. fmin and fmax (Rust's NaN rules)
> and unknown unary codes stay with the helper. If-blocks of families 8 and up use `cmp`'s float
> compares, as `cmp_cond` does, and an if-else block's then-part now comes along for any family.
> `call R` / `goto R` become micro-ops (`CallR` / `GotoR`, the same effect as exec_syntax's arms):
> they end a block, and compiled code goes on through the region's jump table as a return does.
> The AArch64 JIT runs them through its helper.
>
> Melodee 1.4.4 in Node at 360 MHz: 0.97 → 0.69 of real time (in Chromium's AudioWorklet 0.99 →
> 0.71, from 1470 audio glitches in 10 s to none). X0X, Felucca and ChoralRoot: unchanged to a few
> percent faster. Screens and audio are identical to before for Felucca 1.6.1, X0X 1.1.0,
> SLOOP 2.7.1, sloopDX 4.0, ORBIT 0.7.1, ChoralRoot 0.25.1 and stock V15 (8 s with PLAY, a chord
> and an encoder turn). Melodee is identical for 10 s of PLAY and a chord; after an encoder turn
> and a release its last second differs: cpu1 runs after each 1024-instruction slice of cpu0, and
> the slices now end at other block boundaries. (With this change Melodee's compiled and
> interpreted runs agree with each other, which they did not before.) LED levels of V15 and
> Melodee differ slightly for the same reason. `cargo test -p fm1-core` passes, with a new test of
> `call R`.

## 0003: the TIMER4 count without a u128 division

### Pull request text (ready to paste)

Title: `fm1-soc: the TIMER4 count without a u128 division`

Body:

> Every TIMER4 CNT read (0x10804, the firmwares' microsecond clock, polled in their main loops)
> computed `cycles * 24 MHz / hz` in u128: in wasm a compiler-rt call, 1.5 % of Melodee's time in
> the browser. Split into whole seconds and the rest it stays in u64 and gives the same value. A
> new test checks it against the u128 form at 100-640 MHz and up to 10^12 cycles.

## 0004: run for a span of emulated time

### Pull request text (ready to paste)

Title: `fm1-web: run for a span of emulated time (fm1_run_frames, fm1_time_frames)`

Body:

> `fm1_render` runs the machine until the I2S DMA has produced the frames asked for. With
> 512-frame DMA halves (X0X 1.1.0) a host's 128-frame quanta then cost nothing three times and a
> whole half's emulation, 10 ms of a 2.7 ms quantum, the fourth time. Chrome's 256-frame audio
> callbacks miss their deadline every 11.6 ms: 156 glitches in 10 s at 360 MHz, heard as garbled,
> distorted audio, though the machine keeps up on average (load 0.68).
>
> `fm1_run_frames(frames, max_insns)` runs a span of emulated time and queues the audio;
> `fm1_render(n, 0)` hands it out without running; `fm1_time_frames()` reports the emulated time.
> A host can then pace the machine by time: a quantum's span per quantum, with a few tens of ms
> buffered ahead, so the work per quantum is even. RemoteFM1's engine buffers 40 ms: X0X has no
> glitches left at 240-360 MHz, and Melodee's LEDs stop flickering (their on-fractions are taken
> over even stretches of emulated time).
