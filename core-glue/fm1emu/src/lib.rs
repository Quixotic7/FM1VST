// SPDX-License-Identifier: GPL-3.0-only
//! The fm1emu core: Joakim Eriksson's fm1-emu (cores/fm1-emu, MIT; its crates fm1-core and fm1-soc, built from a copy
//! of the pinned commit with core-glue/fm1emu/patches applied: core.cmake)
//! as one more FM1VST core module (core-api/fm1core.h ABI 6). Where the other cores are a firmware compiled for
//! the host, this one is the FM-1's SoC emulated instruction by instruction, running any FM-1 update package
//! (.fwsc): M-VAVE's stock firmware and every community firmware, unchanged. README.md (next to this file) has
//! the design, the measurements and the host calls.
//!
//! One loaded copy is one emulator: the state is this module's globals (the host dlopens a private copy of the
//! module per instance, engine/cores.h). Every entry point runs on the host's one clock thread (fm1core.h
//! THREADING CONTRACT) and catches a panic: a panic halts the core (halted() = 0x100), it never unwinds into C.
//!
//! The parts of fm1-emu's own plugin this needs (crates/fm1-plugin/src/engine.rs: Machine::boot, run,
//! restore_flash; MIT, (c) 2026 Joakim Eriksson) are not a library: they are reimplemented here, following it.

mod abi;
mod fifo;
mod font;
mod sum;

use abi::*;
use fm1_core::{Bus, Cpu};
use fm1_soc::{machine, Action, Board, Soc};
use std::collections::VecDeque;
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::time::{Duration, Instant};

// ------------------------------------------------------------------------------------------------ constants
const RAM_BASE: u32 = 0x01c0_0000;
const RAM_SIZE: usize = 1 << 20;
/// the emulated chip's clock by default: 360 MHz (the real FM-1 runs at about 320..360; fm1-emu's own plugin uses 240).
/// The host picks another before init with fm1core_boot_option("mhz", ..): it sets fm1-soc's `periph.hz` (the
/// instructions per emulated second, which scales every timer the firmware sees) and tick()'s cycles per millisecond.
pub const DEFAULT_MHZ: u32 = 360;
/// the clocks fm1core_boot_option accepts (MHz)
const MHZ_MIN: u32 = 60;
const MHZ_MAX: u32 = 1000;
/// the NOR (1 MiB), its sectors
const NOR: usize = fm1_soc::FLASH_SIZE;
const SECTOR: usize = 4096;
/// the flash image (fm1core.h flash_size): the NOR, then the package slot
const SLOT: usize = 0x10_0000;
const IMAGE: usize = 0x21_0000;
const MAGIC: &[u8; 8] = b"FM1EMUPK";
const SLOT_HEAD: usize = 16;
const PKG_MAX: usize = IMAGE - SLOT - SLOT_HEAD;
/// L, the core's added delay in 44.1 kHz frames (fifo.rs, README "Latency"): ABI frame k plays the device's
/// stream at frame k - L. It must cover a DMA half (L >= H - 42); the largest half measured is 256 frames (SLOOP,
/// sloopDX, OberVave, No Man Sky, FM-106; Felucca 128, the stock V15 64). A package with a larger half raises its
/// instance's L to cover it, in whole blocks (ui_info and fm1core_latency_frames say so).
pub const LATENCY: u32 = 256;
/// a tap (keys_tap / buttons_tap): down now, up this much device time later
const TAP_MS: u64 = 20;
/// the LEDs' on-fractions are taken over at least this much device time (the matrix scans its 11 columns in
/// ~1.1 ms on Felucca, slower on the stock firmware), and a level is the mean of the last LED_AVG takes: 100 ms, as
/// RemoteFM1's site shows them (web/engines/emu/engine.js: 30 Hz samples, the last three). A shorter window flickers:
/// Melodee's multiplexed LEDs flipped ~27 times a second at 8 ms
const LED_WINDOW_MS: u64 = 33;
const LED_AVG: usize = 3;
/// the on-fraction of a steadily lit LED: the LED lines drive one matrix column at a time, so a lit LED is on
/// about 1 scan step in 11 (fm1-live's panel.rs `bright`: full at 0.08). Calibrated on Felucca 1.6.1 and the
/// stock V15 (README "LEDs"): FM1P_LED_LIT at this fraction and above
const LED_LIT_FRACTION: f32 = 0.08;
/// MIDI bytes waiting behind the jack before midi_in says "no room" (fm1-soc's queue holds 3125)
const MIDI_ROOM: usize = 3000;

// ------------------------------------------------------------------------------------------------ the machine
/// fm1-emu's plugin's Machine (engine.rs), minus its thread: the cores, the SoC
struct Machine {
    cpu: Cpu,
    cpu1: Option<Cpu>,
    soc: Soc,
    pending_tick: bool,
}

