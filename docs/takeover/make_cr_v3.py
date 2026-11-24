#!/usr/bin/env python3
"""ChoralRoot takeover, proposal 3: cleaner Move screens in the style of ChoralRoot's FM-1 screens (the chord big, its
type small and raised, little else). Writes standalone screen items for the Ableton Move Designer (format
'ableton-move-designer' v3), real 128 x 64 one-bit pictures: labels in the Move's font5x7 at 2x (the owner's minimum),
the big glyphs rasterised from a bold sans and thresholded to one bit.
    python3 make_cr_v3.py <rfm1cs font5x7.c> <out.json> [x0 y0] [pngdir]"""
import base64, json, os, random, sys
from PIL import Image, ImageDraw, ImageFont

FONT, OUT = sys.argv[1], sys.argv[2]
X0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0
Y0 = int(sys.argv[4]) if len(sys.argv) > 4 else 0
PNG = sys.argv[5] if len(sys.argv) > 5 else None
HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv = [sys.argv[0], FONT, os.environ.get("TMPDIR", "/tmp") + "/cr-v1-png"]
sys.path.insert(0, HERE)
import make_cr_mockups as M  # noqa: E402

BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
random.seed(20261121)
uid = lambda: "".join(random.choice("abcdefghijklmnopqrstuvwxyz0123456789") for _ in range(12))


def big(s, x, y, text, h, v=1):
    """text in a bold sans, cap height ~h px, top-left at x, y (one bit); returns the right edge"""
    f = ImageFont.truetype(BOLD, int(h * 1.40))
    l, t, r, b = f.getbbox(text)
    im = Image.new("L", (r - l + 2, int(h * 1.6) + 4), 0)
    ImageDraw.Draw(im).text((-l, -f.getbbox("H")[1]), text, font=f, fill=255)
    for j in range(im.height):
        for i in range(im.width):
            if im.getpixel((i, j)) >= 128:
                s.set(x + i, y + j, v)
    return x + (r - l)


def lbl(s, x, y, t, v=1):
    s.text(x, y, t, 2, v)


def rlbl(s, y, t, v=1):
    s.text(127 - M.Scr.tw(t, 2), y, t, 2, v)


def clbl(s, y, t, v=1):
    s.ctext(64, y, t, 2, v)


def chord(s, x, y, root, small="", sup="", h=34):
    """the FM-1's chord: the root big, a small part beside it at the foot, the extension raised"""
    e = big(s, x, y, root, h)
    if small:
        e = big(s, e + 1, y + h - int(h * .55), small, int(h * .55))
    if sup:
        big(s, e + 2, y, sup, int(h * .42))
    return e


def dashes(s, n, cur, y=58, w=10, gap=4):
    tot = n * w + (n - 1) * gap
    x = 64 - tot // 2
    for i in range(n):
        (s.rect if i == cur else s.box)(x + i * (w + gap), y, w, 3)


def scr(fn):
    s = M.Scr()
    fn(s)
    return s


# ---- playing
def home(s):
    chord(s, 4, 6, "D", "m", "7")
    lbl(s, 4, 50, "D  F  A  C")


def lock(s):
    chord(s, 4, 6, "D", "m", "6/9")
    lbl(s, 4, 50, "D F A B E")
    s.rect(98, 0, 30, 14)
    s.text(100, 0, "LK", 2, 0)


def keymode(s):
    lbl(s, 0, 0, "Key C")
    chord(s, 62, 12, "E", "m", h=32)
    lbl(s, 62, 50, "E G B")


def arp(s):
    rlbl(s, 0, "1/8")
    chord(s, 2, 2, "A", "m", "7", h=20)
    xs = [4, 36, 68, 100]
    for i, (x, n) in enumerate(zip(xs, ["A", "C", "E", "G"])):
        if i == 2:
            s.rect(x - 4, 34, 22, 22)
            s.text(x + 2, 38, n, 2, 0)
        else:
            s.text(x + 2, 44, n, 2)
    s.hline(0, 61, 128)


def seq(s):
    lbl(s, 0, 0, "1")
    rlbl(s, 0, "5")
    chord(s, 30, 4, "G", "", "7", h=32)
    fill = {0, 4, 8, 12, 14}
    for i in range(16):
        x = i * 8
        if i == 4:
            s.rect(x, 50, 7, 14)
        elif i in fill:
            s.rect(x, 55, 7, 7)
        else:
            s.box(x, 55, 7, 7)


def rec(s):
    lbl(s, 0, 0, "Rec")
    rlbl(s, 0, "2.3")
    chord(s, 44, 12, "F", h=32)
    s.rect(0, 60, 74, 4)
    s.box(74, 60, 54, 4)


def prog(s):
    lbl(s, 0, 0, "Neo Soul")
    chord(s, 4, 18, "A", "m", "11", h=30)
    rlbl(s, 50, "4/8")


def mem(s):
    lbl(s, 0, 0, "M6")
    chord(s, 30, 12, "F", "", "sus", h=32)
    lbl(s, 30, 50, "F Bb C")


def voicing(s):
    chord(s, 4, 2, "D", "", "M7", h=26)
    lbl(s, 4, 36, "A C# D F#")
    lbl(s, 4, 50, "voicing +2")


