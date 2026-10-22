#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The PIGEON core's test bank (tests/fixtures/pigeon), made by PIGEON's own tools (cores/pigeon/tools/ko_bank.py):

  python3 tests/fixtures/pigeon/make_bank.py        (from the FM1VST tree; needs libmp3lame, as ko_bank.py does)

  bank.kob   ko_bank.py build: Song 1 is 4 s of plucked tones (24 notes: its onsets give the white and black keys their
             chops), indexed mono MP3 at 32 kbps (the libmad path); the kit is all sixteen pads, IMA ADPCM, 80 ms each
             (kicks, noise snares, hats and clicks at four pitches). The sounds are synthesised here (sines and seeded noise): no third-party audio.
  bank.syx   ko_bank.py upload's frames, F0 7D 46 4C <cmd> .. F7 one after another, as it sends them to a unit (90
             negotiate, 91 begin, 92 the data then the header, 93 commit), recorded with a link that answers as the
             firmware does. The conformance file's `sysex` sends them (tests/script_runner.h).
Both are committed: the tests read them, nothing is generated at build time."""
import math
import random
import struct
import sys
import tempfile
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parents[2] / "cores" / "pigeon" / "tools"
sys.path.insert(0, str(TOOLS))
import ko_bank  # noqa: E402  (PIGEON v0.4.1's own bank builder and uploader)

_find = ko_bank.ctypes.util.find_library
_brew = Path("/opt/homebrew/lib/libmp3lame.dylib")      # (an Apple-silicon Mac: /usr/local's may be x86_64 only)
ko_bank.ctypes.util.find_library = lambda n: str(_brew) if n == "mp3lame" and _brew.exists() else _find(n)

RATE = 22050


def write_wav(path, x):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", max(-32767, min(32767, int(v * 32767)))) for v in x))


def song():
    rng = random.Random(1)
    n, out = 4 * RATE, [0.0] * (4 * RATE)
    for k in range(24):                                   # a note every 1/6 s, a fresh pitch each
        f = 220.0 * 2 ** (rng.randrange(24) / 12)
        t0 = k * n // 24
        for i in range(n // 24):
            out[t0 + i] += 0.7 * math.exp(-i / (0.05 * RATE)) * math.sin(2 * math.pi * f * i / RATE)
    return out


def pad(k):
    rng = random.Random(k)
    n, kind, up = RATE * 8 // 100, k % 4, 1 + k // 4      # 80 ms; four kinds, four pitches
    if kind == 0:                                         # kick: a falling sine
        return [0.9 * math.exp(-i / 800) * math.sin(2 * math.pi * up * (50 + 120 * math.exp(-i / 400)) * i / RATE)
                for i in range(n)]
    if kind == 1:                                         # snare: noise and a tone
        return [math.exp(-i / 600) * (0.6 * rng.uniform(-1, 1) + 0.3 * math.sin(2 * math.pi * 190 * up * i / RATE))
                for i in range(n)]
    if kind == 2:                                         # hat: short noise
        return [0.5 * math.exp(-i / (300 * up)) * rng.uniform(-1, 1) for i in range(n)]
    return [0.8 * math.exp(-i / 40) * (1 if (i // (8 * up)) % 2 else -1) for i in range(n)]   # click


class RecordingLink:
    """ko_bank.upload's link: each request recorded as its SysEx frame, answered as editor.c's ko_bank_handle does"""
    def __init__(self):
        self.frames = []

    def req(self, cmd, args, timeout=3.0):
        self.frames.append(bytes([0xF0] + ko_bank.HDR_BYTES + [cmd] + list(args) + [0xF7]))
        if cmd == 90:                                     # 4B, two songs (3), capacity, head, no bank yet, codecs 3
            return [0x4B, 3] + ko_bank.u21(ko_bank.SIZE) + ko_bank.u21(ko_bank.HEAD) + [0, 3]
        return [0]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        write_wav(t / "song.wav", song())
        drums = []
        for k in range(16):
            drums.append(t / f"pad{k}.wav")
            write_wav(drums[-1], pad(k))
        image = ko_bank.build(str(t / "song.wav"), [str(d) for d in drums], codec="mp3", bitrate=32,
                              name="FM1VST")
    (HERE / "bank.kob").write_bytes(image)
    ko_bank.HDR_BYTES = [0x7D, 0x46, 0x4C]                # (fm1_sample_upload.HDR: the FL protocol's header)
    link = RecordingLink()
    ko_bank.upload(link, image)
    (HERE / "bank.syx").write_bytes(b"".join(link.frames))
    f = ko_bank.validate(image)
    print(f"bank.kob: {len(image)} bytes (used {f[2]}), {f[16]} chops, {f[11]} pads; "
          f"bank.syx: {len(link.frames)} frames, {sum(map(len, link.frames))} bytes")


if __name__ == "__main__":
    main()