impl Machine {
    /// engine.rs Machine::boot: a package's stock image on a fresh SoC, the SPL's boot parameters in r0
    fn boot(st: fm1_soc::Stock, jit: bool, hz: u64) -> Machine {
        let mut soc = Soc::new(RAM_SIZE, st.xip);
        soc.periph.hz = hz;
        soc.periph.skip_idle = true;
        let mut cpu = Cpu::new(st.entry);
        cpu.jit_on = jit;
        cpu.user = true; // the SPL hands cpu0 over in user mode (fm1-core's Cpu::user)
        let params = RAM_BASE + 0x7fc00;
        soc.write32(params, params + 0x40);
        cpu.regs[0] = fm1_soc::stock_boot(&mut soc, &st.flash, params, st.board);
        Machine { cpu, cpu1: None, soc, pending_tick: false }
    }

    /// run until the SoC's clock reaches `target` cycles (cpu0's blocks, cpu1 beside once released: engine.rs
    /// run). run_dual counts instructions; an instruction is at least a cycle (idle skips add more), so each
    /// call is given what is left, capped at `step`: the clock passes the target by at most one call's skips
    /// `step`: the most instructions a call; `until_audio`: return as soon as a DMA half has come (the first
    /// half's end is found to within a step: fifo.rs)
    fn run_to(&mut self, target: u64, step: u64, until_audio: bool) -> Result<(), String> {
        let mut stuck = 0;
        while self.soc.periph.cycles < target && !(until_audio && !self.soc.periph.audio.is_empty()) {
            let c0 = self.soc.periph.cycles;
            let n = (target - c0).clamp(1, step) as u32;
            let jit = self.cpu.jit_on;
            let Machine { cpu, cpu1, soc, pending_tick } = self;
            machine::run_dual(cpu1, soc, n, RAM_SIZE, jit, |soc, k| machine::run_blocks(cpu, soc, k, pending_tick))?;
            if self.soc.periph.cycles == c0 {
                stuck += 1;
                if stuck > 64 {
                    return Err(format!("the clock stopped at cycle {c0} (pc {:#010x})", self.cpu.pc));
                }
            }
        }
        Ok(())
    }

    /// engine.rs restore_flash for one sector: the NOR and, where the XIP window shows the chip, the window
    fn restore_sector(&mut self, at: usize, data: &[u8]) {
        let soc = &mut self.soc;
        soc.flash.mem[at..at + data.len()].copy_from_slice(data);
        let from = at.max(fm1_soc::XIP_FLASH_OFFSET);
        let end = (at + data.len()).min(fm1_soc::XIP_FLASH_OFFSET + soc.xip.len());
        if from < end {
            soc.xip[from - fm1_soc::XIP_FLASH_OFFSET..end - fm1_soc::XIP_FLASH_OFFSET]
                .copy_from_slice(&data[from - at..end - at]);
        }
    }
}

// ------------------------------------------------------------------------------------------------ the state
#[derive(Clone, PartialEq)]
enum Mode {
    /// before init, or after shutdown
    Off,
    /// no firmware: the LOAD .FWSC screen, silence (the reason on the screen's second line and in ui_info)
    Empty(String),
    Running,
    /// the machine stopped (an emulation error, a panic): halted() = 0x100
    Halted(String),
}

/// what was booted
struct Package {
    bytes: Vec<u8>,
    identity: String,
    sha: String,
    /// where it came from: "staged", "image", or FM1EMU_FWSC's path
    from: String,
}

struct State {
    mode: Mode,
    m: Option<Box<Machine>>,
    pkg: Option<Package>,
    /// flash_stage's bytes (None: nothing staged), options()'s choice
    staged: Option<Vec<u8>>,
    source: Option<Vec<u8>>,
    /// the flash image (IMAGE bytes): what flash() returns, refreshed on each call
    img: Vec<u8>,
    dirty: u32,
    rev_seen: u32,
    jit: bool,
    jit_note: &'static str,
    // the clock: the emulated chip's (Hz; fm1core_boot_option "mhz", taken at the next init) and the device ms
    hz: u64,
    hz_next: u64,
    ms: u64,
    // input
    down: [bool; 41],
    tap_until: [u64; 41],
    master: i32,
    midi: VecDeque<u8>,
    // audio
    fifo: fifo::Fifo,
    halves_seen: u64,
    // output
    lcd_rev: u32,
    led_ms: u64,
    /// the last LED_AVG takes' on-fractions (key ids 0..40), a ring
    led_hist: [[f32; 41]; LED_AVG],
    led_at: usize,
    led_had: usize,
    /// FM1EMU_LED_TRACE: print the LEDs' on-fractions (the calibration of LED_LIT_FRACTION)
    led_trace: bool,
    // speed: emulated ms per wall ms spent in tick, over the last second of device time
    speed: f32,
    win_ms: u64,
    win_busy: Duration,
    busy_total: Duration,
}

impl State {
    fn new() -> State {
        State {
            mode: Mode::Off,
            m: None,
            pkg: None,
            staged: None,
            source: None,
            img: Vec::new(),
            dirty: 0,
            rev_seen: 0,
            jit: true,
            jit_note: "",
            hz: DEFAULT_MHZ as u64 * 1_000_000,
            hz_next: DEFAULT_MHZ as u64 * 1_000_000,
            ms: 0,
            down: [false; 41],
            tap_until: [0; 41],
            master: 0,
            midi: VecDeque::new(),
            fifo: fifo::Fifo::new(LATENCY, DEFAULT_MHZ as u64 * 1_000_000),
            halves_seen: 0,
            lcd_rev: u32::MAX,
            led_ms: 0,
            led_hist: [[0.0; 41]; LED_AVG],
            led_at: 0,
            led_had: 0,
            led_trace: false,
            speed: 0.0,
            win_ms: 0,
            win_busy: Duration::ZERO,
            busy_total: Duration::ZERO,
        }
    }
}