# ---- menus and popups
def entry(s):
    e = M.Scr()
    w = big(e, 0, 0, "ChoralRoot", 15)
    big(s, 64 - w // 2, 12, "ChoralRoot", 15)
    clbl(s, 42, "Takeover")


def pop_range(s):
    clbl(s, 0, "Voicing")
    e = M.Scr()
    w = big(e, 0, 0, "+2", 30)
    big(s, 64 - w // 2, 18, "+2", 30)
    s.box(10, 58, 108, 5)
    s.rect(11, 59, 60, 3)


def pop_list(s):
    clbl(s, 0, "Perform")
    e = M.Scr()
    w = big(e, 0, 0, "Arp", 30)
    big(s, 64 - w // 2, 18, "Arp", 30)
    dashes(s, 8, 5)


def saved(s):
    chord(s, 2, 12, "D", "m", "7", h=32)
    s.rect(84, 8, 44, 48)
    big(s, 90, 14, "M6", 18, 0)
    s.text(91, 40, "ok", 2, 0)


def step(s):
    lbl(s, 0, 0, "Step 9")
    rlbl(s, 0, "x2")
    chord(s, 4, 18, "C", "", "maj7", h=28)
    lbl(s, 0, 50, "2 st")
    rlbl(s, 50, "75%")


def step_empty(s):
    lbl(s, 0, 0, "Step 10")
    s.rect(52, 28, 24, 5)
    clbl(s, 50, "play chord")


def slots(s):
    full = {0, 1, 2, 5}
    for i in range(10):
        x = i * 13
        if i == 2:
            s.rect(x, 0, 11, 11)
        elif i in full:
            s.box(x, 0, 11, 11)
            s.rect(x + 3, 3, 5, 5)
        else:
            s.box(x, 0, 11, 11, dotted=True)
    e = M.Scr()
    w = big(e, 0, 0, "Loop 3", 22)
    big(s, 64 - w // 2, 18, "Loop 3", 22)
    clbl(s, 50, "Overdub")


def picker(s):
    rlbl(s, 0, "B1")
    e = M.Scr()
    w = big(e, 0, 0, "Keys", 30)
    big(s, 64 - w // 2, 16, "Keys", 30)
    dashes(s, 4, 0, w=18, gap=6)


def panic(s):
    s.rect(0, 0, 128, 64)
    e = M.Scr()
    w = big(e, 0, 0, "PANIC", 22)
    big(s, 64 - w // 2, 8, "PANIC", 22, 0)
    s.ctext(64, 48, "all off", 2, 0)


PLAY = [("1 Home", "The chord and its notes, nothing else (FM-1 Chord view).", home),
        ("2 Lock", "A mod locked (Copy): a lit LK tag top right.", lock),
        ("3 Scale layout", "Key Mode: the key top left; E plays Em.", keymode),
        ("4 Arp", "The sounding note hops on a block, the division top right.", arp),
        ("5 Sequencer", "Slot top left, step top right, 16 steps at the foot; the playhead stands tall.", seq),
        ("6 Recording", "'Rec' and bar.beat; the take's progress as a line at the foot.", rec),
        ("7 Prog", "The progression's name; chord 4 of 8.", prog),
        ("8 Memory played", "Its number top left.", mem),
        ("9 Voicing turned", "The revoiced notes and the voicing, for a second.", voicing)]
MENUS = [("10 Entering", "Track 1: the firmware's name over 'Takeover'; said 'ChoralRoot, takeover'.", entry),
         ("11 Knob, a range", "Name, the value big, a thin bar.", pop_range),
         ("12 Knob, a list", "Name, the entry big, dashes for the place (4 clicks an entry).", pop_list),
         ("13 Memory saved", "The chord and a lit block with the memory's number.", saved),
         ("14 Step held", "Its chord; ratchet, length and chance only when not the default.", step),
         ("15 Empty step held", "A dash and the one thing to do.", step_empty),
         ("16 Shift: slots", "The ten slots (filled: a loop), the slot big, the record mode.", slots),
         ("17 Menu held", "The layout big, dashes for the four, the bank top right.", picker),
         ("18 PANIC", "Delete tapped: the whole screen lit.", panic)]


def item(x, y, name, desc, s):
    return {"id": uid(), "x": x, "y": y, "w": 560, "h": 340, "type": "screen", "name": name, "description": desc,
            "screen": M_pack(s)}


def M_pack(s):
    b = bytearray(1024)
    for yy in range(64):
        for xx in range(128):
            if s.px[yy][xx]:
                b[yy * 16 + xx // 8] |= 0x80 >> (xx % 8)
    return base64.b64encode(bytes(b)).decode()


def main():
    DX, DY = 600, 380
    items = [{"id": uid(), "x": X0 - 60, "y": Y0 - 120, "w": 9 * DX + 80, "h": 2 * DY + 200, "type": "group",
              "title": "ChoralRoot takeover - proposal 3: cleaner screens (FM-1 style), 2026-11-21", "color": "#ff7800"}]
    for row, lst in enumerate((PLAY, MENUS)):
        for k, (name, desc, fn) in enumerate(lst):
            s = scr(fn)
            items.append(item(X0 + k * DX, Y0 + row * DY, name, desc, s))
            if PNG:
                os.makedirs(PNG, exist_ok=True)
                s.image(4).save(os.path.join(PNG, "v3-" + name.split()[0].zfill(2) + ".png"))
    doc = {"format": "ableton-move-designer", "version": 3, "name": "ChoralRoot takeover, proposal 3", "items": items}
    json.dump(doc, open(OUT, "w"), indent=1)


main()
