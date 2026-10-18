// SPDX-License-Identifier: GPL-3.0-only
//! The audio between the emulated SoC and the ABI's 128-frame blocks (README "Audio").
//!
//! The SoC hands over a DMA half when it has finished playing (fm1-soc's `periph.audio`), so frames arrive in
//! bursts of one half (H frames), and tick() may run the machine past its millisecond (an idle skip jumps to the
//! next timed event: during a boot that can be tens of ms). A plain FIFO primed once at the first half then holds
//! the wrong amount whenever the machine ran ahead at that moment (measured: SLOOP's first burst came 42 ms
//! early and its FIFO ran dry 40 ms later, for good). So the stream is kept on the device's own time axis:
//!
//! * every frame has its device frame index, its play time in frames of the device's I2S clock, 24 MHz / 544 =
//!   44117.647 Hz (fm1-soc's FRAME_T4, Felucca's DAC_TICKS): index = cycles / 5440 at 240 MHz;
//! * the first half's end is found exactly (tick() runs in short steps until it arrives), the halves after it
//!   follow on contiguously; a burst whose end lies a half or more past where the stream ends (the DMA was
//!   stopped) is placed at its own time, silence in the gap;
//! * ABI frame k (counted from power-on, 44100 Hz) plays the device's stream at index (k - L) * 44117.647 / 44100,
//!   through a 4-point Lagrange interpolator (engine/resampler.h's): the 0.04 % between the two clocks is taken
//!   up exactly, nothing creeps, nothing is dropped, and the delay is exactly L frames whatever ran ahead.
//!
//! L must cover the half still playing (a frame is there only once its half is over, up to H - 1 frames after
//! its time) less the millisecond tick() runs ahead of audio(): L >= H - 42. Not there: silence, counted
//! (`underflows`); arriving after its time: dropped, counted (`dropped`).

use std::collections::VecDeque;

/// device frames per ABI frame: (24 MHz / 544) / 44100
pub const STEP: f64 = 24_000_000.0 / 544.0 / 44_100.0;
/// CPU cycles per device frame at 240 MHz (240e6 * 544 / 24e6)
pub const CYCLES_PER_FRAME: u64 = 5440;
/// the most frames kept ahead of the read position (a machine far ahead): 3 s
const MAX_AHEAD: usize = 3 * 44_118;

pub struct Fifo {
    buf: VecDeque<[i16; 2]>,
    /// the device frame index of buf[0]
    base: i64,
    pub started: bool,
    pub lat: u32,
    pub half: u32,
    /// ABI frames given out since power-on
    k: u64,
    pub underflows: u32,
    pub dropped: u64,
    /// frames of silence put into gaps (the DMA stopped and started again)
    pub gaps: u64,
    /// the margin: frames there beyond the last one read, lowest and highest after a pull
    pub low: i64,
    pub high: i64,
}

impl Fifo {
    pub fn new(lat: u32) -> Fifo {
        Fifo {
            buf: VecDeque::new(),
            base: 0,
            started: false,
            lat,
            half: 0,
            k: 0,
            underflows: 0,
            dropped: 0,
            gaps: 0,
            low: i64::MAX,
            high: i64::MIN,
        }
    }

    fn end(&self) -> i64 {
        self.base + self.buf.len() as i64
    }

    /// a burst of interleaved stereo i16 (`halves` DMA halves), the last of which ended at or before `cycles`
    /// (exactly at it, give or take a run step, for the first: tick() runs in short steps until it comes)
    pub fn push(&mut self, a: &[i16], halves: u64, cycles: u64) {
        if a.is_empty() {
            return;
        }
        let n = (a.len() / 2) as i64;
        let h = (n as u64 / halves.max(1)) as u32;
        if h > self.half {
            self.half = h;
        }
        let at = (cycles / CYCLES_PER_FRAME) as i64; // (the burst's end is here or up to a half before)
        if !self.started {
            self.started = true;
            // (a half larger than the prime: this instance's L grows to cover it, in whole blocks)
            if self.half > self.lat {
                self.lat = self.half.div_ceil(128) * 128;
            }
            self.base = at - n;
        } else if at - (self.end() + n) >= self.half as i64 {
            // a gap: the burst ends a half or more after the stream would: the DMA stopped meanwhile
            let g = at - (self.end() + n);
            self.buf.extend(std::iter::repeat([0, 0]).take(g as usize));
            self.gaps += g as u64;
        }
        self.buf.extend(a.chunks_exact(2).map(|f| [f[0], f[1]]));
        // what the reader has passed already (a late burst) goes, and a machine far ahead keeps at most MAX_AHEAD
        let x = self.pos(self.k) as i64 - 1;
        if x > self.base {
            let late = ((x - self.base) as usize).min(self.buf.len());
            self.dropped += late.saturating_sub(self.buf.len().saturating_sub(n as usize)) as u64;
            self.buf.drain(..late);
            self.base += late as i64;
        }
        if self.buf.len() > MAX_AHEAD {
            let over = self.buf.len() - MAX_AHEAD;
            self.buf.drain(..over);
            self.base += over as i64;
            self.dropped += over as u64;
        }
    }