static mut STATE: Option<State> = None;
static mut PANEL: Panel = unsafe { std::mem::zeroed() };

#[allow(static_mut_refs)]
fn st() -> &'static mut State {
    // SAFETY: one thread at a time (fm1core.h THREADING CONTRACT); the module's own globals
    unsafe { (*std::ptr::addr_of_mut!(STATE)).get_or_insert_with(State::new) }
}
fn panel() -> &'static mut Panel {
    // SAFETY: as st(); the host writes the input fields between entry points only
    unsafe { &mut *std::ptr::addr_of_mut!(PANEL) }
}
fn take_u32(v: &mut u32) -> u32 {
    // SAFETY: a field of the panel, a valid aligned u32
    unsafe {
        let x = std::ptr::read_volatile(v);
        std::ptr::write_volatile(v, 0);
        x
    }
}
fn take_i32(v: &mut i32) -> i32 {
    // SAFETY: as take_u32
    unsafe {
        let x = std::ptr::read_volatile(v);
        std::ptr::write_volatile(v, 0);
        x
    }
}
fn rd_u32(v: &u32) -> u32 {
    // SAFETY: as take_u32
    unsafe { std::ptr::read_volatile(v) }
}
fn rd_i32(v: &i32) -> i32 {
    // SAFETY: as take_u32
    unsafe { std::ptr::read_volatile(v) }
}

/// run an entry point's body; a panic halts the core and gives `dflt`
fn guard<R>(dflt: R, f: impl FnOnce() -> R) -> R {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => r,
        Err(e) => {
            let what = e
                .downcast_ref::<&str>()
                .map(|s| s.to_string())
                .or_else(|| e.downcast_ref::<String>().cloned())
                .unwrap_or_else(|| "a panic".into());
            let s = st();
            s.mode = Mode::Halted(format!("panic: {what}"));
            dflt
        }
    }
}

// ------------------------------------------------------------------------------------------------ JIT memory
extern "C" {
    fn mmap(addr: *mut c_void, len: usize, prot: c_int, flags: c_int, fd: c_int, off: i64) -> *mut c_void;
    fn munmap(addr: *mut c_void, len: usize) -> c_int;
}

