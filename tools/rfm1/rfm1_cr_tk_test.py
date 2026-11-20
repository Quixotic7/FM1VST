#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ChoralRoot's Move takeover, "CHORDS" (docs/RFM1-TAKEOVER.md tk1; the firmware's firmware/src/cr_move.c, built into
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


# the design's screens (make_cr_v2.py scr_home / scr_saved / scr_step / scr_slots / scr_layout / scr_prog2,
# make_cr_mockups.py scr_popup), parameterised by what this run holds
def scr_home(key, slot, name, filled=(), play=None, nsteps=16):
    s = Scr()
    s.header(key, "SEQ %d" % slot)
    s.text(1, 19, name, 4)
    for i in range(16):
        x = 1 + i * 8
        if i in filled:
            s.rect(x, 52, 7, 7)
        else:
            s.box(x, 52, 7, 7, 1, dotted=i >= nsteps)
        if i == play:
            s.rect(x, 61, 7, 3)
    return s


def scr_saved(n, name):
    s = Scr()
    s.ctext(64, 0, "Memory %d" % n, 2)
    s.ctext(64, 17, name, 4)
    s.ctext(64, 50, "saved", 2)
    return s


def scr_step(n, name, rat="x1", ln="len 1", chance="100%"):
    s = Scr()
    s.header("STEP %d" % n, rat)
    s.text(1, 19, name, 4)
    s.text(1, 50, ln, 2)
    s.text(127 - Scr.tw(chance, 2), 50, chance, 2)
    return s


