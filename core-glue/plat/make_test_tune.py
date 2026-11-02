#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The SiDJ core's run test tune: a tiny PSID written for FM1VST's test (no one's music: a C major arpeggio on voice 1,
pulse, over a held C2 on voice 2, triangle), and the SysEx that puts it in the song store and plays it, as the songs
page (web/site/fm1songs.js) and tools/fm1_sid.py speak songs.c's transfer: F0 7D 53 cmd pack7(payload) F7.

  python3 core-glue/plat/make_test_tune.py   ->  core-glue/plat/sidj_test_tune.syx
"""
import hashlib
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOAD = 0x1000
PAL_HZ = 985248.0


def freq(hz):
    return round(hz * 16777216.0 / PAL_HZ)


def assemble():
    """the 6502 code, hand-assembled (two passes for the labels)"""
    out, labels, fix = bytearray(), {}, []
    pc = lambda: LOAD + len(out)                                       # noqa: E731

    def op(*b):
        out.extend(b)

    def abs_(opc, addr):
        if isinstance(addr, str):
            fix.append((len(out) + 1, addr, "abs"))
            addr = 0
        op(opc, addr & 0xFF, addr >> 8)

    def bpl(lbl):
        fix.append((len(out) + 1, lbl, "rel"))
        op(0x10, 0)

    def label(n):
        labels[n] = pc()

    abs_(0x4C, "init")                                                 # $1000 JMP init
    abs_(0x4C, "play")                                                 # $1003 JMP play
    label("init")
    op(0xA2, 0x18, 0xA9, 0x00)                                         # LDX #$18, LDA #0
    label("clr")
    abs_(0x9D, 0xD400)                                                 # STA $D400,X
    op(0xCA)                                                           # DEX
    bpl("clr")
    for reg, v in ((0xD418, 0x0F), (0xD402, 0x00), (0xD403, 0x08), (0xD405, 0x09), (0xD406, 0xA4),
                   (0xD40C, 0x00), (0xD40D, 0xF0), (0xD407, freq(65.41) & 0xFF), (0xD408, freq(65.41) >> 8),
                   (0xD40B, 0x11)):
        op(0xA9, v)                                                    # LDA #v
        abs_(0x8D, reg)                                                # STA reg
    op(0xA9, 0x00)
    abs_(0x8D, "cnt")
    abs_(0x8D, "idx")
    op(0x60)                                                           # RTS
    label("play")
    abs_(0xCE, "cnt")                                                  # DEC cnt
    bpl("done")
    op(0xA9, 0x07)
    abs_(0x8D, "cnt")                                                  # a note every 8 frames
    abs_(0xAE, "idx")                                                  # LDX idx
    abs_(0xBD, "lo")
    abs_(0x8D, 0xD400)                                                 # LDA lo,X  STA $D400
    abs_(0xBD, "hi")
    abs_(0x8D, 0xD401)
    op(0xA9, 0x40)
    abs_(0x8D, 0xD404)                                                 # gate off ..
    op(0xA9, 0x41)
    abs_(0x8D, 0xD404)                                                 # .. and on: pulse
    op(0xE8, 0x8A, 0x29, 0x07)                                         # INX, TXA, AND #7
    abs_(0x8D, "idx")
    label("done")
    op(0x60)
    notes = [261.63, 329.63, 392.00, 523.25, 392.00, 329.63, 261.63, 196.00]
    label("lo")
    op(*[freq(h) & 0xFF for h in notes])
    label("hi")
    op(*[freq(h) >> 8 for h in notes])
    label("cnt")
    op(0)
    label("idx")
    op(0)
    for at, lbl, kind in fix:
        a = labels[lbl]
        if kind == "abs":
            out[at], out[at + 1] = a & 0xFF, a >> 8
        else:
            d = a - (LOAD + at + 1)
            assert -128 <= d < 128
            out[at] = d & 0xFF
    return bytes(out)


def psid(code):
    s = lambda t: t.encode("latin-1").ljust(32, b"\0")                  # noqa: E731
    hdr = (b"PSID" + struct.pack(">HHHHHHHI", 2, 0x7C, 0, LOAD, LOAD + 3, 1, 1, 0)
           + s("SiDJ core test tune") + s("FM1VST") + s("2026 FM1VST (GPL-3.0)")
           + struct.pack(">HBBH", 0x0014, 0, 0, 0))                     # PAL, 6581
    assert len(hdr) == 0x7C
    return hdr + struct.pack("<H", LOAD) + code


def pack7(b):
    o = bytearray()
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        o.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        o.extend(x & 0x7F for x in g)
    return bytes(o)


def crc16(d):
    c = 0
    for x in d:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def msg(cmd, payload=b""):
    return bytes([0xF0, 0x7D, 0x53, cmd]) + pack7(bytes(payload)) + b"\xF7"


def main():
    f = psid(assemble())
    lens = [600]                                                        # 60 s, in deciseconds
    syx = msg(3, struct.pack("<IB", len(f), len(lens)) + hashlib.md5(f).digest()
              + b"".join(struct.pack("<H", x) for x in lens))          # PUT_BEGIN
    for off in range(0, len(f), 256):
        syx += msg(4, struct.pack("<I", off)[:3] + f[off:off + 256])   # PUT_DATA
    syx += msg(5, struct.pack("<H", crc16(f)))                         # PUT_END
    syx += msg(9, bytes([0, 0, 1]))                                    # PLAY entry 0, subsong 1
    (HERE / "sidj_test_tune.syx").write_bytes(syx)
    print(f"the tune: {len(f)} B, sidj_test_tune.syx: {len(syx)} B")


if __name__ == "__main__":
    main()