/// Can this process map JIT memory (MAP_JIT)? fm1-core's JIT (crates/fm1-core/src/jit.rs CodeCache::new) needs
/// it; in a hardened-runtime host without com.apple.security.cs.allow-jit the mmap fails, and fm1-core then tries
/// again at every block it runs (Jit::new is retried while `jit` is None): the core turns the JIT off instead.
fn jit_available() -> bool {
    const MAP_PRIVATE: c_int = 0x0002;
    const MAP_ANON: c_int = 0x1000;
    const MAP_JIT: c_int = 0x0800;
    if !cfg!(all(target_arch = "aarch64", target_os = "macos")) {
        return false; // (fm1-core compiles its JIT for AArch64 macOS only)
    }
    // SAFETY: an anonymous mapping, unmapped at once
    unsafe {
        let p = mmap(std::ptr::null_mut(), 1 << 14, 7, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        if p as isize == -1 || p.is_null() {
            return false;
        }
        munmap(p, 1 << 14);
    }
    true
}

// ------------------------------------------------------------------------------------------------ boot
fn image_buf(s: &mut State) -> &mut Vec<u8> {
    if s.img.len() != IMAGE {
        s.img = vec![0xff; IMAGE];
    }
    &mut s.img
}

/// the package in an fm1emu image's slot (magic, length, CRC-32, the bytes), if it is one
fn slot_package(b: &[u8]) -> Option<&[u8]> {
    if b.len() != IMAGE || &b[SLOT..SLOT + 8] != MAGIC {
        return None;
    }
    let len = u32::from_le_bytes(b[SLOT + 8..SLOT + 12].try_into().unwrap()) as usize;
    let crc = u32::from_le_bytes(b[SLOT + 12..SLOT + 16].try_into().unwrap());
    if len == 0 || len > PKG_MAX {
        return None;
    }
    let pkg = &b[SLOT + SLOT_HEAD..SLOT + SLOT_HEAD + len];
    (sum::crc32(pkg) == crc).then_some(pkg)
}

fn empty(s: &mut State, why: &str) {
    s.mode = Mode::Empty(why.to_string());
    s.m = None;
    let p = panel();
    let bg: u16 = 0x10a2; // a dark grey
    let mut fb = vec![bg; LCD_W * LCD_H];
    font::draw(&mut fb, 92, 3, "LOAD .FWSC", 0xfd20); // amber
    let mut line = why.to_ascii_uppercase();
    line.truncate(19);
    font::draw(&mut fb, 136, 2, &line, 0x9cf3); // light grey
    for (o, v) in p.lcd.iter_mut().zip(fb.iter()) {
        *o = v.swap_bytes(); // (RGB565 big-endian as sent)
    }
    p.lcd_writes = p.lcd_writes.wrapping_add(1);
    for c in p.led.iter_mut() {
        *c = [0; NLINE];
    }
    p.led_play_green = 0;
}

/// boot `pkg` (a .fwsc) on a fresh flash, then (case 2) the image's NOR sectors that differ from the fresh one
fn boot_package(s: &mut State, pkg: Vec<u8>, nor: Option<&[u8]>, from: &str) -> Result<(), String> {
    if pkg.len() > PKG_MAX {
        return Err(format!("package too large ({} bytes)", pkg.len()));
    }
    let st = catch_unwind(|| fm1_soc::stock_from_package(&pkg, None))
        .map_err(|_| "malformed package".to_string())??;
    if st.board != Board::Fm1 {
        return Err(format!("{}: an SMK-37 package", st.identity));
    }
    let identity = st.identity.clone();
    let mut m = Box::new(Machine::boot(st, s.jit, s.hz));
    if let Some(nor) = nor {
        for at in (0..NOR).step_by(SECTOR) {
            if nor[at..at + SECTOR] != m.soc.flash.mem[at..at + SECTOR] {
                m.restore_sector(at, &nor[at..at + SECTOR]);
            }
        }
    }
    // the image: the live NOR and the slot
    let img = image_buf(s);
    img[..NOR].copy_from_slice(&m.soc.flash.mem);
    img[SLOT..SLOT + 8].copy_from_slice(MAGIC);
    img[SLOT + 8..SLOT + 12].copy_from_slice(&(pkg.len() as u32).to_le_bytes());
    img[SLOT + 12..SLOT + 16].copy_from_slice(&sum::crc32(&pkg).to_le_bytes());
    img[SLOT + SLOT_HEAD..SLOT + SLOT_HEAD + pkg.len()].copy_from_slice(&pkg);
    img[SLOT + SLOT_HEAD + pkg.len()..].fill(0xff);
    s.rev_seen = m.soc.flash.revision;
    s.dirty = s.dirty.wrapping_add(1);
    s.pkg = Some(Package { sha: sum::hex(&sum::sha256(&pkg)[..4]), identity, bytes: pkg, from: from.to_string() });
    s.m = Some(m);
    s.mode = Mode::Running;
    Ok(())
}

fn power_on(s: &mut State) {
    s.led_trace = std::env::var_os("FM1EMU_LED_TRACE").is_some();
    s.jit = jit_available() && std::env::var("FM1EMU_JIT").map_or(true, |v| v != "0");
    s.jit_note = if !jit_available() {
        "JIT unavailable (MAP_JIT refused): interpreter"
    } else if s.jit {
        "JIT on"
    } else {
        "JIT off (FM1EMU_JIT=0)"
    };
    let bytes = s.source.take().unwrap_or_default();
    // 1 / 2: the staged bytes, a package or an image
    let mut why = String::from("NO FIRMWARE");
    if !bytes.is_empty() {
        let r = if let Some(pkg) = slot_package(&bytes) {
            boot_package(s, pkg.to_vec(), Some(&bytes[..NOR]), "image")
        } else if catch_unwind(|| fm1_soc::fwsc::unpack(&bytes).is_ok()).unwrap_or(false) {
            boot_package(s, bytes, None, "staged")
        } else {
            Err("not a package or an fm1emu image".into())
        };
        match r {
            Ok(()) => return,
            Err(e) => why = e,
        }
    }
    // 3: nothing usable staged: FM1EMU_FWSC (tests and development), else no firmware
    if let Ok(path) = std::env::var("FM1EMU_FWSC") {
        match std::fs::read(&path) {
            Ok(b) => match boot_package(s, b, None, &path) {
                Ok(()) => return,
                Err(e) => why = e,
            },
            Err(e) => why = format!("FM1EMU_FWSC: {e}"),
        }
    }
    let img = image_buf(s);
    img.fill(0xff);
    s.dirty = s.dirty.wrapping_add(1);
    empty(s, &why);
}

// ------------------------------------------------------------------------------------------------ input / output
/// the panel's input into the machine (fm1core.h: read at the top of tick, frame and idle)
fn input(s: &mut State) {
    let Some(m) = s.m.as_mut() else { return };
    let p = panel();
    let keys = rd_u32(&p.keys) & ((1 << NKEY) - 1);
    let ktaps = take_u32(&mut p.keys_tap) & ((1 << NKEY) - 1);
    let btns = rd_u32(&p.buttons) & ((1 << NBTN) - 1);
    let btaps = take_u32(&mut p.buttons_tap) & ((1 << NBTN) - 1);
    // key ids: 0..13 the matrix buttons, 14 + n note key n (the keymap's ids, fm1-soc's Action::Key)
    let held = (btns as u64) | (keys as u64) << 14;
    let taps = (btaps as u64) | (ktaps as u64) << 14;
    for id in 0..41usize {
        let (h, t) = (held >> id & 1 != 0, taps >> id & 1 != 0);
        if h {
            s.tap_until[id] = 0;
            if !s.down[id] {
                s.down[id] = true;
                m.soc.periph.apply(&Action::Key { id: id as u8, down: true });
            }
        } else if t {
            s.tap_until[id] = s.ms + TAP_MS;
            if !s.down[id] {
                s.down[id] = true;
                m.soc.periph.apply(&Action::Key { id: id as u8, down: true });
            }
        } else if s.down[id] && s.ms >= s.tap_until[id] {
            s.down[id] = false;
            s.tap_until[id] = 0;
            m.soc.periph.apply(&Action::Key { id: id as u8, down: false });
        }
    }
    for e in 0..NENC {
        let d = take_i32(&mut p.enc[e]);
        if d != 0 {
            m.soc.periph.apply(&Action::Enc { enc: e as u8, clicks: d });
        }
    }
    let mv = rd_i32(&p.master).clamp(0, 1023);
    if mv != s.master {
        s.master = mv;
        m.soc.periph.apply(&Action::Adc { ch: 4, value: mv as u16 });
    }
}

fn leds_out(s: &mut State) {
    let Some(m) = s.m.as_mut() else { return };
    let now = m.soc.periph.ms();
    if now < s.led_ms + LED_WINDOW_MS {
        return;
    }
    s.led_ms = now;
    let take = m.soc.periph.led_take();
    s.led_hist[s.led_at].copy_from_slice(&take[..41]);
    s.led_at = (s.led_at + 1) % LED_AVG;
    s.led_had = (s.led_had + 1).min(LED_AVG);
    let mut f = [0f32; 41];
    for h in s.led_hist.iter().take(s.led_had) {
        for (o, v) in f.iter_mut().zip(h.iter()) {
            *o += v / s.led_had as f32;
        }
    }
    if s.led_trace {
        let lit: Vec<String> = (0..41).filter(|&i| f[i] > 0.005).map(|i| format!("{i}:{:.3}", f[i])).collect();
        eprintln!("fm1emu leds {now} ms: {}", lit.join(" "));
    }
    let p = panel();
    for (r, row) in KEYMAP.iter().enumerate().take(NLINE + 1).skip(1) {
        for (c, &id) in row.iter().enumerate() {
            let v = if id >= 0 { (f[id as usize] / LED_LIT_FRACTION * 255.0).round().clamp(0.0, 255.0) as u8 } else { 0 };
            p.led[c][r - 1] = v;
        }
    }
    // PLAY's green LED (column 8, row bit 1) has no key id in fm1-soc's matrix (KEYMAP -1 there): its LED
    // accounting credits key ids only, so it is not modelled: 0
    p.led_play_green = 0;
}

fn lcd_out(s: &mut State) {
    let Some(m) = s.m.as_ref() else { return };
    if m.soc.lcd.revision == s.lcd_rev {
        return;
    }
    s.lcd_rev = m.soc.lcd.revision;
    let p = panel();
    for (o, v) in p.lcd.iter_mut().zip(m.soc.lcd.fb.iter()) {
        *o = v.swap_bytes(); // fm1-soc's fb holds the pixel values (hi << 8 | lo as sent): big-endian bytes here
    }
    p.lcd_writes = p.lcd_writes.wrapping_add(1);
}

fn sync_dirty(s: &mut State) {
    if let Some(m) = s.m.as_ref() {
        if m.soc.flash.revision != s.rev_seen {
            s.rev_seen = m.soc.flash.revision;
            s.dirty = s.dirty.wrapping_add(1);
        }
    }
}

fn halt(s: &mut State, why: String) {
    s.mode = Mode::Halted(why);
}

// ------------------------------------------------------------------------------------------------ entry points
extern "C" fn options(path: *const c_char, _no_flash: i32, _save_on_exit: i32, _headless: i32) {
    guard((), || {
        let s = st();
        s.source = if path.is_null() {
            None
        } else {
            // SAFETY: a NUL-terminated string from the host
            let p = unsafe { CStr::from_ptr(path) };
            if p.to_bytes() == FLASH_STAGED {
                s.staged.clone()
            } else {
                // a file: read as staged bytes (a package or an image); nothing is written back to it
                std::fs::read(p.to_string_lossy().as_ref()).ok()
            }
        };
    })
}

extern "C" fn init(_demo: i32) {
    guard((), || {
        let s = st();
        let p = panel();
        p.btn_id = BTN_ID;
        p.enc_id = ENC_ID;
        p.enc_dir = [1; NE - 1];
        s.ms = 0;
        s.down = [false; 41];
        s.tap_until = [0; 41];
        s.midi.clear();
        s.hz = s.hz_next;
        s.fifo = fifo::Fifo::new(LATENCY, s.hz);
        s.lcd_rev = u32::MAX;
        s.led_ms = 0;
        s.led_at = 0;
        s.led_had = 0;
        power_on(s);
        // MASTER: the host's value, or (0) the machine's default (fm1-soc's ADC channel 4: 900)
        let mv = rd_i32(&p.master);
        if let Some(m) = s.m.as_mut() {
            if mv > 0 {
                m.soc.periph.apply(&Action::Adc { ch: 4, value: mv.clamp(0, 1023) as u16 });
                s.master = mv.clamp(0, 1023);
            } else {
                s.master = m.soc.periph.adc_value[4] as i32;
                p.master = s.master;
            }
            s.halves_seen = m.soc.periph.audio_halves;
        }
        input(s);
        lcd_out(s);
        p.ready = 1;
    })
}

extern "C" fn tick(ms: u32) {
    guard((), || {
        let s = st();
        if s.mode != Mode::Running {
            return;
        }
        s.ms = ms as u64;
        input(s);
        let Some(m) = s.m.as_mut() else { return };
        while let Some(b) = s.midi.pop_front() {
            m.soc.periph.apply(&Action::Midi { b });
        }
        let t0 = Instant::now();
        let target = (ms as u64 + 1) * s.hz / 1000;
        loop {
            // (before the first DMA half, short steps of 10 us: its end is the stream's time origin, fifo.rs)
            let first = !s.fifo.started;
            let step = if first { s.hz / 100_000 } else { s.hz / 1000 };
            if let Err(e) = m.run_to(target, step, first) {
                halt(s, e);
                return;
            }
            // the halves that finished playing, on the device's time axis (fifo.rs)
            let halves = m.soc.periph.audio_halves;
            s.fifo.push(&m.soc.periph.audio, halves.saturating_sub(s.halves_seen), m.soc.periph.cycles);
            m.soc.periph.audio.clear();
            s.halves_seen = halves;
            if m.soc.periph.cycles >= target {
                break;
            }
        }
        let dt = t0.elapsed();
        // the speed figure: device ms per wall ms, over each second of device time
        s.win_busy += dt;
        s.busy_total += dt;
        s.win_ms += 1;
        if s.win_ms >= 1000 {
            let busy = s.win_busy.as_secs_f32() * 1000.0;
            s.speed = if busy > 0.0 { s.win_ms as f32 / busy } else { 0.0 };
            s.win_ms = 0;
            s.win_busy = Duration::ZERO;
        }
        sync_dirty(s);
    })
}

extern "C" fn frame() {
    guard((), || {
        let s = st();
        if s.mode != Mode::Running {
            return;
        }
        input(s);
        lcd_out(s);
        leds_out(s);
    })
}

extern "C" fn idle() {
    guard((), || {
        let s = st();
        if s.mode != Mode::Running {
            return;
        }
        input(s);
        leds_out(s);
    })
}

extern "C" fn audio(out: *mut i16, frames: u32) {
    // SAFETY: the host's buffer of `frames` stereo frames
    let buf = unsafe { std::slice::from_raw_parts_mut(out, 2 * frames as usize) };
    buf.fill(0);
    guard((), || {
        let s = st();
        if s.mode != Mode::Running {
            return; // (no firmware, halted: silence)
        }
        s.fifo.pull(buf);
    })
}

extern "C" fn midi_in(pkt: u32) -> i32 {
    guard(1, || {
        let s = st();
        let Some(m) = s.m.as_ref().filter(|_| s.mode == Mode::Running) else {
            return 1; // (no machine: taken and dropped, never "retry")
        };
        if m.soc.periph.midi_queued() + s.midi.len() + 3 > MIDI_ROOM {
            return 0;
        }
        // a USB-MIDI event packet (cable << 4 | CIN, then the bytes: engine/Processor decodePacket's layout):
        // its bytes onto the MIDI IN jack, as a keyboard on the TRS input would send them
        const LEN: [usize; 16] = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1];
        let n = LEN[(pkt & 15) as usize];
        for i in 0..n {
            s.midi.push_back((pkt >> (8 * (i + 1))) as u8);
        }
        1
    })
}