    /// the device stream's position (frames) ABI frame k plays
    fn pos(&self, k: u64) -> f64 {
        (k as f64 - self.lat as f64) * STEP
    }

    /// `out.len() / 2` frames at 44100 Hz: silence before the stream starts (not counted) and where it has not
    /// arrived yet (counted, once a call)
    pub fn pull(&mut self, out: &mut [i16]) {
        out.fill(0);
        let frames = out.len() / 2;
        if !self.started {
            self.k += frames as u64;
            return;
        }
        let mut dry = false;
        for o in out.chunks_exact_mut(2) {
            let x = self.pos(self.k);
            self.k += 1;
            let i = x.floor() as i64;
            let r = i - 1 - self.base;
            if r < 0 {
                continue; // (before the stream: silence)
            }
            if r as usize + 3 >= self.buf.len() {
                dry = true;
                continue;
            }
            let t = (x - i as f64) as f32;
            let c0 = -t * (t - 1.0) * (t - 2.0) * (1.0 / 6.0);
            let c1 = (t + 1.0) * (t - 1.0) * (t - 2.0) * 0.5;
            let c2 = -(t + 1.0) * t * (t - 2.0) * 0.5;
            let c3 = (t + 1.0) * t * (t - 1.0) * (1.0 / 6.0);
            let r = r as usize;
            for (ch, v) in o.iter_mut().enumerate() {
                let s = c0 * self.buf[r][ch] as f32
                    + c1 * self.buf[r + 1][ch] as f32
                    + c2 * self.buf[r + 2][ch] as f32
                    + c3 * self.buf[r + 3][ch] as f32;
                *v = s.round().clamp(-32768.0, 32767.0) as i16;
            }
        }
        if dry {
            self.underflows += 1;
        }
        // the frames behind the window of the next read go
        let next = self.pos(self.k).floor() as i64 - 1;
        if next > self.base {
            let n = ((next - self.base) as usize).min(self.buf.len());
            self.buf.drain(..n);
            self.base += n as i64;
        }
        let margin = self.end() - (self.pos(self.k - 1).floor() as i64 + 3);
        self.low = self.low.min(margin);
        self.high = self.high.max(margin);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// halves of `h` frames from `t0_ms` at the device's rate, the ABI's blocks at 44100, `secs` long; the
    /// machine runs `ahead` ms past each millisecond (an idle skip) every `every` ms
    fn run(h: u64, lat: u32, secs: u64, ahead: u64, every: u64) -> Fifo {
        let mut f = Fifo::new(lat);
        let half_cycles = h * CYCLES_PER_FRAME;
        let t0 = 180 * 240_000u64;
        let (mut made, mut taken, mut cycles) = (0u64, 0u64, 0u64);
        let mut out = vec![0i16; 256];
        let mut n = 0i16;
        for ms in 0..secs * 1000 {
            let mut target = (ms + 1) * 240_000;
            if every > 0 && ms % every == 0 {
                target += ahead * 240_000;
            }
            cycles = cycles.max(target);
            let mut burst = Vec::new();
            let mut halves = 0;
            while cycles >= t0 && t0 + (made + 1) * half_cycles <= cycles {
                for _ in 0..h {
                    burst.extend_from_slice(&[n, n]);
                    n = n.wrapping_add(1);
                }
                made += 1;
                halves += 1;
            }
            f.push(&burst, halves, cycles);
            while taken + 128 <= ms * 44_100 / 1000 {
                f.pull(&mut out[..256]);
                taken += 128;
            }
        }
        f
    }

    #[test]
    fn steady_and_ahead() {
        for (h, lat) in [(64, 256), (128, 256), (256, 256), (256, 512)] {
            for (ahead, every) in [(0, 0), (40, 997), (3, 7)] {
                let f = run(h, lat, 30, ahead, every);
                assert_eq!((f.underflows, f.dropped, f.gaps), (0, 0, 0), "h {h} L {lat} ahead {ahead}/{every}");
                assert!(f.low >= 0, "h {h} L {lat}: margin {}..{}", f.low, f.high);
            }
        }
    }

    #[test]
    fn a_ramp_comes_out_whole() {
        // a ramp through the interpolator at the device's rate: consecutive outputs step by STEP
        let mut f = Fifo::new(256);
        let mut v: Vec<i16> = Vec::new();
        for i in 0..4096 {
            v.extend_from_slice(&[i as i16, i as i16]);
        }
        f.push(&v, 16, 4096 * CYCLES_PER_FRAME);
        let mut out = vec![0i16; 2 * 2048];
        f.pull(&mut out);
        let a = out[2 * 1000] as f64;
        let b = out[2 * 1999] as f64;
        assert!((b - a - 999.0 * STEP).abs() < 1.0, "{a} {b}");
    }
}
