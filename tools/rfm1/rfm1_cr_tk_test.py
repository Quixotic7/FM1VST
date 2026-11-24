#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ChoralRoot's Move takeover, "ChoralRoot" (docs/RFM1-TAKEOVER.md tk1; the firmware's firmware/src/cr_move.c, built into
choralroot-<v>.rfm1 by core-glue/choralroot/core_choralroot.cmake) through rfm1_run --tk: the record and the manifest,
then scenarios as a Move host drives it (pads, steps, knobs, buttons), each checked by its words (the screen reader's
say), its LEDs and its screen. The screens are compared pixel for pixel with the approved design's own drawing
functions (make_cr_v2.py / make_cr_mockups.py: header, text at 2x / 4x, the 16-step strip, the boxes), redrawn here
with the firmware's font (cr_move_font.h, the Move's 5 x 7). Exits 1 on a failure.

    python3 tools/rfm1/rfm1_cr_tk_test.py --run build/rfm1_run --file build/rfm1/choralroot-0.42.rfm1 [--png DIR]

--png DIR: the main screens as PNG (4x, the mock-ups' look) into DIR, for a side-by-side with the design (Pillow).
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
CHECK = os.path.join(TOOLS, "rfm1_check.py")
FONT_H = os.path.join(ROOT, "cores", "ChoralRootFM1", "firmware", "src", "cr_move_font.h")
FAILS = []
A = None
PNGS = {}


def ok(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)
    return cond


# ---- the design's screen (make_cr_mockups.py Scr), with the firmware's font
def font():
    src = open(FONT_H).read()
    rows = re.findall(r"\{\s*(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2}),"
                      r"\s*(0x[0-9A-Fa-f]{2})\s*\}", src)
    return {chr(32 + i): [int(c, 16) for c in r] for i, r in enumerate(rows)}


GLYPH = None


class Scr:
    W, H = 128, 64

    def __init__(self):
        self.px = [[0] * self.W for _ in range(self.H)]
        self.wild = []                             # big text (AF_CRB, cut to one bit): only where, not each pixel

    def big(self, x0, y0, x1, y1, inv=False):
        self.wild.append((x0, y0, x1, y1, inv))

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
                self.set(i, y, v)
                self.set(i, y + h - 1, v)
        for j in range(y, y + h):
            if not dotted or j % 2 == 0:
                self.set(x, j, v)
                self.set(x + w - 1, j, v)

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

    def rows(self):
        return ["".join(str(v) for v in r) for r in self.px]


# the design's screens (proposal 3 / 4, docs/takeover/make_cr_v3.py, make_cr_v4.py): labels, tags, boxes and dashes
# exact; big text (cr_move.c bt: ChoralRoot's bold) a region that must hold ink
def lbl(s, x, y, t):
    s.text(x, y, t, 2)


def rlbl(s, y, t):
    s.text(127 - Scr.tw(t, 2), y, t, 2)


def clbl(s, y, t):
    k = 2 if Scr.tw(t, 2) <= 126 else 1
    s.ctext(64, y, t, k)


def tag(s, x, w, t):
    s.rect(x, 0, w, 14)
    s.text(x + (w - Scr.tw(t, 2)) // 2, 0, t, 2, 0)


def dashes(s, n, cur, y=58, w=10, gap=4):          # cr_move.c px_dashes
    if n < 2 or n > 24:
        return
    while w > 2 and n * w + (n - 1) * gap > 124:
        w -= 1
        if gap > 2 and w < 8:
            gap -= 1
    x = 64 - (n * w + (n - 1) * gap) // 2
    for i in range(n):
        (s.rect if i == cur else s.box)(x + i * (w + gap), y, w, 3)


def bigc(s, y, h, desc=True):                      # centred big text, caps' top at y (descenders: 0.3 h)
    s.big(0, y - 1, 128, y + h + (h * 3 // 10 if desc else 2))


def scr_home(chord=True, notes="", x=4, y=6, h=34, label=None, lock=False):
    s = Scr()
    if label:
        lbl(s, 0, 0, label)
    if chord:
        s.big(x, y - 1, 128, y + h + 2)
        if notes:
            s.text(x, 50, notes, 2)
    else:
        s.rect(52, 28, 24, 5)
    if lock:
        tag(s, 98, 30, "LK")
    return s


def scr_saved(n):
    s = Scr()
    s.big(2, 11, 84, 46)
    s.rect(84, 8, 44, 48)
    s.big(85, 13, 127, 34, inv=True)
    s.text(94, 40, "ok", 2, 0)
    return s


def scr_step(n, rat="", ln="", chance=""):
    s = Scr()
    lbl(s, 0, 0, "Step %d" % n)
    if rat:
        rlbl(s, 0, rat)
    s.big(4, 17, 128, 48)
    if ln:
        lbl(s, 0, 50, ln)
    if chance:
        rlbl(s, 50, chance)
    return s


def scr_slots(cur, full, bpm=120):
    s = Scr()
    for i in range(10):
        x = i * 13
        if i == cur:
            s.rect(x, 0, 11, 11)
        elif i in full:
            s.box(x, 0, 11, 11)
            s.rect(x + 3, 3, 5, 5)
        else:
            s.box(x, 0, 11, 11, dotted=True)
    bigc(s, 18, 22)
    clbl(s, 50, "%d bpm" % bpm)
    return s


def scr_layout(cur=0, bank=1):
    s = Scr()
    rlbl(s, 0, "B%d" % bank)
    bigc(s, 16, 30)
    dashes(s, 6, cur, 58, 18, 6)
    return s


SC = ["KEY", "LOCK", "LATCH", "PERF", "B.PRF", "FX", "BASS", "METRO"]


def scr_shortcuts(on):                             # proposal 4, C: Shift held
    s = Scr()
    for i, lab in enumerate(SC):
        x, y = (i % 2) * 66, (i // 2) * 16
        if on[i]:
            s.rect(x, y, 62, 15)
            s.text(x + 1, y + 1, lab, 2, 0)
        else:
            s.text(x + 1, y + 1, lab, 2)
    return s


def scr_layer(name, on, at, of):                   # proposal 4, D: a settings layer
    s = Scr()
    lbl(s, 0, 0, name)
    if on:
        tag(s, 98, 30, "on")
    else:
        rlbl(s, 0, "off")
    bigc(s, 18, 30)
    dashes(s, of, at, 58, 18 if of <= 4 else 10, 6 if of <= 4 else 4)
    return s


def scr_popup(title, pm=None):                     # a knob: its name, the value big, a bar (pm: permille)
    s = Scr()
    clbl(s, 0, title)
    bigc(s, 18, 30)
    if pm is not None:
        s.box(10, 58, 108, 5)
        s.rect(11, 59, 106 * pm // 1000, 3)
    return s


def ink(run, x0, y0, x1, y1, v="1"):
    return any(run.screen[y][x] == v for y in range(y0, min(y1, len(run.screen))) for x in range(x0, x1))


# ---- a run
class Run:
    def __init__(self, evs, end, extra=()):
        a = [A.run, "--settle", str(end - 200), "--seconds", "0", "--keys", "none", "--min-rms", "0", "--tk",
             "--tk-speech", "--tk-screen", "-", "--tk-leds"] + list(extra)
        for e in evs:
            a += ["--tk-ev", e]
        r = subprocess.run(a + [A.file], capture_output=True, text=True, errors="replace")
        self.rc, self.out, self.err = r.returncode, r.stdout, r.stderr
        L = self.out.splitlines()
        self.lines = L
        i = L.index("P1") if "P1" in L else -1
        self.screen = L[i + 2:i + 66] if i >= 0 else []
        self.leds, self.led_at = {}, []
        for l in L:
            m = re.match(r"tk-led@(\d+): (\d+) (\d+) (\d+) (\d+)", l)
            if m:
                self.leds[int(m.group(2))] = tuple(int(x) for x in m.group(3, 4, 5))
                self.led_at.append((int(m.group(1)), int(m.group(2)), tuple(int(x) for x in m.group(3, 4, 5))))
        for l in L:
            m = re.match(r"tk-led-end: (\d+) (\d+) (\d+) (\d+)", l)
            if m:
                self.leds[int(m.group(1))] = tuple(int(x) for x in m.group(2, 3, 4))
        self.says = [re.sub(r'^tk-say@\d+: interrupt \d ', "", l).strip('"') for l in L if l.startswith("tk-say")]

    def led(self, i):
        return self.leds.get(i, (0, 0, 0))

    def led_when(self, i, t):                      # LED i as it was at device ms t
        v = (0, 0, 0)
        for at, j, c in self.led_at:
            if j == i and at <= t:
                v = c
        return v


def rms(evs, settle, secs):                        # the chord part's level from settle ms on, secs long
    a = [A.run, "--settle", str(settle), "--seconds", str(secs), "--keys", "none", "--min-rms", "0", "--tk",
         "--tk-screen", "-"]
    for e in evs:
        a += ["--tk-ev", e]
    r = subprocess.run(a + [A.file], capture_output=True, text=True, errors="replace")
    m = re.search(r"chord rms ([0-9.]+)", r.stdout + r.stderr)
    return float(m.group(1)) if m else -1.0


def same(run, exp, what, png=None):
    got = run.screen
    want = exp.rows()
    mask = [[False] * 128 for _ in range(64)]
    for x0, y0, x1, y1, inv in exp.wild:
        for y in range(max(0, y0), min(64, y1)):
            for x in range(max(0, x0), min(128, x1)):
                mask[y][x] = True
    bad = [y for y in range(64) if len(got) != 64 or len(got[y]) < 128 or
           any(got[y][x] != want[y][x] and not mask[y][x] for x in range(128))]
    inked = len(got) == 64 and all(ink(run, x0, y0, x1, y1, "0" if inv else "1") for x0, y0, x1, y1, inv in exp.wild)
    good = ok(not bad and inked, "screen: " + what)
    if not good:
        print("  big text inked: %s" % inked)
        for y in bad[:64]:
            if y < len(got):
                print("  row %2d got  %s\n         want %s" % (y, got[y], want[y]))
    if png:
        PNGS[png] = got
    return good


def dim(c, f):
    return tuple(int(v * f) for v in c)


VIOLET_D, ORANGE_D = (0x36, 0x10, 0x47), (0x47, 0x21, 0x00)
VIOLET, ORANGE = (196, 58, 255), (255, 120, 0)
KEY_D, YELLOW, WHITE = (0x51, 0x51, 0x51), (0xFF, 0xD2, 0x28), (255, 255, 255)
BASS, GREEN, RED, CYAN = (255, 90, 30), (40, 220, 90), (255, 40, 40), (0x13, 0xAB, 0xBF)
MEM, SEQ, HELD, PROG = (0x13, 0xAB, 0xBF), (40, 110, 255), (255, 120, 0), (150, 90, 255)


def pct(c, p):                                     # cr_move.c led_dim: c * p / 100, integer
    return tuple(v * p // 100 for v in c)


def pad(r, c):                                     # the design's row r (1 top .. 4 bottom), column c (1..8)
    return (4 - r) * 8 + (c - 1)


def custom_rfm1(b):
    i = b.index(b"\x04rfm1")
    j = b.index(b"{", i)
    depth = 0
    for k in range(j, len(b)):
        depth += {ord("{"): 1, ord("}"): -1}.get(b[k], 0)
        if depth == 0:
            return json.loads(b[j:k + 1])
    return None


KEY_ON = "knob:5@150:48"                           # Key + 12 semitones (4 ticks a semitone): the tonic stays C, Key Mode on ("C MAJ")
DM7 = ["pad:25@200+700", "pad:17@205+700", "pad:1@300+500"]   # MIN + m7 held, D: Dm7


def main():
    global A, GLYPH
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--file", required=True)
    ap.add_argument("--png")
    A = ap.parse_args()
    GLYPH = font()
    ok(len(GLYPH) == 95, "the firmware's font: 95 glyphs (cr_move_font.h)")

    # ---- the record, the manifest, rfm1_check
    r = subprocess.run([A.run, "--tk-info", A.file], capture_output=True, text=True)
    info = json.loads(r.stdout) if r.returncode == 0 else {}
    ok(info.get("version") == 1 and info.get("size") == 1468 and info.get("name") == "ChoralRoot",
       "the record: version 1, size 1468, name ChoralRoot (%s)" % r.stdout.strip())
    ok(info.get("claims") == 0x5F, "claims 0x5F: all but TRACKS and VOLUME")
    m = custom_rfm1(open(A.file, "rb").read())
    ok("tk1" in m.get("wants", []) and m.get("takeover") == {
        "name": "ChoralRoot", "claims": ["screen", "pads", "steps", "knobs", "jog", "buttons"]},
       "the manifest: wants tk1, takeover %s" % m.get("takeover"))
    c = subprocess.run([sys.executable, CHECK, "--run", A.run, A.file], capture_output=True, text=True)
    ok(c.returncode == 0 and 'takeover (tk1, "ChoralRoot" claims 0x5f)' in c.stdout, "rfm1_check --run accepts it")

    # ---- entering: the home view, the Keys layout's LEDs
    e = Run([], 600)
    ok(e.rc == 0 and "tk-state@100: on claims 0x5f speech" in e.lines, "entering: shown with claims 0x5f")
    same(e, scr_home(chord=False), "home on entering: no chord yet, a dash (proposal 3)")
    ok(e.led(pad(4, 1)) == YELLOW and e.led(pad(4, 8)) == YELLOW and e.led(pad(4, 2)) == KEY_D and
       e.led(pad(3, 2)) == (16, 16, 18) and e.led(pad(3, 1)) == (0, 0, 0) and e.led(pad(3, 4)) == (0, 0, 0),
       "Keys layout: the tonic (C) yellow, the key's notes grey, the others dim, no key above E / B")
    kd = Run(["knob:5@150:8"], 600)
    ok(kd.led(pad(4, 2)) == YELLOW and kd.led(pad(3, 2)) == KEY_D and kd.led(pad(4, 1)) == (16, 16, 18) and
       kd.led(pad(4, 4)) == (16, 16, 18), "Keys in D major: D yellow, C# grey (in the key), C and F dim")
    ok(all(e.led(pad(1, c)) == VIOLET_D for c in range(1, 5)) and all(e.led(pad(2, c)) == ORANGE_D for c in range(1, 5)),
       "the chord keys: types violet, extensions orange (dim)")
    ok(all(e.led(pad(r, c)) == pct(MEM, 18) for r in (1, 2) for c in range(5, 9)), "eight empty memories, dim cyan")
    ok(all(e.led(32 + i) == (24, 24, 30) for i in range(16)), "the steps: 16 dark (an empty slot: a bar)")
    ok(all(e.led(76 + i) == (0, 0, 0) for i in range(16)), "the step icons: off for a loop of one page")
    ok(e.led(55) == ORANGE and e.led(56) == BASS and e.led(57) == VIOLET and e.led(58) == GREEN and e.led(59) == YELLOW
       and e.led(62) == WHITE, "the knob rings: Voicing, Bass reg., Perf amount, FX send, Key .. Tempo")

    # ---- a chord from the chord keys and a key
    c1 = Run([KEY_ON] + DM7, 2000)
    ok("Chord D minor seven" in c1.says, "MIN + m7 + D: said \"Chord D minor seven\" (%s)" % c1.says)
    same(c1, scr_home(notes="D F A C", x=30, y=15, h=32, label="Key C"), "home: Dm7 in C major (Key C top left, the chord big, its notes)", png="1-home-dm7")
    ok(c1.led_when(pad(1, 2), 400) == WHITE and c1.led_when(pad(2, 2), 400) == WHITE and
       c1.led_when(pad(4, 2), 400) == WHITE, "MIN, m7 and D lit white while held")
    ok(c1.led(pad(1, 2)) == VIOLET_D and c1.led(pad(4, 2)) == KEY_D, ".. and back when let go")
    sus = Run(["pad:26@200+400", "pad:27@200+400", "pad:1@300+200"], 800)
    ok(any(s.startswith("Chord D sus") for s in sus.says), "MIN + MAJ held: SUS (%s)" % sus.says)

    # ---- a memory: saved, played, cleared
    mm = Run(DM7 + ["pad:21@500+100"], 1400)
    ok("Memory 6, D minor seven" in mm.says, "a key held + memory 6: saved, said (%s)" % mm.says)
    ok(mm.led_when(pad(2, 6), 500) == WHITE and mm.led(pad(2, 6)) == MEM, "memory 6 flashes white, then cyan")
    mm2 = Run(DM7 + ["pad:21@500+100"], 900)
    same(mm2, scr_saved(6), "the saved popup: the chord, a lit block M6 ok", png="2-memory-saved")
    mp = Run(DM7 + ["pad:21@500+100", "pad:21@2500+300", "button:delete@3500+300", "pad:21@3600+100"], 4200)
    ok(mp.says.count("Memory 6, D minor seven") == 2, "memory 6 tapped alone: plays it, said (%s)" % mp.says)
    ok("Memory 6, cleared" in mp.says and mp.led(pad(2, 6)) == pct(MEM, 18), "Delete + memory 6: cleared, dim")
    ok(not any("PANIC" in s for s in mp.says), "Delete used as the clear modifier: no PANIC")

    # ---- the Menu picker and the layouts
    mn = Run(["button:menu@200+600"], 700)
    same(mn, scr_layout(), "Menu held: the layout big, a dash each, B1", png="5-layout-picker")
    ok(mn.led(pad(1, 1)) == WHITE and mn.led(pad(1, 2)) == pct(WHITE, 45) and mn.led(pad(1, 3)) == pct(WHITE, 15) and
       mn.led(pad(2, 1)) == pct(WHITE, 15) and mn.led(pad(2, 2)) == pct(WHITE, 15) and mn.led(pad(2, 3)) == (0, 0, 0),
       "picker: Keys now, Scale the previous, ISO and Iso Key under Keys, the rest of that row dark")
    ok(mn.led(pad(1, 5)) == MEM and mn.led(pad(1, 6)) == pct(MEM, 15), "picker: bank 1 of 8")
    ok(all(mn.led(pad(3, c)) == pct(PROG, 85) for c in range(1, 7)) and mn.led(pad(3, 7)) == pct(PROG, 12),
       "picker: progressions 1-6 (the factory set) lit, 7.. dim")
    ly = Run(["button:menu@200+400", "pad:25@300+50", "pad:0@900+50", "button:menu@1200+50", "button:menu@1500+400",
              "pad:26@1600+50"], 2000)
    ok(ly.says[:3] == ["Layout Scale", "Chord C major", "Layout Keys"] and ly.says[-1] == "Layout Chromatic",
       "Menu + Scale, a tap back to Keys, Menu + Chromatic (%s)" % ly.says)
    sc = Run(["button:menu@200+400", "pad:25@300+50"], 900)
    ok(sc.led(pad(4, 1)) == YELLOW and sc.led(pad(4, 8)) == YELLOW and sc.led(pad(3, 1)) == YELLOW and
       sc.led(pad(4, 2)) == KEY_D, "Scale: the tonic yellow each octave, the degrees grey")
    same(sc, scr_home(chord=False, label="Key C"), "Scale: Key Mode on (Key C top left)")
    sd = Run(["button:menu@200+400", "pad:25@300+50", "pad:2@800+300"], 1300)
    ok("Chord E minor" in sd.says, "Scale: the third degree plays the key's chord, E minor (%s)" % sd.says)
    ch = Run(["button:menu@200+400", "pad:26@300+50"], 900)
    ok(ch.led(pad(4, 1)) == YELLOW and ch.led(pad(4, 2)) == (12, 12, 14) and ch.led(pad(4, 3)) == pct(WHITE, 32) and
       ch.led(pad(3, 1)) == pct(WHITE, 32), "Chromatic: C yellow, C# dark, D in the scale; the row above from F")
    pg = Run(["button:menu@200+400", "pad:10@300+50", "pad:3@1600+300"], 2000)
    ok(pg.says[0].startswith("Progression 3"), "Menu + progression 3: the Prog layout with it (%s)" % pg.says[:1])
    pn_ = Run(["button:menu@200+900", "pad:10@300+50"], 800)
    same(pn_, (lambda s: (clbl(s, 0, "Progression 3"), bigc(s, 18, 30), s)[-1])(Scr()),
         "Menu + a progression: its name shown (Neo Soul)", png="12-prog-name")
    ok(all(pg.led(pad(4, c)) == pct(PROG, 85) for c in (1, 2, 3)), "Prog: its chords lit purple")
    ok(len(pg.says) >= 2 and pg.says[1].startswith("Chord "), "Prog: chord 4 plays (%s)" % pg.says[1:])
    ok(len(pg.screen) == 64 and ink(pg, 0, 0, 64, 14) and ink(pg, 4, 17, 128, 48) and ink(pg, 80, 50, 128, 64) and
       not ink(pg, 0, 50, 70, 64), "Prog view: its name top left, the chord big, chord k / n bottom right")
    PNGS["8-prog"] = pg.screen
    pg2 = Run(["button:menu@200+400", "pad:10@300+50", "button:shift@800+200", "button:right@850+50"], 1300)
    ok(any(s.startswith("Chords 17") for s in pg2.says), "Shift + Right in Prog: chords 17-32 (%s)" % pg2.says)

    # ---- the steps: written, cleared, held and edited
    st = Run(DM7 + ["step:0@1500+50", "step:4@1600+50", "step:4@1800+50", "step:8@2000+2000", "pad:24@2100+300",
                    "pad:1@2150+200", "knob:4@2500:4", "knob:1@2550:1"], 3800)
    ok(st.led(32) == SEQ and st.led(36) == (24, 24, 30), "step 1 tapped: the last chord in (blue); step 5 tapped "
       "twice: written, cleared")
    ok(st.led(40) == HELD and "Step 9, empty" in st.says, "step 9 held: orange, said")
    same(st, scr_step(9, "x2", "2 st"), "step 9 held: Ddim written by D + DIM, Ratchet x2, Note len 2",
         png="3-step-held")
    st2 = Run(DM7 + ["step:0@1500+50", "step:8@2000+300", "pad:21@2100+100", "step:0@3000+50"], 3500)
    ok(st2.led(40) == (24, 24, 30) and st2.led(32) == (24, 24, 30),
       "a step held + an empty memory: nothing; step 1 tapped again: cleared")
    st3 = Run(DM7 + ["pad:21@500+100", "step:5@1500+400", "pad:21@1600+100"], 2300)
    ok(st3.led(37) == SEQ, "a step held + memory 6: the step gets it")
    same(st3, scr_home(notes="D  F  A  C"), "home: the chord big, its notes (the steps on their LEDs)", png="1b-home-step")

    pe = Run(DM7 + ["button:right@1400+50", "step:0@1500+50"], 2000)
    ok(not any(w.startswith("Step 17") for w in pe.says) and pe.led(32) != SEQ,
       "a step past the loop's end (page 2 of a 16-step loop): nothing (%s)" % pe.says[-3:])

    # ---- Shift: the FM-1's buttons on the pads of columns 7-8 (proposal 5), the screen's 2 x 4 grid pad for name
    SCP = [pad(1 + i // 2, 1 + i % 2) for i in range(8)]       # KEY LOCK / LATCH PERF / B.PRF FX / BASS METRO
    sh = Run(["button:shift@200+600"], 700)
    COLS = [YELLOW, VIOLET, WHITE, VIOLET, BASS, GREEN, BASS, RED]
    on = [sh.led(SCP[i]) == COLS[i] for i in range(8)]
    ok(all(sh.led(SCP[i]) in (COLS[i], pct(COLS[i], 18)) for i in range(8)),
       "Shift held: the pads of columns 1-2 KEY .. METRO, lit when on, dim when off")
    ok(all(sh.led(pad(r, c)) == (0, 0, 0) for r in (1, 2) for c in (3, 4)) and
       all(sh.led(pad(r, c)) == (0, 0, 0) for r in (3, 4) for c in range(3, 9)) and sh.led(pad(1, 5)) == pct(MEM, 18),
       "Shift held: only the shortcuts and the top right's eight lit, the rest dark")
    JOBS = [i for i in range(16) if i not in (3, 11, 12)]          # the Move's step icons (its manual)
    ok(all(sh.led(32 + i) != (0, 0, 0) and sh.led(76 + i) == WHITE for i in JOBS) and
       all(sh.led(32 + i) == (0, 0, 0) and sh.led(76 + i) == (0, 0, 0) for i in (3, 11, 12)),
       "Shift held: the steps with an icon lit (dim: off), their icons on; S4, S12, S13 (no icon) dark")
    ok(not on[0] and not on[1], "Shift held: Key and Lock off at the start")
    same(sh, scr_shortcuts(on), "Shift held: the eight buttons, inverted = on", png="4-shortcuts")
    kt = Run(["button:shift@200+300", "pad:%d@250+50" % SCP[0], "button:shift@700+300", "pad:%d@750+50" % SCP[1]], 2300)
    ok(kt.says[:2] == ["Key, on", "Lock, on"], "Shift + the KEY pad tapped: Key Mode on; the LOCK pad: LOCK on (%s)"
       % kt.says)
    ok(not any(t < 2300 and i == 0 for t, i, c in []) and kt.lines and
       not any("tk-msg" in l for l in kt.lines), "a shortcut tapped: no popup (the inverted name says it)")
    same(kt, scr_home(chord=False, label="Key C", lock=True), 'home: Key C, LOCK on: the LK tag')
    pl = Run(["button:shift@200+700", "pad:%d@250+600" % SCP[3]], 1100)
    ok(pl.says == ["PERF, Strum"], "Shift + the PERF pad held: the PERF settings, said (%s)" % pl.says)
    same(pl, scr_layer("PERF", False, 1, 8), "the PERF settings: off, its mode big, the modes as dashes",
         png="9-perf-settings")
    ok(all(pl.led(55 + k) == VIOLET for k in range(4)) and all(pl.led(59 + k) == (0, 0, 0) for k in range(4)),
       "the PERF settings: knobs 1-4 lit violet, 5-8 dark")
    pw = Run(["button:shift@200+900", "pad:%d@250+600" % SCP[3], "knob:0@1300:1", "knob:0@1350:1", "knob:1@1500:1"],
             1700)
    ok(pw.says[1:] == ["PERF, Strum 2 Octaves", "PERF, Slop", "Rate 41 ms"], "the wheel turns its pages (the mode), "
       "knob 1 the page's first (%s)" % pw.says)
    pk = Run(["button:shift@200+900", "pad:%d@250+600" % SCP[0], "button:back@1500+50", "touch:4@1700+100"], 1900)
    ok(pk.says[:2] == ["KEY, Key", "Home"] and pk.says[-1].startswith("Key "), "Shift + the KEY pad held: KEY; Back: "
       "home; knob 5 is Key again (%s)" % pk.says)
    pk2 = Run(["button:shift@200+300", "pad:%d@250+600" % SCP[0], "button:shift@1500+300",
               "pad:%d@1550+600" % SCP[0]], 2400)
    ok(pk2.says == ["KEY, Key", "Closed"], "the same pad held again: closed (%s)" % pk2.says)
    fx = Run(["button:shift@200+900", "pad:%d@250+600" % SCP[5], "knob:2@1500:3"], 1700)
    ok(fx.says[0] == "FX, Sends" and fx.says[1].startswith("Chorus "), "FX held: the Sends page; knob 2 the "
       "Chorus send (%s)" % fx.says)
    pb = Run(["button:shift@200+900", "pad:%d@250+600" % SCP[3], "button:shift@1300+300", "step:7@1350+50",
              "button:back@1700+50"], 2000)
    ok(pb.says[-1] == "Home" and pb.led(pad(1, 5)) == pct(MEM, 18),
       "the editor over a settings layer, Back: home at once, the memories back (%s)" % pb.says)

    # ---- Shift + S8: the sound editor (proposal 5)
    ed = Run(["button:shift@200+300", "step:7@250+50", "knob:5@700:4", "pad:29@2000+50"], 2300)
    ok(len(ed.says) == 3 and ed.says[0] == "Sound editor, chord, Oscillators" and ed.says[1].startswith("Level ") and
       ed.says[2] == "Filter 1", "Shift + S8: the sound editor (OSC); knob 5 the next lane's first, Level; the second "
       "memory pad: FILT (%s)" % ed.says)
    ok(ed.led(pad(1, 6)) == ORANGE and ed.led(pad(1, 5)) == pct(CYAN, 22) and ed.led(pad(2, 8)) == pct(BASS, 25),
       "the editor: the memories' pads its groups, each its colour, FILT full, OSC dim; the eighth chord / bass")
    ok(ed.led(55) == CYAN, "the editor: its knobs lit cyan")
    ec = Run(["button:shift@200+300", "step:7@250+50"], 700)
    same(ec, scr_popup("Chord"), "entering: a card, Chord / EDIT", png="10-editor-card")
    em = Run(["button:shift@200+300", "step:7@250+50", "button:shift@1500+300", "step:7@1550+50"], 1900)
    ok(em.says[-1] == "Memories", "Shift + S8 again: the memories (%s)" % em.says[-1:])
    same(em, (lambda s: (bigc(s, 18, 30), s)[-1])(Scr()), "leaving: a card, MEMORY")
    eb_ = Run(["button:shift@200+300", "step:7@250+50", "button:back@2000+50"], 2300)
    ok(eb_.says[-1] == "Home" and eb_.led(pad(1, 5)) == pct(MEM, 18), "the editor, Back: home (the memories back)")

    # ---- Shift + S2: the Settings, RemoteFM1's list (proposal 5)
    se = Run(["button:shift@200+300", "step:1@250+50", "knob:0@600:1", "button:jogclick@700+50", "knob:0@800:-30",
              "button:jogclick@900+50"], 1200)
    ok(se.says == ["Settings, Velocity: Dynamic", "Fixed level: 100", "Editing Fixed level: 100", "Fixed level: 70",
                   "Done"], "Shift + S2: the Settings; the wheel a row, a click edits, the wheel the value, a click: "
       "done (%s)" % se.says)
    ok(ink(se, 0, 0, 100, 14) and se.screen[16] == "1" * 128 and all(se.led(55 + k) == (0, 0, 0) for k in range(8)),
       "the Settings: its title, the rule, the list; the knobs dark")
    PNGS["11-settings"] = se.screen
    sb = Run(["button:shift@200+300", "step:1@250+50", "knob:0@600:1", "button:jogclick@700+50",
              "button:back@900+50", "button:back@1100+50"], 1400)
    ok(sb.says[-2:] == ["Done", "Home"], "Back while editing: done (the value kept); again: home (%s)" % sb.says)
    FIX1 = ["button:shift@200+300", "step:1@250+50", "button:jogclick@500+50", "knob:0@600:1", "button:jogclick@700+50",
            "knob:0@800:1", "button:jogclick@900+50", "knob:0@1000:-60", "knob:0@1010:-60", "button:back@1100+50",
            "button:back@1200+50"]
    lo, hi = rms(FIX1 + ["pad:4@1500+1500"], 1600, 1.0), rms(["pad:4@1500+1500"], 1600, 1.0)
    ok(0 < lo < hi * 0.3, "Settings, Velocity Fixed at level 1: the pads quiet (rms %.4f; Dynamic %.4f)" % (lo, hi))

    # ---- Loop held: the slots (proposal 4, E)
    lh = Run(["button:loop@200+600"], 700)
    same(lh, scr_slots(0, ()), "Loop held: the ten slots, Loop 1 big, the tempo", png="4-slots")
    ok(lh.led(32) == HELD and lh.led(33) == pct(SEQ, 12) and lh.led(42) == (0, 0, 0) and lh.led(70) == WHITE,
       "Loop held: slot 1 orange, 2..10 dim, Loop lit")
    sw = Run(DM7 + ["step:0@1500+50", "button:loop@2000+2000", "step:1@2200+50", "knob:1@2400:4"], 3700)
    ok(any(s.startswith("loop 2") for s in sw.says) and "Length 1 bar" in sw.says, "Loop + step 2: slot 2 (slot 1 "
       "saved); Loop + knob 1: the length (%s)" % sw.says)
    ok("Latch, on" not in sw.says, "Loop held and used: no LATCH")
    same(sw, scr_slots(1, (0,)), "the slots view: slot 2 current, slot 1 holds a loop")
    ok(all(sw.led(76 + i) == (0, 0, 0) for i in range(16)), "the icons: off (slot 2's loop is one page)")

    # ---- the wheel: preset; click: engine; Shift: the bass (proposal 4, A B)
    wh = Run(["knob:0@200:1", "button:jogclick@500+50", "knob:0@800:1", "button:shift@1100+400", "knob:0@1200:1"],
             1700)
    ok(len(wh.says) == 4 and wh.says[2].startswith("Engine ") and wh.says[3].startswith("Bass "),
       "the wheel: a preset; clicked: the engine; Shift: the bass sound (%s)" % wh.says)

    # ---- the full isomorphic layout (proposal 4, F)
    iso = Run(["button:menu@200+400", "pad:16@300+50", "pad:20@900+300", "button:shift@1400+700"], 1900)
    ok(iso.says[:2] == ["Layout Isomorphic", "Chord D"], "Menu + ISO; the top row's fifth pad is D (%s)" % iso.says)
    ok(iso.led_when(pad(2, 3), 1300) == YELLOW and iso.led_when(pad(1, 1), 1300) == (12, 12, 14) and
       iso.led(pad(1, 1)) == pct(YELLOW, 18) and iso.led(pad(1, 5)) == pct(MEM, 18),
       "ISO: notes on the top rows too (C5 yellow, D#5 dark); Shift held: the shortcuts and the memories")

    # ---- a knob's popup
    kp = Run(["touch:0@200+300", "knob:1@250:2"], 400)
    same(kp, scr_popup("Voicing", 1000 * 14 // 24), "Voicing touched and turned: name, then the value big",
         png="6-knob-popup")
    ok(kp.says[:2] == ["Voicing 0", "Voicing plus 2"], "said: %s" % kp.says)
    p1 = Run(["knob:5@200:1", "knob:5@210:1", "knob:5@220:1"], 400)
    same(p1, scr_popup("Key"), "Key: three ticks are not an entry yet")
    ap_ = Run(["button:shift@200+1400", "pad:%d@250+50" % SCP[3], "pad:%d@500+600" % SCP[3], "knob:0@1300:1", "knob:0@1310:1",
               "knob:0@1320:1", "button:back@1700+50", "pad:25@1900+1500", "pad:17@1900+1500", "pad:1@2000+1400"], 2900)
    ok(len(ap_.screen) == 64 and ap_.screen[61] == "1" * 128 and ink(ap_, 80, 0, 128, 14) and ink(ap_, 0, 34, 128, 56),
       "Arp running: the hop view (the division top right, the notes, its rule at the foot)")
    PNGS["7-arp"] = ap_.screen
    sp = Run(["button:shift@200+400", "knob:8@300:1"], 500)
    ok(sp.says[-1].startswith("Tempo 121"), "Shift + knob 8: Tempo, as without Shift (one layer) (%s)" % sp.says)

    # ---- the buttons
    bt = Run(["button:loop@400+50", "button:mute@600+50", "button:up@800+50"], 1000)
    ok(bt.says[:3] == ["Latch, on", "Bass, on", "Octave, plus 1"], "Loop LATCH, Mute bass, Up the octave: %s" % bt.says)
    ok(bt.led(70) == (255,) * 3 and bt.led(74) == (255,) * 3 and bt.led(68) == (255,) * 3, "their LEDs lit")
    pn = Run(DM7[:1] + ["button:shift@350+200", "button:delete@400+50"], 700)
    ok("PANIC, all notes off" in pn.says, "Shift + Delete: PANIC (%s)" % pn.says)
    same(pn, (lambda s: (s.rect(0, 0, 128, 64), bigc(s, 8, 22), s.ctext(64, 48, "all off", 2, 0), s)[-1])(Scr()),
         "PANIC: inverted")
    pn2 = Run(DM7[:1] + ["button:delete@400+50"], 700)
    ok(not any("PANIC" in t for t in pn2.says) and pn2.says[-1].startswith("Delete: hold"),
       "Delete tapped alone: no PANIC, a hint (%s)" % pn2.says)

    # ---- the clipboard: Copy / Delete as RemoteFM14Move's sequencer (proposal 5)
    cp = Run(DM7 + ["step:0@1500+50", "button:copy@1700+600", "step:0@1800+50", "step:2@1900+50"], 2500)
    ok(cp.says[-2:] == ["Step 1 copied", "Pasted to step 3"] and cp.led(34) == SEQ, "Copy held + step 1: copied; + "
       "the empty step 3: pasted (%s)" % cp.says[-2:])
    ca = Run(DM7 + ["step:0@1500+50", "button:copy@1700+300", "step:0@1800+50", "step:5@2300+50"], 2700)
    ok(ca.says[-2:] == ["Tap a step to paste", "Pasted to step 6"] and ca.led(37) == SEQ,
       "Copy let go after a copy: the next step tap pastes (%s)" % ca.says[-2:])
    ok(any(i == 32 and c == WHITE for t, i, c in ca.led_at if 1850 < t < 2300), "the copied step blinks white")
    cu_ = Run(DM7 + ["step:0@1500+50", "button:delete@1700+800", "step:0@1800+50", "step:3@2000+50"], 2700)
    ok(cu_.says[-2:] == ["Step 1 cut", "Pasted to step 4"] and cu_.led(32) == (24, 24, 30) and cu_.led(35) == SEQ,
       "Delete held + step 1: cut; step 4 then: pasted (%s)" % cu_.says[-2:])
    cr_ = Run(DM7 + ["step:0@1500+50", "button:delete@1700+1200", "step:0@1800+50"], 2800)
    ok(any(i == 32 and c == RED for t, i, c in cr_.led_at if 1850 < t < 2800), "the cut step blinks red")
    cd = Run(DM7 + ["step:0@1500+50", "button:copy@1700+300", "step:0@1800+50", "button:copy@2300+50"], 2600)
    ok(cd.says[-1] == "Paste dropped", "Copy tapped alone with a paste waiting: dropped (%s)" % cd.says[-1:])
    du = Run(DM7 + ["step:0@1500+50", "button:copy@1700+50"], 2100)
    ok(du.says[-1] == "Loop copied, slot 2", "Copy tapped alone: the loop into the next empty slot (%s)" % du.says[-1:])
    dp = Run(DM7 + ["step:0@1500+50", "button:delete@1700+300", "button:play@1800+50"], 2300)
    ok(dp.says[-1] == "Loop, cleared" and dp.led(32) == (24, 24, 30) and dp.led(48) == (0, 40, 0),
       "Delete + Play: the loop cleared, not played (%s)" % dp.says[-1:])
    sh_ = Run(DM7 + ["step:0@1500+50", "step:0@2200+900"], 3000)
    ok(sh_.led(pad(1, 2)) == WHITE and sh_.led(pad(2, 2)) == WHITE and sh_.led(pad(4, 2)) == WHITE and
       sh_.led(pad(4, 3)) == KEY_D and sh_.led(32) == HELD, "a step held: its root (D) and keys (MIN, m7) lit white")
    quiet, heard = (rms(DM7 + ["step:0@1500+50", "step:0@4000+%d" % h], 3900, 1.2) for h in (50, 900))
    ok(quiet == 0.0 and heard > 0.01, "stopped: a step tapped is silent, held it is previewed (rms %.4f, %.4f)"
       % (quiet, heard))

    # ---- proposal 7: Shift + steps by the Move's icons; a toggle a card, a value a popup the wheel turns
    S6 = "button:shift@200+300"
    def card(t, v):
        return (lambda x: (clbl(x, 0, t), bigc(x, 18, 30), x)[-1])(Scr())
    sl1 = Run([S6, "step:0@250+50", "knob:0@600:1", "button:jogclick@800+50", "button:shift@1200+300",
               "step:0@1250+50"], 1700)
    ok(sl1.says == ["Slot, playing, Loop 1", "Slot, empty, Loop 2", "Done", "Slot, playing, Loop 2"],
       "Shift + S1 (Set overview): the slots, the wheel one, a click loads it (%s)" % sl1.says)
    ci = Run([S6, "step:2@250+50"], 600)
    same(ci, card("Count-in", "Off"), "Shift + S3 (Workflow): Count-in, a card")
    ok(ci.says == ["Count-in, off"], "said: %s" % ci.says)
    te = Run([S6, "step:4@250+50", "knob:0@600:3"], 900)
    same(te, card("Tempo", "123 bpm"), "Shift + S5 (Tempo): its popup, the wheel 123", png="14-tempo")
    mt = Run([S6, "step:5@250+50"], 600)
    ok(mt.says == ["Metronome, on"], "Shift + S6 (Metronome): on, a card (%s)" % mt.says)
    gr = Run([S6, "step:6@250+50", "knob:0@600:3"], 900)
    ok(gr.says == ["Groove, 0 percent", "Groove, 6 percent"], "Shift + S7 (Groove): the loop's swing (%s)" % gr.says)
    ks = Run([S6, "step:8@250+50", "knob:0@600:1", "button:shift@800+300", "knob:0@900:1"], 1300)
    ok(ks.says == ["Key & Scale, C Major", "Key & Scale, C Minor", "Key & Scale, C# Minor"],
       "Shift + S9 (Keys & Scales): the wheel the scale, Shift + wheel the key (%s)" % ks.says)
    fv_ = Run([S6, "step:9@250+50"], 600)
    same(fv_, card("Fixed velocity", "On"), "Shift + S10 (Full velocity): Fixed velocity, a card")
    pf_ = Run([S6, "step:10@250+50"], 600)
    ok(pf_.says == ["Perform, on"], "Shift + S11 (Repeat): Perform on (%s)" % pf_.says)
    LP = DM7 + ["step:0@1200+50", "step:4@1250+50"]
    nw = Run(LP + ["button:shift@1500+300", "step:13@1550+50"], 2000)
    ok(nw.says[-1] == "New, empty, Loop 2", "Shift + S14 (New clip): the next empty slot (%s)" % nw.says[-1:])
    db = Run(LP + ["button:shift@1500+300", "step:14@1550+50", "button:right@2600+50"], 3000)
    ok("Loop doubled, 32 steps" in db.says and db.led(32) == SEQ and db.led(36) == SEQ,
       "Shift + S15 (Double loop): 32 steps, page 2 a copy of page 1 (%s)" % db.says[-2:])
    qt = Run([S6, "step:15@250+50", "knob:0@600:1", "button:back@900+50"], 1200)
    ok(qt.says == ["Quantize, none", "Quantize, 1/4", "Home"], "Shift + S16 (Quantize): its popup; Back: home (%s)"
       % qt.says)
    st_ = Run(DM7 + ["step:0@1200+50", "button:shift@1500+300", "step:1@1550+50", "knob:0@1900:5",
                     "button:jogclick@2000+50", "knob:0@2100:1", "knob:0@2200:1"], 2500)
    ok(st_.says[-3:] == ["Editing Steps: 16", "Steps: 17", "Steps: 18"], "the Settings hold the loop's rows: Steps "
       "(%s)" % st_.says[-3:])
    ph = Run(["button:shift@200+1500", "pad:%d@250+700" % SCP[3]], 1400)
    same(ph, scr_layer("PERF", False, 1, 8), "Shift + the PERF pad held, Shift still held: the settings at once")
    cpy = Run(DM7 + ["step:0@1200+50", "step:4@1250+50", "button:copy@1500+900", "step:0@1700+50"], 2200)
    ok(cpy.says[-1] == "Step 1 copied" and ink(cpy, 0, 0, 30, 30) and cpy.screen[37][1:7] == "111111",
       "Copy held: the copy icon, the steps (no step view)")
    PNGS["15-copy-held"] = cpy.screen
    # values (the owner, 2026-11-24: "way too twitchy .. room size almost jumps from 0 to 127 while barely turning the
    # knob", the cutoff slow to sweep): a step every 2 ticks (a turn's first tick at once), through the FM-1's own
    # acceleration (kn_turn: slow, the finest step; quick, up to the range / 20 a detent); the editor's too
    FXR = ["button:shift@200+900", "pad:%d@250+600" % SCP[5], "knob:0@1300:1"]     # FX held, the Reverb page
    EDF = ["button:jogclick@200+50", "knob:0@400:1", "button:jogclick@700+50", S6.replace("200", "1000"),
           "step:7@1050+50", "pad:29@1500+50"]                                    # FM TONE, the editor's filter
    def turn(base, kn, t0, n, gap):
        return Run(base + ["knob:%d@%d:-1" % (kn, t0 + i * gap) for i in range(n)], t0 + n * gap + 300).says[-1:]
    rv = [turn(FXR, 1, 1800, 3, 10), turn(FXR, 1, 1800, 8, 200), turn(FXR, 1, 1800, 40, 8)]
    ok(rv == [["Size 86"], ["Size 84"], ["Size 0"]], "FX Reverb Size (88): a quick nudge of 3 ticks two steps, 8 slow "
       "ticks four, a quick spin of 40 the whole way (%s)" % rv)
    fq = [turn(EDF, 5, 1900, 8, 200), turn(EDF, 5, 1900, 60, 8)]
    ok(fq == [["Freq 126.96"], ["Freq 0.00"]], "the editor's FM TONE Freq (127.00, hi-res): 8 slow ticks 0.01 a "
       "step, a quick spin of 60 the whole way (%s)" % fq)
    jt = Run([S6, "step:1@250+50", "touch:9@600+400", "knob:0@700:1"], 1100)
    ok(jt.says == ["Settings, Velocity: Dynamic", "Fixed level: 100"] and ink(jt, 0, 0, 100, 14) and
       jt.screen[16] == "1" * 128, "the Settings: the wheel touched and turned moves the row, no preset (%s)" % jt.says)
    sl_ = Run(["button:shift@200+1500", "step:1@250+50"], 1000)
    ok(sl_.screen[16] == "1" * 128, "Shift still held: the Settings shown, not the Shift screen")
    mc = Run(DM7 + ["step:0@1200+50", "button:play@1400+50", "pad:2@2000+200", "step:5@3200+50", "button:play@3500+50",
                    "step:5@4000+900"], 5100)
    ok(mc.says[-1] == "Step 6, E", "a step tapped while the loop plays: the chord the player played (E), not the "
       "loop's (%s)" % mc.says[-2:])

    # ---- Iso Key and the Perf amount knob (proposal 5)
    ik = Run(["button:menu@200+400", "pad:17@300+50", "pad:8@900+300", "pad:9@1300+300"], 1800)
    ok(ik.says[0] == "Layout Isomorphic, in key" and ik.says[1:3] == ["Chord F", "Chord G"],
       "Menu + Iso Key: in key, the row above a 4th up (F, then G) (%s)" % ik.says)
    ok(all(ik.led(n) in (YELLOW, pct(WHITE, 32)) for n in range(32)), "Iso Key: every pad in the key")
    pa = Run(["knob:3@200:1", "knob:3@220:1", "knob:3@240:1", "knob:3@260:1", "knob:3@280:1", "knob:3@300:1"], 500)
    ok(len(pa.says) == 2, "Perf amount: three ticks a step (six ticks: two) (%s)" % pa.says)
    lk = Run(["pad:26@200+1300", "button:shift@300+300", "pad:%d@350+50" % SCP[1], "pad:1@1600+100", "pad:3@1900+100"],
             2200)
    ok("Chord D major" in lk.says and "Chord F major" in lk.says, "MAJ held + Shift + the LOCK pad: MAJ latched (%s)"
       % lk.says)
    rc = Run(DM7 + ["step:0@1500+50", "button:play@1700+50"], 2000)
    ok(rc.led(48) == (0, 200, 0) and rc.led_when(32, 1720) == WHITE and rc.led(32) == SEQ and
       WHITE in [rc.led(32 + i) for i in range(1, 16)], "Play: the slot plays, Play lit green, the playhead (white) "
       "from step 1 on")
    PNGS["1c-home-playing"] = rc.screen
    # an empty slot picked while playing: the bar runs on (Play lit, the playhead moving); a step written joins it;
    # Play stops it (the owner, 2026-11-21)
    eb = DM7 + ["step:0@1500+50", "button:play@1700+50", "button:loop@2500+400", "step:2@2600+50"]
    e1 = Run(eb, 7000)
    heads = {t for t, i, c in e1.led_at if 32 <= i < 48 and c == WHITE and t > 5000}
    ok(e1.led(48) == (0, 200, 0) and len(heads) >= 8, "an empty slot while playing: the bar runs on (%d playhead "
       "moves after 5 s)" % len(heads))
    e2 = Run(eb + ["pad:1@4500+300", "step:8@5000+50"], 8000)
    ok(e2.led(48) == (0, 200, 0) and e2.led(40) == SEQ and
       any(c == WHITE for t, i, c in e2.led_at if i == 40 and t > 5500), "a step written in it: the loop plays in "
       "phase (step 9 lit, the playhead over it)")
    e3 = Run(eb + ["button:play@5000+50"], 7000)
    ok(e3.led(48) == (0, 40, 0), "Play: the empty bar stops")
    rm = Run(["button:shift@200+300", "button:rec@300+50"], 600)
    ok("Rec mode, Advance" in rm.says, "Shift + Rec: the next record mode (%s)" % rm.says)

    # ---- the global save (Capture): the memories, the bank and the session kept over a power cycle
    import tempfile
    with tempfile.TemporaryDirectory(prefix="rfm1_cr_tk") as tmp:
        fl = os.path.join(tmp, "flash.bin")
        sv = Run(DM7 + ["pad:21@500+100", "knob:1@1200:3", "button:menu@1500+600", "pad:29@1700+50",
                        "button:capture@2500+50"], 3000, ("--flash-out", fl))
        ok("saved" in sv.says, "Capture: the global save (%s)" % sv.says[-1:])
        rb = Run(["button:menu@200+600"], 700, ("--flash-in", fl))
        same(rb, scr_layout(bank=2), "after a power cycle: bank 2 still picked")
        ok(rb.led(pad(1, 5)) == pct(MEM, 50) and rb.led(pad(1, 6)) == MEM,
           "after a power cycle: bank 1 holds a chord (memory 6), bank 2 picked")
        rb2 = Run(["button:menu@200+300", "pad:28@300+50", "pad:21@800+100", "touch:0@1500+100"], 1700,
                  ("--flash-in", fl))
        ok(rb2.says[1:3] == ["Memory 6, D minor seven", "Voicing plus 3"],
           "after a power cycle: bank 1's memory 6 plays Dm7, the voicing (the session's) +3 (%s)" % rb2.says)

    if A.png and PNGS:
        try:
            from PIL import Image
        except ImportError:
            print("(no Pillow: no PNGs)")
        else:
            os.makedirs(A.png, exist_ok=True)
            for name, rows in PNGS.items():
                im = Image.new("RGB", (128 * 4, 64 * 4), (8, 8, 8))
                px = im.load()
                for y, row in enumerate(rows):
                    for x, v in enumerate(row):
                        if v == "1":
                            for dy in range(3):
                                for dx in range(3):
                                    px[x * 4 + dx, y * 4 + dy] = (236, 236, 236)
                im.save(os.path.join(A.png, "cr-move-%s.png" % name))
            print("%d PNGs in %s" % (len(PNGS), A.png))
    print("%d failures" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