extern "C" fn midi_out_take(_pkt: *mut u32) -> i32 {
    // fm1-soc has no MIDI or USB transmit path (the UART's TX DMA finishes at once into nothing, periph.rs; the
    // USB block is register storage): nothing comes out
    0
}

fn info_text(s: &State) -> String {
    let mode = match &s.mode {
        Mode::Off => "off".to_string(),
        Mode::Empty(w) => format!("no firmware ({w})"),
        Mode::Running => "running".to_string(),
        Mode::Halted(w) => format!("halted: {w}"),
    };
    let pkg = s.pkg.as_ref().map_or("-".to_string(), |p| {
        format!("{} {} bytes sha256 {} from {}", p.identity, p.bytes.len(), p.sha, p.from)
    });
    let ms = s.m.as_ref().map_or(0, |m| m.soc.periph.ms());
    let cycles = s.m.as_ref().map_or(0, |m| m.soc.periph.cycles);
    let f = &s.fifo;
    format!(
        "fm1emu {mode}; {pkg}; board FM-1; clock {} MHz; H {} L {}; margin {}..{}; speed {:.1}x; {} ms; cycles {}; underflows {} dropped {} gaps {}; {}",
        s.hz / 1_000_000,
        f.half,
        f.lat,
        if f.low == i64::MAX { 0 } else { f.low },
        if f.high == i64::MIN { 0 } else { f.high },
        s.speed,
        ms,
        cycles,
        f.underflows,
        f.dropped,
        f.gaps,
        s.jit_note
    )
}

