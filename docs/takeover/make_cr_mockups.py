#!/usr/bin/env python3
"""ChoralRoot's Move takeover (tk1): mock-ups of the screen, pads, steps and knobs, for the owner's approval.
The screen is drawn pixel for pixel with the Move's own font (font5x7, at 2x / 3x / 4x as rfm1cs draws).
    python3 make_cr_mockups.py <rfm1cs font5x7.c> <outdir>"""
import re, sys
from PIL import Image, ImageDraw, ImageFont

FONT_C, OUT = sys.argv[1], sys.argv[2]
rows = re.findall(r"\{\s*(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2})\s*\}",
                  open(FONT_C).read())
GLYPH = {chr(32 + i): [int(c, 16) for c in r] for i, r in enumerate(rows)}


class Scr:
    W, H = 128, 64

    def __init__(self):
        self.px = [[0] * self.W for _ in range(self.H)]

    def set(self, x, y, v=1):
        if 0 <= x < self.W and 0 <= y < self.H:
            self.px[y][x] = v

    def rect(self, x, y, w, h, v=1):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.set(i, j, v)

    def box(self, x, y, w, h, v=1, dotted=False):
        for i in range(x, x + w):
            if not dotted or i % 2 == 0:
                self.set(i, y, v); self.set(i, y + h - 1, v)
        for j in range(y, y + h):
            if not dotted or j % 2 == 0:
                self.set(x, j, v); self.set(x + w - 1, j, v)

    def hline(self, x, y, w, v=1):
        self.rect(x, y, w, 1, v)

    @staticmethod
    def tw(s, k):
        return len(s) * 6 * k - k if s else 0

    def text(self, x, y, s, k=2, v=1):
        for ch in s:
            g = GLYPH.get(ch, GLYPH["?"])
            for col, bits in enumerate(g):
                for row in range(7):
                    if bits >> row & 1:
                        self.rect(x + col * k, y + row * k, k, k, v)
            x += 6 * k
        return x

    def ctext(self, cx, y, s, k=2, v=1):
        self.text(cx - self.tw(s, k) // 2, y, s, k, v)

    def header(self, left, right=""):
        self.rect(0, 0, 128, 16, 1)
        self.text(1, 1, left, 2, 0)
        if right:
            self.text(127 - self.tw(right, 2), 1, right, 2, 0)

    def keyboard(self, x, y, octaves, lit, start=48):
        """2 octaves: white keys 9 px wide, 15 tall; a lit white key gets a block at its foot, a lit black key is hollow"""
        whites = [0, 2, 4, 5, 7, 9, 11]
        n = 7 * octaves
        self.box(x, y, n * 9 + 1, 15)
        for i in range(n):
            o, w = divmod(i, 7)
            note = start + 12 * o + whites[w]
            xx = x + i * 9
            self.rect(xx, y, 1, 15)
            if note in lit:
                self.rect(xx + 2, y + 10, 6, 4)
        for i in range(n):
            o, w = divmod(i, 7)
            if whites[w] in (0, 2, 5, 7, 9):
                note = start + 12 * o + whites[w] + 1
                xx = x + i * 9 + 6
                self.rect(xx, y, 6, 9, 1)
                if note in lit:
                    self.rect(xx + 1, y + 1, 4, 7, 0)

    def image(self, scale=4, on=(236, 236, 236), off=(8, 8, 8)):
        im = Image.new("RGB", (self.W * scale, self.H * scale), off)
        d = ImageDraw.Draw(im)
        for y in range(self.H):
            for x in range(self.W):
                if self.px[y][x]:
                    d.rectangle([x * scale, y * scale, x * scale + scale - 2, y * scale + scale - 2], fill=on)
        return im


# ---- the surface
VIOLET = (196, 58, 255)      # the chord types (FM-1 mode's colour)
ORANGE = (255, 120, 0)       # the extensions (ChoralRoot's extension colour)
YELLOW = (255, 210, 40)      # the tonic
CYAN = (26, 228, 255)        # the chord's own tones on the root pads
WHITE = (255, 255, 255)
PURPLE = (150, 90, 255)      # progressions (the Chord view's purple)
OFF = (22, 22, 26)


def dim(c, f):
    return tuple(int(v * f) for v in c)


try:
    LBL = ImageFont.truetype("/System/Library/Fonts/SFNS.ttf", 13)
    LBLB = ImageFont.truetype("/System/Library/Fonts/SFNS.ttf", 15)
    TITLE = ImageFont.truetype("/System/Library/Fonts/SFNS.ttf", 22)
    NOTE = ImageFont.truetype("/System/Library/Fonts/SFNS.ttf", 14)
except OSError:
    LBL = LBLB = TITLE = NOTE = ImageFont.load_default()


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def surface(title, scr, pads, steps, knobs, buttons, notes, tracks=None):
    """pads: 32 (label, colour) bottom-left first; steps: 16 (label, colour); knobs: 8 (label, colour)"""
    W, H = 1180, 900
    im = Image.new("RGB", (W, H), (38, 38, 42))
    d = ImageDraw.Draw(im)
    d.text((24, 16), title, fill=WHITE, font=TITLE)
    # knobs
    for i, (lab, col) in enumerate(knobs):
        cx = 250 + i * 84
        d.ellipse([cx - 26, 70, cx + 26, 122], fill=(18, 18, 20), outline=col, width=5)
        d.text((cx, 136), lab, fill=WHITE, font=LBL, anchor="mm")
    # the screen
    s = scr.image(4)
    im.paste(s, (300, 160))
    d.rectangle([296, 156, 816, 420], outline=(70, 70, 76), width=3)
    # right column: buttons
    for j, (lab, col) in enumerate(buttons):
        y = 62 + j * 31
        d.rounded_rectangle([880, y, 1150, y + 26], 6, fill=(28, 28, 30), outline=col, width=2)
        d.text((1015, y + 13), lab, fill=WHITE, font=LBL, anchor="mm")
    # track buttons (left of the pads)
    tracks = tracks or [("Track 1: FM-1", (196, 58, 255)), ("Track 2: MIDI", (200, 255, 42)),
                        ("Track 3: Control", (26, 228, 255)), ("Track 4: Seq", (255, 42, 42))]
    for j, (lab, col) in enumerate(tracks):
        y = 448 + j * 76
        d.rounded_rectangle([24, y, 200, y + 60], 8, fill=(28, 28, 30), outline=col, width=3)
        d.text((112, y + 30), lab, fill=WHITE, font=LBL, anchor="mm")
    # pads
    pw, ph, gap = 104, 70, 8
    x0, y0 = 230, 440
    for i, (lab, col) in enumerate(pads):
        r, c = divmod(i, 8)
        x = x0 + c * (pw + gap)
        y = y0 + (3 - r) * (ph + gap)
        d.rounded_rectangle([x, y, x + pw, y + ph], 8, fill=col)
        fg = (0, 0, 0) if lum(col) > 110 else (235, 235, 235)
        for k, part in enumerate(lab.split("\n")):
            d.text((x + pw // 2, y + ph // 2 + (k - (len(lab.split("\n")) - 1) / 2) * 17), part, fill=fg,
                   font=LBLB if k == 0 else LBL, anchor="mm")
    # steps
    sw = 51
    for i, (lab, col) in enumerate(steps):
        x = x0 + i * (sw + 5)
        y = y0 + 4 * (ph + gap) + 10
        d.rounded_rectangle([x, y, x + sw, y + 34], 6, fill=col)
        fg = (0, 0, 0) if lum(col) > 110 else (220, 220, 220)
        d.text((x + sw // 2, y + 17), lab, fill=fg, font=LBL, anchor="mm")
    # notes
    for k, line in enumerate(notes):
        d.text((24, 820 + k * 20), line, fill=(205, 205, 210), font=NOTE)
    return im


NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
MAJOR = [0, 2, 4, 5, 7, 9, 11]
ROMAN = ["I", "ii", "iii", "IV", "V", "vi", "vii"]

CHORD_ROW = [("DIM", VIOLET), ("MIN", VIOLET), ("MAJ", VIOLET), ("SUS", VIOLET),
             ("6", ORANGE), ("m7", ORANGE), ("M7", ORANGE), ("9", ORANGE)]


def chord_row(held=(), locked=()):
    out = []
    for i, (lab, col) in enumerate(CHORD_ROW):
        f = 1.0 if i in held else 0.75 if i in locked else 0.28
        out.append((lab + ("\nLOCK" if i in locked else ""), dim(col, f) if f < 1 else WHITE if i in held else col))
    return out


def scale_pads(pressed=None, chord_pcs=(), held=(), locked=()):
    """rows 1..3: degrees 1..8 of C major, an octave a row (C3, C4, C5)"""
    pads = []
    for r in range(3):
        for c in range(8):
            deg = c % 7
            oct_ = 3 + r + (1 if c == 7 else 0)
            pc = MAJOR[deg]
            lab = ROMAN[deg] if c < 7 else "I"
            name = NOTE_NAMES[pc] + str(oct_)
            if (r, c) == pressed:
                col = WHITE
            elif deg == 0:
                col = YELLOW
            elif pc in chord_pcs:
                col = dim(CYAN, 0.75)
            else:
                col = dim(WHITE, 0.32)
            pads.append((lab + "\n" + name, col))
    return pads + chord_row(held, locked)


def chrom_pads(pressed=None):
    """rows a 4th apart (Move's chromatic), from C3; the key's notes lit, the tonic yellow"""
    pads = []
    for r in range(3):
        for c in range(8):
            n = 48 + r * 5 + c
            pc = n % 12
            name = NOTE_NAMES[pc] + str(n // 12 - 1)
            if (r, c) == pressed:
                col = WHITE
            elif pc == 0:
                col = YELLOW
            elif pc in MAJOR:
                col = dim(WHITE, 0.32)
            else:
                col = (12, 12, 14)
            pads.append((name, col))
    return pads + chord_row()


PROG = ["Em9", "Cmaj9", "G6/B", "Am11", "Em9", "D/F#", "Cmaj7", "B7#5"]


def prog_pads(pressed=None):
    pads = []
    for i in range(24):
        if i < len(PROG):
            col = WHITE if i == pressed else dim(PURPLE, 0.85)
            pads.append((str(i + 1) + "\n" + PROG[i], col))
        else:
            pads.append((str(i + 1), (14, 14, 18)))
    return pads + chord_row()


def prog_steps(sel=None):
    names = ["Pop", "Jazz", "Neo", "Gosp", "Epic", "Dor", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"]
    out = []
    for i, n in enumerate(names):
        if i == sel:
            out.append((n, PURPLE))
        elif i < 6:
            out.append((n, dim(PURPLE, 0.35)))
        else:
            out.append((n, (30, 30, 34)))
    return out


KNOBS_MAIN = [("Voicing", ORANGE), ("Perform", VIOLET), ("Rate", VIOLET), ("Direction", VIOLET),
              ("Bass reg.", (255, 90, 30)), ("Key", YELLOW), ("Scale", YELLOW), ("Transpose", YELLOW)]
KNOBS_SHIFT = [("Sound", WHITE), ("Bass sound", (255, 90, 30)), ("Gate", VIOLET), ("Swing", VIOLET),
               ("Bass level", (255, 90, 30)), ("Velocity", PURPLE), ("Humanize", PURPLE), ("Tempo", WHITE)]
BUTTONS = [("Play: loop play / stop", (0, 200, 0)), ("Rec: loop record", (220, 0, 0)),
           ("Loop: LATCH", WHITE), ("Copy: LOCK chord keys", WHITE), ("Mute: bass on / off", WHITE),
           ("- / +: octave -2..+2", WHITE), ("Left / Right: prog page", WHITE), ("Menu: Scale / Chromatic", WHITE),
           ("Delete: PANIC", WHITE), ("Shift: page 2 of knobs", WHITE), ("Wheel: sound; Shift: engine", WHITE),
           ("Volume: MASTER (RemoteFM1)", (120, 120, 120))]


def scr_chord(name="Dm7", roman="ii7", lit=(50, 53, 57, 60), key="C MAJ", perf="ARP", voicing=None):
    s = Scr()
    s.header(key, perf)
    k = 4 if Scr.tw(name, 4) + (Scr.tw(roman, 2) + 4 if roman else 0) <= 126 else 3
    s.text(1, 18, name, k)
    if roman:
        s.text(127 - Scr.tw(roman, 2), 18, roman, 2)
    if voicing:
        s.text(127 - Scr.tw(voicing, 2), 33, voicing, 2)
    s.keyboard(1, 48, 2, set(lit), 48)
    return s


def scr_popup(title, value, bar=None):
    s = Scr()
    s.ctext(64, 2, title, 2)
    k = 4 if Scr.tw(value, 4) <= 126 else 3
    s.ctext(64, 24, value, k)
    if bar is not None:
        s.box(8, 58, 112, 5)
        s.rect(9, 59, int(110 * bar), 3)
    return s


def scr_shift():
    s = Scr()
    names = ["Sound", "Bass", "Gate", "Swing", "BasLv", "Velo", "Human", "Tempo"]
    for i, a in enumerate(names):
        col, row = divmod(i, 4)
        s.text(1 + col * 66, 1 + row * 16, a, 2)
    s.rect(63, 0, 1, 64)
    return s


def scr_prog():
    s = Scr()
    s.header("NEO SOUL", "5")
    s.ctext(64, 19, "Am11", 4)
    s.text(1, 50, "keys 1-24", 2)
    return s


def scr_arp():
    s = Scr()
    s.header("C MAJ", "ARP")
    s.text(1, 19, "Dm7", 3)
    s.text(127 - Scr.tw("1/8", 2), 19, "1/8", 2)
    xs = [10, 40, 70, 100]
    names = ["D", "F", "A", "C"]
    for i, (x, n) in enumerate(zip(xs, names)):
        if i == 2:
            s.rect(x - 4, 38, 22, 22, 1)
            s.text(x + 2, 42, n, 2, 0)
        else:
            s.text(x + 2, 50, n, 2)
    s.hline(0, 61, 128)
    return s


def main():
    import os
    os.makedirs(OUT, exist_ok=True)
    common_steps = prog_steps()
    sheets = [
        ("1-chords-scale", "1. CHORDS, Scale layout (Key Mode): D pressed with m7 held = Dm7",
         scr_chord(voicing="+2"), scale_pads(pressed=(1, 1), chord_pcs=(2, 5, 9, 0), held=(5,)), common_steps, KNOBS_MAIN, BUTTONS,
         ["Top row: the chord keys (types violet, extensions orange); held = white. Rows 1-3: the key's degrees, an octave a row;",
          "a pad plays the scale's chord (Key Mode), a chord key held overrides it. Tonic yellow, the chord's tones cyan.",
          "Screen: the chord big, its numeral, the voiced notes on a 3-octave keyboard. Steps: the 16 progressions."]),
        ("2-chords-chromatic", "2. CHORDS, Chromatic layout (Menu): rows a 4th apart, as Move's chromatic",
         scr_chord(name="Ebsus", roman="", lit=(51, 56, 58), perf="STRUM"), chrom_pads(pressed=(0, 3)), common_steps,
         KNOBS_MAIN, BUTTONS,
         ["For chords outside the key. The key's notes are lit, the tonic yellow, the rest dark.",
          "The chord keys stay on the top row: hold SUS, press Eb."]),
        ("3-prog", "3. PROG: step 3 (Neo Soul) tapped; the 24 root pads are its chords, Left / Right the next 24",
         scr_prog(), prog_pads(pressed=3), prog_steps(sel=2), KNOBS_MAIN, BUTTONS,
         ["A step picks a progression and turns the root pads into its chords; the lit step tapped again: back to Scale.",
          "The chord keys still override (a chord key held + a pad: your own chord on that pad's root)."]),
        ("4-knob", "4. A knob touched or turned: its name, then its value, full screen (RemoteFM1's rule)",
         scr_popup("Voicing", "+2", 14 / 24), scale_pads(), common_steps, KNOBS_MAIN, BUTTONS,
         ["Touch: the name and value at once; turn: the value moves; let go: gone after 1 s.",
          "Said by the screen reader as \"Voicing: plus 2\"."]),
        ("5-shift", "5. Shift held: the knobs' second page", scr_shift(), scale_pads(), common_steps, KNOBS_SHIFT, BUTTONS,
         ["Sound and bass sound (with the wheel: the sounds by name), the performance's gate and swing,",
          "the bass level, the progression's velocity and humanize, the tempo."]),
        ("6-arp", "6. An arpeggio running: the sounding note lifted onto a block, as the FM-1's Chord view",
         scr_arp(), scale_pads(pressed=(1, 1), chord_pcs=(2, 5, 9, 0), held=(5,)), common_steps, KNOBS_MAIN, BUTTONS,
         ["Perform (knob 2): Off, Strum, Strum 2, Slop, Arp, Arp 2, Pattern, Harp; Rate and Direction its first two values."]),
    ]
    for fn, title, scr, pads, steps, knobs, buttons, notes in sheets:
        surface(title, scr, pads, steps, knobs, buttons, notes).save(os.path.join(OUT, f"cr-takeover-{fn}.png"))
        scr.image(4).save(os.path.join(OUT, f"cr-screen-{fn}.png"))
    # one sheet of the six screens alone
    sh = Image.new("RGB", (3 * 520 + 20, 2 * 264 + 20), (38, 38, 42))
    for i, (fn, *_r) in enumerate(sheets):
        sh.paste(Image.open(os.path.join(OUT, f"cr-screen-{fn}.png")), (10 + (i % 3) * 520, 10 + (i // 3) * 264))
    sh.save(os.path.join(OUT, "cr-screens.png"))


main()