def scr_slots(cur, full, page=1):
    s = Scr()
    s.header("SLOTS", "PG %d" % page)
    for i in range(10):
        x = 1 + (i % 5) * 25
        y = 20 + (i // 5) * 22
        if i == cur:
            s.rect(x, y, 24, 20)
            s.ctext(x + 12, y + 3, str(i + 1), 2, 0)
        else:
            s.box(x, y, 24, 20, 1, dotted=i not in full)
            s.ctext(x + 12, y + 3, str(i + 1), 2)
    return s


def scr_layout(cur="KEYS", prev="SCALE", bank=1):
    s = Scr()
    s.header("LAYOUT", "BK %d" % bank)
    s.ctext(64, 19, cur, 4)
    s.text(1, 50, "prev " + prev, 2)
    return s


def scr_prog(name, num, chord, filled, last):
    s = Scr()
    s.header(name, str(num))
    s.text(1, 19, chord, 4)
    for i in range(16):
        x = 1 + i * 8
        (s.rect if i in filled else s.box)(x, 52, 7, 7, 1, **({} if i in filled else {"dotted": True}))
        if i == last:
            s.rect(x, 61, 7, 3)
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


def same(run, exp, what, png=None):
    got = run.screen
    want = exp.rows()
    good = ok(got == want, "screen: " + what)
    if not good:
        for y in range(min(len(got), 64)):
            if got[y] != want[y]:
                print("  row %2d got  %s\n         want %s" % (y, got[y], want[y]))
    if png:
        PNGS[png] = got
    return good


def dim(c, f):
    return tuple(int(v * f) for v in c)


VIOLET_D, ORANGE_D = (0x36, 0x10, 0x47), (0x47, 0x21, 0x00)
VIOLET, ORANGE = (196, 58, 255), (255, 120, 0)
KEY_D, YELLOW, WHITE = (0x51, 0x51, 0x51), (0xFF, 0xD2, 0x28), (255, 255, 255)
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


KEY_ON = "knob:6@150:48"                           # Key + 12 semitones (4 ticks a semitone): the tonic stays C, Key Mode on ("C MAJ")
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
    ok(info.get("version") == 1 and info.get("size") == 1468 and info.get("name") == "CHORDS",
       "the record: version 1, size 1468, name CHORDS (%s)" % r.stdout.strip())
    ok(info.get("claims") == 0x5F, "claims 0x5F: all but TRACKS and VOLUME")
    m = custom_rfm1(open(A.file, "rb").read())
    ok("tk1" in m.get("wants", []) and m.get("takeover") == {
        "name": "CHORDS", "claims": ["screen", "pads", "steps", "knobs", "jog", "buttons"]},
       "the manifest: wants tk1, takeover %s" % m.get("takeover"))
    c = subprocess.run([sys.executable, CHECK, "--run", A.run, A.file], capture_output=True, text=True)
    ok(c.returncode == 0 and 'takeover (tk1, "CHORDS" claims 0x5f)' in c.stdout, "rfm1_check --run accepts it")

    # ---- entering: the home view, the Keys layout's LEDs
    e = Run([], 600)
    ok(e.rc == 0 and "tk-state@100: on claims 0x5f speech" in e.lines, "entering: shown with claims 0x5f")
    same(e, scr_home("FREE", 1, "-"), "home on entering (no chord yet, Key Mode off, the empty slot's bar)")
    ok(e.led(pad(4, 1)) == YELLOW and e.led(pad(4, 8)) == YELLOW and e.led(pad(4, 2)) == KEY_D and
       e.led(pad(3, 2)) == KEY_D and e.led(pad(3, 1)) == (0, 0, 0) and e.led(pad(3, 4)) == (0, 0, 0),
       "Keys layout: C and the top C yellow, the keys grey, no key above E / B")
    ok(all(e.led(pad(1, c)) == VIOLET_D for c in range(1, 5)) and all(e.led(pad(2, c)) == ORANGE_D for c in range(1, 5)),
       "the chord keys: types violet, extensions orange (dim)")
    ok(all(e.led(pad(r, c)) == pct(MEM, 18) for r in (1, 2) for c in range(5, 9)), "eight empty memories, dim cyan")
    ok(all(e.led(32 + i) == (24, 24, 30) for i in range(16)), "the steps: 16 dark (an empty slot: a bar)")
    ok(e.led(76) == (255,) * 3 and all(e.led(77 + i) == (64,) * 3 for i in range(3)), "the step icons: page 1 of 4")
    ok(e.led(55) == ORANGE and e.led(56) == VIOLET and e.led(60) == YELLOW, "the knob rings: Voicing, Perform, Key")

    # ---- a chord from the chord keys and a key
    c1 = Run([KEY_ON] + DM7, 2000)
    ok("Chord D minor seven" in c1.says, "MIN + m7 + D: said \"Chord D minor seven\" (%s)" % c1.says)
    same(c1, scr_home("C MAJ", 1, "Dm7"), "home: Dm7 in C major", png="1-home-dm7")
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
    same(mm2, scr_saved(6, "Dm7"), "the saved popup", png="2-memory-saved")
    mp = Run(DM7 + ["pad:21@500+100", "pad:21@2500+300", "button:delete@3500+300", "pad:21@3600+100"], 4200)
    ok(mp.says.count("Memory 6, D minor seven") == 2, "memory 6 tapped alone: plays it, said (%s)" % mp.says)
    ok("Memory 6, cleared" in mp.says and mp.led(pad(2, 6)) == pct(MEM, 18), "Delete + memory 6: cleared, dim")
    ok(not any("PANIC" in s for s in mp.says), "Delete used as the clear modifier: no PANIC")

    # ---- the Menu picker and the layouts
    mn = Run(["button:menu@200+600"], 700)
    same(mn, scr_layout(), "Menu held: the picker (layout E)", png="5-layout-picker")
    ok(mn.led(pad(1, 1)) == WHITE and mn.led(pad(1, 2)) == pct(WHITE, 45) and mn.led(pad(1, 3)) == pct(WHITE, 15) and
       mn.led(pad(2, 1)) == (0, 0, 0), "picker: Keys now, Scale the previous, the free row dark")
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
    ok(sc.screen[:16] == scr_home("C MAJ", 1, "-").rows()[:16], "Scale: Key Mode on (\"C MAJ\")")
    sd = Run(["button:menu@200+400", "pad:25@300+50", "pad:2@800+300"], 1300)
    ok("Chord E minor" in sd.says, "Scale: the third degree plays the key's chord, E minor (%s)" % sd.says)
    ch = Run(["button:menu@200+400", "pad:26@300+50"], 900)
    ok(ch.led(pad(4, 1)) == YELLOW and ch.led(pad(4, 2)) == (12, 12, 14) and ch.led(pad(4, 3)) == pct(WHITE, 32) and
       ch.led(pad(3, 1)) == pct(WHITE, 32), "Chromatic: C yellow, C# dark, D in the scale; the row above from F")
    pg = Run(["button:menu@200+400", "pad:10@300+50", "pad:3@800+300"], 1200)
    ok(pg.says[0].startswith("Progression 3"), "Menu + progression 3: the Prog layout with it (%s)" % pg.says[:1])
    ok(all(pg.led(pad(4, c)) == pct(PROG, 85) for c in (1, 2, 3)), "Prog: its chords lit purple")
    ok(len(pg.says) >= 2 and pg.says[1].startswith("Chord "), "Prog: chord 4 plays (%s)" % pg.says[1:])
    ok(pg.screen[16:] and pg.screen[0][:2] == "11" and pg.screen[62][1 + 3 * 8] == "1",
       "Prog view: its header, chord 4 marked under the strip")
    PNGS["8-prog"] = pg.screen
    pg2 = Run(["button:menu@200+400", "pad:10@300+50", "button:shift@800+200", "button:right@850+50"], 1300)
    ok(any(s.startswith("Chords 17") for s in pg2.says), "Shift + Right in Prog: chords 17-32 (%s)" % pg2.says)

    # ---- the steps: written, cleared, held and edited
    st = Run(DM7 + ["step:0@1500+50", "step:4@1600+50", "step:4@1800+50", "step:8@2000+2000", "pad:24@2100+300",
                    "pad:1@2150+200", "knob:4@2500:4", "knob:1@2550:1"], 3800)
    ok(st.led(32) == SEQ and st.led(36) == (24, 24, 30), "step 1 tapped: the last chord in (blue); step 5 tapped "
       "twice: written, cleared")
    ok(st.led(40) == HELD and "Step 9, empty" in st.says, "step 9 held: orange, said")
    same(st, scr_step(9, "Ddim", "x2", "len 2"), "step 9 held: Ddim written by D + DIM, Ratchet x2, Note len 2",
         png="3-step-held")
    st2 = Run(DM7 + ["step:0@1500+50", "step:8@2000+300", "pad:21@2100+100", "step:0@3000+50"], 3500)
    ok(st2.led(40) == (24, 24, 30) and st2.led(32) == (24, 24, 30),
       "a step held + an empty memory: nothing; step 1 tapped again: cleared")
    st3 = Run(DM7 + ["pad:21@500+100", "step:5@1500+400", "pad:21@1600+100"], 2300)
    ok(st3.led(37) == SEQ, "a step held + memory 6: the step gets it")
    same(st3, scr_home("FREE", 1, "Dm7", filled=(5,)), "home: the strip shows step 6 filled", png="1b-home-step")

    pe = Run(DM7 + ["button:right@1400+50", "step:0@1500+50"], 2000)
    ok(not any(w.startswith("Step 17") for w in pe.says) and pe.led(32) != SEQ,
       "a step past the loop's end (page 2 of a 16-step loop): nothing (%s)" % pe.says[-3:])

    # ---- Shift: the slots and the pages
    sh = Run(["button:shift@200+600"], 700)
    same(sh, scr_slots(0, ()), "Shift held: the slots view (layout D)", png="4-slots")
    ok(sh.led(32) == HELD and sh.led(33) == pct(SEQ, 12) and sh.led(42) == (0, 0, 0) and sh.led(44) == WHITE and
       sh.led(45) == pct(WHITE, 25), "Shift held: slot 1 orange, 2..10 dim, 13 = page 1 lit")
    sw = Run(DM7 + ["step:0@1500+50", "button:shift@2000+2000", "step:1@2200+50", "step:13@2300+50"], 3700)
    ok(any(s.startswith("loop 2") for s in sw.says) and "Page 2" in sw.says, "Shift + step 2: slot 2 (slot 1 saved); "
       "Shift + step 14: page 2 (%s)" % sw.says)
    same(sw, scr_slots(1, (0,), 2), "the slots view: slot 2 current, slot 1 holds a loop, page 2")
    ok(sw.led(77) == (255,) * 3 and sw.led(76) == (64,) * 3, "the icons: page 2")

    # ---- a knob's popup
    kp = Run(["touch:0@200+300", "knob:1@250:2"], 400)
    same(kp, scr_popup("Voicing", "+2", 14 / 24), "Voicing touched and turned: name, then the value big",
         png="6-knob-popup")
    ok(kp.says[:2] == ["Voicing 0", "Voicing plus 2"], "said: %s" % kp.says)
    pf = Run(["knob:2@200:4", "knob:2@210:4", "knob:2@220:4", "knob:2@230:4"], 400)
    same(pf, scr_popup("Perform", "Arp"), "Perform turned four entries (16 ticks): Arp")
    p1 = Run(["knob:2@200:1", "knob:2@210:1", "knob:2@220:1"], 400)
    same(p1, scr_popup("Perform", "Off"), "Perform: three ticks are not an entry yet")
    ap_ = Run(["knob:2@200:4", "knob:2@210:4", "knob:2@220:4", "knob:2@230:4"] + [
        "pad:25@1500+1500", "pad:17@1500+1500", "pad:1@1600+1400"], 2500)
    ok(ap_.screen[0].startswith("1") and ap_.screen[61] == "1" * 128,
       "Arp running: the hop view (its rule at the foot)")
    PNGS["7-arp"] = ap_.screen
    sp = Run(["button:shift@200+400", "knob:8@300:1"], 500)
    ok(sp.says[-1].startswith("Tempo 121"), "Shift + knob 8: Tempo (%s)" % sp.says)

    # ---- the buttons
    bt = Run(["button:copy@200+50", "button:loop@400+50", "button:mute@600+50", "button:up@800+50"], 1000)
    ok(bt.says[:4] == ["Lock, on", "Latch, on", "Bass, on", "Octave, plus 1"], "Copy LOCK, Loop LATCH, Mute bass, "
       "Up the octave: %s" % bt.says)
    ok(bt.led(71) == (255,) * 3 and bt.led(70) == (255,) * 3 and bt.led(74) == (255,) * 3 and bt.led(68) == (255,) * 3,
       "their LEDs lit")
    pn = Run(DM7[:1] + ["button:delete@400+50"], 700)
    ok("PANIC, all notes off" in pn.says, "Delete tapped alone: PANIC (%s)" % pn.says)
    lk = Run(["button:shift@200+300", "pad:25@300+100", "pad:1@1000+100", "pad:3@1300+100"], 1600)
    ok("Chord D minor" in lk.says and "Chord F minor" in lk.says, "Shift + MIN: LOCK, MIN latched (%s)" % lk.says)
    rc = Run(DM7 + ["step:0@1500+50", "button:play@1700+50"], 2000)
    ok(rc.led(48) == (0, 200, 0) and rc.led_when(32, 1720) == WHITE and rc.led(32) == SEQ and
       WHITE in [rc.led(32 + i) for i in range(1, 16)], "Play: the slot plays, Play lit green, the playhead (white) "
       "from step 1 on")
    PNGS["1c-home-playing"] = rc.screen
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