extern "C" fn ui_info(buf: *mut c_char, n: u32) {
    guard((), || {
        if buf.is_null() || n == 0 {
            return;
        }
        let t = info_text(st());
        let b = t.as_bytes();
        let k = b.len().min(n as usize - 1);
        // SAFETY: the host's buffer of n bytes
        unsafe {
            std::ptr::copy_nonoverlapping(b.as_ptr(), buf as *mut u8, k);
            *buf.add(k) = 0;
        }
    })
}

extern "C" fn dump() {
    guard((), || {
        let s = st();
        println!("{}", info_text(s));
        if let Some(m) = s.m.as_ref() {
            println!(
                "  pc {:#010x} cpu1 {} cycles {} skipped {} midi queued {} dropped {} flash rev {} lcd rev {}",
                m.cpu.pc,
                m.cpu1.as_ref().map_or("-".into(), |c| format!("{:#010x}", c.pc)),
                m.soc.periph.cycles,
                m.soc.periph.skipped,
                m.soc.periph.midi_queued(),
                m.soc.periph.midi_dropped(),
                m.soc.flash.revision,
                m.soc.lcd.revision
            );
        }
    })
}

extern "C" fn stats(shed: *mut u32, cpu_pct: *mut u32) {
    guard((), || {
        let s = st();
        let pct = if s.speed > 0.0 { (100.0 / s.speed).round() as u32 } else { 0 };
        // SAFETY: the host's two outputs
        unsafe {
            if !shed.is_null() {
                *shed = s.fifo.underflows.wrapping_add(s.fifo.dropped as u32);
            }
            if !cpu_pct.is_null() {
                *cpu_pct = pct;
            }
        }
    })
}

extern "C" fn shutdown() {
    guard((), || {
        let s = st();
        if let Some(m) = s.m.take() {
            let img = image_buf(s);
            img[..NOR].copy_from_slice(&m.soc.flash.mem);
            if m.soc.flash.revision != s.rev_seen {
                s.dirty = s.dirty.wrapping_add(1);
            }
            drop(m); // (the machine's memory; fm1-core's JIT pages: README "Memory")
        }
        s.fifo = fifo::Fifo::new(s.fifo.lat, s.hz);
        s.midi = VecDeque::new();
        s.source = None;
        s.staged = None;
        if let Some(p) = s.pkg.as_mut() {
            p.bytes = Vec::new();
        }
        if !matches!(s.mode, Mode::Halted(_)) {
            s.mode = Mode::Off;
        }
    })
}

extern "C" fn halted() -> i32 {
    guard(0x100, || if matches!(st().mode, Mode::Halted(_)) { 0x100 } else { 0 })
}

extern "C" fn flash_stage(bytes: *const u8, n: u32) -> i32 {
    guard(0, || {
        let s = st();
        s.staged = Some(if bytes.is_null() || n == 0 {
            Vec::new()
        } else {
            // SAFETY: the host's n bytes
            unsafe { std::slice::from_raw_parts(bytes, n as usize) }.to_vec()
        });
        1
    })
}

extern "C" fn flash() -> *mut u8 {
    guard(std::ptr::null_mut(), || {
        let s = st();
        if let Some(m) = s.m.as_ref() {
            let mem = m.soc.flash.mem.as_ptr();
            let img = image_buf(s);
            // SAFETY: the NOR is NOR bytes, the image longer
            unsafe { std::ptr::copy_nonoverlapping(mem, img.as_mut_ptr(), NOR) };
        }
        image_buf(s).as_mut_ptr()
    })
}

extern "C" fn flash_dirty() -> u32 {
    guard(0, || {
        let s = st();
        sync_dirty(s);
        s.dirty
    })
}

extern "C" fn flash_sync() -> i32 {
    // the firmware saves by itself, over SPI: the image is current unless a command is under way (chip select low)
    guard(1, || st().m.as_ref().map_or(1, |m| (!m.soc.flash.busy()) as i32))
}

// ------------------------------------------------------------------------------------------------ the descriptor
static mut DESC: Option<Core> = None;

fn leak(s: &str) -> *const c_char {
    CString::new(s).unwrap().into_raw()
}

const CREDITS: &str = "The emulator: fm1-emu by Joakim Eriksson (https://github.com/joakimeriksson/fm1-emu), \
its fm1-core, fm1-soc and fm1-isa crates, with FM1VST's patches (core-glue/fm1emu/patches: the P33 power block, \
JIT and timer speed-ups; offered upstream). The firmware a user loads carries its own licence.\n\
\n\
fm1-emu's credits: built on the community's reverse engineering of the FM-1: AL-255/FM-1-RE, ip2k/lunar-modulator, \
kagaimiq's jielie, the hardware layer of hugelton/Felucca and awesome-fm-1. Thanks to every firmware author for \
publishing their work.\n\
\n\
MIT License\n\
\n\
Copyright (c) 2026 Joakim Eriksson\n\
\n\
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated \
documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation \
the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to \
permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\
\n\
The above copyright notice and this permission notice shall be included in all copies or substantial portions of \
the Software.\n\
\n\
THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO \
THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE \
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, \
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE \
SOFTWARE.";

/// the module's one symbol (fm1core.h): the ABI 6 descriptor, NULL for any other ABI
#[no_mangle]
pub extern "C" fn fm1core_get(abi_version: u32) -> *const Core {
    if abi_version != ABI {
        return std::ptr::null();
    }
    guard(std::ptr::null(), || {
        // SAFETY: one thread at a time; built once, then only read
        let d = unsafe { &mut *std::ptr::addr_of_mut!(DESC) };
        if d.is_none() {
            let commit = option_env!("FM1EMU_COMMIT").unwrap_or("unknown");
            let short: String = commit.chars().take(7).collect();
            // the patches applied over the pinned commit (core-glue/fm1emu/patches, core.cmake): "0001 0002"
            let patches = option_env!("FM1EMU_PATCHES").unwrap_or("").trim();
            let version = if patches.is_empty() {
                format!("fm1-emu {short}")
            } else {
                format!("fm1-emu {short} + {}", patches.replace(' ', "+"))
            };
            let names = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"];
            let mut button_names = [std::ptr::null(); NB];
            for (o, n) in button_names.iter_mut().zip(names.iter()) {
                *o = leak(n);
            }
            *d = Some(Core {
                abi_version: ABI,
                id: leak("fm1emu"),
                name: leak("FM-1 (emulated)"),
                version: leak(&version),
                source_url: leak(&format!("https://github.com/joakimeriksson/fm1-emu@{commit}")),
                family: leak("fm1emu"),
                license: leak("MIT"),
                gated: std::ptr::null(),
                flash_size: IMAGE as u32,
                button_names,
                params: std::ptr::null(),
                nparams: 0,
                panel: std::ptr::addr_of_mut!(PANEL),
                keymap: KEYMAP.as_ptr() as *const i8,
                options: Some(options),
                boot_options: None,
                init: Some(init),
                tick: Some(tick),
                frame: Some(frame),
                idle: Some(idle),
                audio: Some(audio),
                midi_in: Some(midi_in),
                midi_out_take: Some(midi_out_take),
                dump: Some(dump),
                stats: Some(stats),
                ui_info: Some(ui_info),
                shutdown: Some(shutdown),
                halted: Some(halted),
                flash_stage: Some(flash_stage),
                flash: Some(flash),
                flash_dirty: Some(flash_dirty),
                flash_sync: Some(flash_sync),
                param_epoch: None,
                knob_target: None,
                credits: leak(CREDITS),
            });
        }
        d.as_ref().unwrap() as *const Core
    })
}

/// The core's added audio delay in 44.1 kHz frames (the FIFO's prime, README "Latency"): not in the descriptor (no
/// ABI change), found with dlsym. Before init: LATENCY; after it, this instance's L (larger only for a package
/// whose DMA half exceeds LATENCY). The host adds it to its own: setLatencySamples(L * hostRate / 44100 + the
/// resampler's and Device's own latency, Device::latency_frames).
#[no_mangle]
pub extern "C" fn fm1core_latency_frames() -> u32 {
    guard(LATENCY, || st().fifo.lat)
}

/// An option for the next power-on (fm1core.h "optional module symbols"; not in the descriptor, no ABI change): found
/// with dlsym, called before init on the clock's thread (or with the clock stopped). Keys:
///   "mhz"   the emulated chip's clock in MHz, 60..1000 (default 360): fm1-soc's periph.hz and tick()'s cycles per ms
/// Returns 1 taken, 0 an unknown key, -1 a bad value (the option unchanged).
#[no_mangle]
pub extern "C" fn fm1core_boot_option(key: *const c_char, value: *const c_char) -> c_int {
    guard(-1, || {
        if key.is_null() || value.is_null() {
            return -1;
        }
        // SAFETY: NUL-terminated strings from the host
        let (k, v) = unsafe { (CStr::from_ptr(key).to_string_lossy(), CStr::from_ptr(value).to_string_lossy()) };
        match k.as_ref() {
            "mhz" => match v.trim().parse::<u32>() {
                Ok(m) if (MHZ_MIN..=MHZ_MAX).contains(&m) => {
                    st().hz_next = m as u64 * 1_000_000;
                    1
                }
                _ => -1,
            },
            _ => 0,
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn boot_option_mhz() {
        let k = CString::new("mhz").unwrap();
        for (v, want) in [("480", 1), ("59", -1), ("1001", -1), ("x", -1)] {
            let v = CString::new(v).unwrap();
            assert_eq!(fm1core_boot_option(k.as_ptr(), v.as_ptr()), want);
        }
        assert_eq!(st().hz_next, 480_000_000);
        let other = CString::new("nope").unwrap();
        assert_eq!(fm1core_boot_option(other.as_ptr(), k.as_ptr()), 0);
    }
    #[test]
    fn keymap_is_fm1_socs() {
        // fm1-soc's periph.rs KEYMAP is private: check through its LED accounting instead would need a firmware;
        // the table here is core-glue/common/panel.h's FM1P_KEYMAP_INIT verbatim, and every id 0..40 occurs once
        let mut seen = [0; 41];
        for row in KEYMAP.iter() {
            for &id in row {
                if id >= 0 {
                    seen[id as usize] += 1;
                }
            }
        }
        assert!(seen.iter().all(|&n| n == 1));
    }
}
