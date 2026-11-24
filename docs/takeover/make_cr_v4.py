#!/usr/bin/env python3
"""ChoralRoot takeover, proposal 4 (the owner's notes of 2026-11-21): the FM-1's menu language on the Move.
Shift + a step is one of the FM-1's buttons (tap: on / off, held: its settings, the wheel turns their pages), one
layer of knobs (no Shift page), the wheel the sound (click: preset <-> engine, Shift: the bass), Loop held the slots,
a full isomorphic layout and a sound editor layout (its groups where the memories are).
    python3 make_cr_v4.py <rfm1cs font5x7.c> <out.json> [x0 y0]"""
import json, os, random, sys

FONT, OUT = sys.argv[1], sys.argv[2]
X0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0
Y0 = int(sys.argv[4]) if len(sys.argv) > 4 else 0
HERE = os.path.dirname(os.path.abspath(__file__))


def borrow(fn, stop):
    """a generator's helpers, without its main()"""
    sys.argv = [fn, FONT, os.devnull]
    g = {"__name__": "borrowed", "__file__": os.path.join(HERE, fn)}
    exec(open(os.path.join(HERE, fn)).read().split(stop)[0], g)
    return g


V = borrow("make_cr_v2.py", "\ndef main():")
T = borrow("make_cr_v3.py", "\nPLAY = [")
M = V["M"]
dim, hexc, pack = M.dim, V["hexc"], V["pack"]
big, chord, lbl, rlbl, clbl, dashes = T["big"], T["chord"], T["lbl"], T["rlbl"], T["clbl"], T["dashes"]
random.seed(20261122)
uid = lambda: "".join(random.choice("abcdefghijklmnopqrstuvwxyz0123456789") for _ in range(12))

WHITE, YELLOW, KEY_D, MEMC, SEQC = V["WHITE"], V["YELLOW"], V["KEY_D"], V["MEMC"], V["SEQC"]
VIOLET, ORANGE, BASSC, PROGC = (196, 58, 255), (255, 120, 0), (255, 90, 30), (150, 90, 255)
GREEN, CYAN, RED = (40, 220, 90), (19, 171, 191), (255, 40, 40)

# ---- Shift + a step: the FM-1's buttons (tap: on / off; held: the settings)
SC = [  # label, colour, on in the pictures, tap, held
    ("KEY", YELLOW, True, "Key Mode on / off", "The KEY settings: tonic, scale, Keyboard, Numerals, Snap"),
    ("LOCK", VIOLET, False, "LOCK on / off (the chord keys held now stay latched; the roots play them)",
     "The LOCK settings: Ext keys Toggle / Pushed, Release"),
    ("LATCH", WHITE, False, "LATCH on / off (what you play keeps sounding)", "-"),
    ("PERF", VIOLET, True, "Chord Perform on / off", "The PERF settings: the mode and its pages"),
    ("B.PRF", BASSC, False, "Bass Perform on / off", "The Bass PERF settings"),
    ("FX", GREEN, True, "The effects on / off", "The FX settings: an effect a page"),
    ("BASS", BASSC, True, "The bass on / off", "The BASS settings: behaviour, level, glide"),
    ("BEAT", RED, False, "The beat (metronome or drums) on / off", "The beat's settings: Metronome / Drums and their pages"),
    ("EDIT", WHITE, False, "The sound editor on the chord sound (the Editor layout)", "The engine picker"),
    ("B.EDIT", BASSC, False, "The sound editor on the bass sound", "The bass engine picker"),
]
OPT = ("OPTIONS", WHITE, False, "The Options (MIDI, hold time, velocity, ...)", "-")


def scut(i):
    return SC[i] if i < len(SC) else OPT if i == 15 else None


# ---- the screens (proposal 3's style: the chord big, a small label, nothing else)
def scr(fn):
    s = M.Scr()
    fn(s)
    return s


def s_home(s):
    chord(s, 4, 6, "D", "m", "7")
    lbl(s, 4, 50, "D  F  A  C")


def s_engine(s):
    lbl(s, 0, 0, "Engine")
    rlbl(s, 0, "3/7")
    e = M.Scr()
    w = big(e, 0, 0, "FM TONE", 20)
    big(s, 64 - w // 2, 20, "FM TONE", 20)
    dashes(s, 7, 2, w=12)


def s_preset(s):
    lbl(s, 0, 0, "FM TONE")
    rlbl(s, 0, "17")
    e = M.Scr()
    w = big(e, 0, 0, "Glass Pad", 18)
    big(s, 64 - w // 2, 22, "Glass Pad", 18)
    clbl(s, 50, "preset")


def s_bass(s):
    s.rect(0, 0, 52, 14)
    s.text(2, 0, "Bass", 2, 0)
    rlbl(s, 0, "VA 17")
    e = M.Scr()
    w = big(e, 0, 0, "Deep Sub", 20)
    big(s, 64 - w // 2, 22, "Deep Sub", 20)


def s_shift(s):
    for i in range(8):
        lab, _, on, _, _ = SC[i]
        x, y = (i % 2) * 66, (i // 2) * 16
        if on:
            s.rect(x, y, 62, 15)
            s.text(x + 1, y + 1, lab, 2, 0)
        else:
            s.text(x + 1, y + 1, lab, 2)


def s_perf(s):
    lbl(s, 0, 0, "PERF")
    s.rect(98, 0, 30, 14)
    s.text(102, 0, "on", 2, 0)
    e = M.Scr()
    w = big(e, 0, 0, "Arp", 30)
    big(s, 64 - w // 2, 18, "Arp", 30)
    dashes(s, 3, 0, w=18, gap=6)


def s_perf_knob(s):
    clbl(s, 0, "Division")
    e = M.Scr()
    w = big(e, 0, 0, "1/16", 28)
    big(s, 64 - w // 2, 18, "1/16", 28)
    dashes(s, 8, 4)


def s_loop(s):
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
    clbl(s, 50, "120 bpm")


def s_iso(s):
    lbl(s, 0, 0, "Iso")
    s.rect(98, 0, 30, 14)
    s.text(100, 0, "LK", 2, 0)
    chord(s, 30, 12, "A", "m", "7", h=32)
    lbl(s, 30, 50, "A C E G")


def s_editor(s):
    lbl(s, 0, 0, "VA")
    rlbl(s, 0, "1/2")
    e = M.Scr()
    w = big(e, 0, 0, "OSC", 30)
    big(s, 64 - w // 2, 16, "OSC", 30)
    clbl(s, 50, "Osc 1 + 2")


def s_editor_knob(s):
    clbl(s, 0, "Osc 2 Fine")
    e = M.Scr()
    w = big(e, 0, 0, "+7", 30)
    big(s, 64 - w // 2, 18, "+7", 30)
    s.box(10, 58, 108, 5)
    s.rect(64, 59, 18, 3)


# ---- the knobs
KN_HOME = [("Voicing", ORANGE), ("Bass reg.", BASSC), ("Perf amount", VIOLET), ("FX send", GREEN),
           ("Key", YELLOW), ("Scale", YELLOW), ("Transpose", YELLOW), ("Tempo", WHITE)]
KN_HOME_W = {"Voicing": "The chord's voicing, -12..+12 (the FM-1's KNOB 1)",
             "Bass reg.": "The bass register, -2..+4 octaves (KNOB 2)",
             "Perf amount": "The performance mode's main value: Rate, Amount, Division, the pattern (KNOB 3)",
             "FX send": "The chord part's send into the selected effect, % (KNOB 4)",
             "Key": "The tonic (Key Mode on)", "Scale": "The Key Mode scale (all 23)",
             "Transpose": "-24..+24 semitones, everything played", "Tempo": "20..300 BPM (the FM-1's SELECT)"}
KN_PERF = [("Mode", VIOLET), ("Division", VIOLET), ("Direction", VIOLET), ("Octaves", VIOLET),
           ("Gate", VIOLET), ("Swing", VIOLET), ("Velocity", VIOLET), ("Hold", VIOLET)]
KN_LOOP = [("Tempo", WHITE), ("Rec mode", RED), ("Quantize", RED), ("Count-in", RED),
           ("Sync", SEQC), ("Length", SEQC), ("Level", SEQC), ("Rate", SEQC)]
KN_EDIT = [("Osc 1 Wave", CYAN), ("Osc 1 Level", ORANGE), ("Osc 1 Coarse", WHITE), ("Osc 1 Fine", GREEN),
           ("Osc 2 Wave", CYAN), ("Osc 2 Level", ORANGE), ("Osc 2 Coarse", WHITE), ("Osc 2 Fine", GREEN)]


def knobs(c, kn, where, words=None):
    for i, (lab, col) in enumerate(kn):
        c[f"knob-{i + 1}"] = {"meta": {"label": lab.upper(), "function": (words or {}).get(lab, f"{where}: {lab}"),
                                       "turn": f"Change {lab}", "touch": f"{lab}, then its value, full screen",
                                       "shiftTurn": "Nothing: one layer of knobs (Shift is for the steps)"}}
        c[f"knobled-{i + 1}"] = {"led": {"color": hexc(col)}}


# ---- what changes everywhere
def common(c, shift=False, wheel_engine=False):
    m = lambda **k: {"meta": k}
    c["wheel"] = m(label="SOUND" if not wheel_engine else "ENGINE",
                   function="Home: the chord sound. Click: turning picks the preset <-> the engine (the screen says which). "
                            "In a settings layer: its pages. In the Editor: its screens and lanes.",
                   turn="The chord preset (or engine, after a click)", click="Preset <-> engine",
                   shiftTurn="The bass sound (Shift + click: the bass preset <-> engine)", touch="The sound's name")
    c["shift"] = {**m(label="SHIFT", function="Held: the steps are the FM-1's buttons (tap: on / off; held: the settings). "
                                              "Shift + the wheel: the bass sound. Shift + a mod: LOCK it.",
                      hold="The shortcuts on the steps and the screen"), "led": {"brightness": 1 if shift else 0.3}}
    c["loop"] = {**m(label="LOOP", function="Tap: LATCH on / off. Held: the looper (the FM-1's PLAY menu): the slots on the "
                                            "steps, the loop's settings on the knobs.",
                     click="Latch on / off", hold="The slots and the loop settings"), "led": {"brightness": 0.3}}
    c["copy"] = {**m(label="LOCK", function="LOCK on / off (as Shift + step 2). Lit while on; the screen shows LK.",
                     click="Lock on / off"), "led": {"brightness": 0.3}}
    c["sample"] = {**m(label="FAV", function="The chord sound's favourite mark (was the wheel's click).",
                       click="Favourite on / off"), "led": {"brightness": 0.3}}
    c["back"]["meta"]["function"] = "Closes a settings layer, the Editor, a popup. Touch Volume + Back: FM-1 mode."
    return c


def steps_seq(play=4):
    return V["steps"](play=play)


def ctl(padmap, stepmap, kn, where, screen_meta, words=None, shift=False, menu=False, padmeta=None, wheel_engine=False):
    c = V["controls"](padmap, stepmap, kn, "x", screen_meta, shift=False, menu_held=menu, padmeta=padmeta)
    knobs(c, kn, where, words)
    common(c, shift, wheel_engine)
    for i in range(16):
        c[f"stepicon-{i + 1}"] = {"led": {"brightness": 0}}
    if shift:
        for i in range(16):
            s = scut(i)
            if not s:
                c[f"step-{i + 1}"] = {"meta": {"label": "", "function": "Free"}, "led": {"color": None}}
                continue
            lab, col, on, tap, held = s
            c[f"step-{i + 1}"] = {"meta": {"label": lab, "function": f"Shift + step {i + 1}: {lab}",
                                           "click": tap, "hold": held,
                                           "description": "Lit: on, dim: off (as the FM-1's button lights)."},
                                  "led": {"color": hexc(col if on else dim(col, 0.18))}}
        c["shift"]["led"] = {"brightness": 1}
    return c


def layout(x, y, name, desc, s, c, frames=None):
    return V["layout"](x, y, name, desc, s, c, frames)


# ---- the pads
def pads_iso(pressed=()):
    """all 32 pads: rows a 4th apart (Move's own), the bottom-left C; in-key lit, the tonic yellow"""
    c, meta = {}, {}
    sc = {0, 2, 4, 5, 7, 9, 11}
    for r in range(4):              # r 0 = the bottom row
        for k in range(8):
            n = r * 5 + k
            pid = f"pad-{4 - r}-{k + 1}"
            pc = n % 12
            col = WHITE if n in pressed else YELLOW if pc == 0 else KEY_D if pc in sc else (20, 20, 24)
            c[pid] = (V["NN"][pc], col)
            meta[pid] = {"label": V["NN"][pc], "function": "A root: its chord, of the LOCKed mods (or Key Mode's chord); none: a single note",
                         "click": "Play it", "description": "Rows a 4th apart, as the Move's own layout. Up / Down: the octave, Left / Right: a semitone."}
    return c, meta


EDG = ["OSC", "FILT", "ENV", "LFO", "MOD", "FX", "MIX", "CHORD"]


def pads_editor(group=0):
    c = V["pads"]()
    meta = {}
    for j, g in enumerate(EDG):
        pid = f"pad-{1 + j // 4}-{5 + j % 4}"
        if j == 7:
            c[pid] = ("CHORD\nbass", dim(BASSC, 0.5))
            meta[pid] = {"label": "CHORD / BASS", "function": "Which sound the editor edits: the chord's or the bass's",
                         "click": "Chord <-> bass"}
        else:
            c[pid] = (g, WHITE if j == group else dim(CYAN, 0.7))
            meta[pid] = {"label": g, "function": f"The editor's {g} group (the FM-1's EDIT buttons: OSC FILT ENV LFO / MOD FX MIX)",
                         "click": "Open it; tapped again: its next screen",
                         "description": "Lit white: the group open; the screens remember where you were (as Evil Genius's page pads)."}
    return c, meta


def main():
    DX, DY = 1148, 920
    pos = lambda k: (X0 + (k % 3) * DX, Y0 + (k // 3) * DY)
    items = [{"id": uid(), "x": X0 - 60, "y": Y0 - 60, "w": 3 * DX + 860, "h": 3 * DY + 220, "type": "group",
              "title": "ChoralRoot takeover - proposal 4: the FM-1's menu language, Iso and Editor layouts, 2026-11-21",
              "color": "#c43aff"}]
    home_meta = {"label": "Chord view", "function": "The chord big, its notes; LK when LOCK is on."}
    L = []
    c = ctl(V["pads"](pressed=(2,), held_mods=(5,)), steps_seq(), KN_HOME, "Home", home_meta, words=KN_HOME_W)
    L.append(("A. Home: one layer of knobs, the wheel the sound",
              "The knobs are the FM-1's four (Voicing, Bass register, Perf amount, FX send) and Key, Scale, Transpose, Tempo; Shift does not change them. The wheel turns the chord preset.",
              scr(s_home), c, None,
              "Wheel: the chord preset. Wheel click: the wheel turns the engine instead (again: the preset). Shift + wheel: the bass sound (Shift + click: its engine).\n"
              "No Shift page of knobs any more: everything else is in the settings (Shift + a step held). Sample: favourite. Copy: LOCK (lit, LK on the screen)."))
    c = ctl(V["pads"](), steps_seq(), KN_HOME, "Home", {"label": "Engine", "function": "The wheel turns the engine: its name big, dashes for the 7."},
            words=KN_HOME_W, wheel_engine=True)
    L.append(("B. Wheel clicked: the engine",
              "After a click the wheel turns the chord engine (FM6, FM TONE, VA, CZ-1, P5, WHEEL, GRAIN); a second click: back to presets. Shift + wheel: the bass's (screen C).",
              scr(s_engine), c, [(scr(s_engine), 1500), (scr(s_preset), 1500), (scr(s_bass), 1500)],
              "Animated (hover): engine, then the preset after a click, then Shift + wheel: the bass sound (an inverted 'Bass' tag)."))
    c = ctl(V["pads"](), {i: ("", None) for i in range(16)}, KN_HOME, "Home", {"label": "Shortcuts", "function": "The eight toggles; lit (inverted) = on."},
            words=KN_HOME_W, shift=True)
    L.append(("C. Shift held: the FM-1's buttons on the steps",
              "Shift held: the steps are KEY, LOCK, LATCH, PERF, B.PRF (bass perf), FX, BASS, BEAT, EDIT, B.EDIT ... OPTIONS on 16. Tap: on / off (lit = on). Held: its settings (D).",
              scr(s_shift), c, None,
              "The FM-1's language: a tap is the button's on / off, a hold opens its settings, which stay open after you let go (Back or the same step held again closes).\n"
              "LOCK is now findable: Shift + step 2 (or Copy); Shift + a mod still LOCKs that mod. The loop slots moved to Loop held (E)."))
    c = ctl(V["pads"](), steps_seq(), KN_PERF, "PERF settings, Arp page", {"label": "PERF settings", "function": "PERF, on / off, the page big (the wheel turns it), dashes for the pages; a knob touched: name, then value."})
    c["wheel"]["meta"]["turn"] = "The PERF pages: Mode, Arp, Groove"
    L.append(("D. Shift + step 4 held: the PERF settings",
              "PERF's settings open (and stay): the wheel turns the page (Mode, Arp, Groove), the 8 knobs are the page's. Keys, mods, memories and steps play as ever.",
              scr(s_perf), c, [(scr(s_perf), 1500), (scr(s_perf_knob), 1500)],
              "Every settings layer works this way: KEY, LOCK, PERF, B.PERF, FX (a page an effect), BASS, BEAT (Metronome, Drums: Beat, Kick, Snare, Hats, Mix), OPTIONS.\n"
              "Back closes; so does holding the same step again. Shift + another step held: straight to that one."))
    sl = {}
    for i in range(16):
        if i < 10:
            full = i in (0, 1, 2, 5)
            sl[i] = (f"Slot {i + 1}", ORANGE if i == 2 else dim(SEQC, 0.6 if full else 0.12))
        else:
            sl[i] = ("", None)
    c = ctl(V["pads"](), sl, KN_LOOP, "Loop held", {"label": "Looper", "function": "The ten slots (filled: a loop), the slot big, the tempo."})
    for i in range(10):
        c[f"step-{i + 1}"]["meta"] = {"label": f"Slot {i + 1}", "function": "Loop held + a step: that slot (at the loop's end while playing)", "click": "Pick it"}
    c["loop"]["led"] = {"brightness": 1}
    L.append(("E. Loop held: the slots and the looper's settings",
              "Loop held: steps 1-10 the slots (3 playing), the knobs Tempo, Rec mode, Quantize, Count-in, Sync, Length, Level, Rate. Let go: back.",
              scr(s_loop), c, None,
              "The FM-1's PLAY menu. A slot picked while playing switches at the loop's end. An empty slot: see the open question (today it stops the loop)."))
    pi, pim = pads_iso(pressed=(9, 14))
    c = ctl(pi, steps_seq(), KN_HOME, "Home", {"label": "Iso view", "function": "Iso and LK; the chord big."}, words=KN_HOME_W, padmeta=pim)
    L.append(("F. Iso layout: all 32 pads",
              "All four rows are notes a 4th apart (the Move's own layout), C bottom left, in-key white, the tonic yellow. A root plays the LOCKed chord, or Key Mode's.",
              scr(s_iso), c, None,
              "Picked with Menu held like the others. No mod pads here: LOCK (Shift + step 2, or Shift + a mod in another layout) sets the chord type the pads play; with Key Mode on each pad plays its own chord.\n"
              "Alternative: the mods kept on the left 4 columns of the top two rows, Iso on the rest (say which)."))
    pe, pem = pads_editor(0)
    c = ctl(pe, steps_seq(), KN_EDIT, "Editor, OSC", {"label": "Editor", "function": "The engine, the group big, the screen; a knob touched: its name, then value."}, padmeta=pem)
    c["wheel"]["meta"]["turn"] = "The group's screens and lanes (the FM-1's SELECT)"
    L.append(("G. Editor layout: the sound editor",
              "Shift + step 9: the editor on the chord sound. The memories' pads are its groups (OSC FILT ENV LFO / MOD FX MIX, CHORD/bass); the mods and the keyboard play as ever. Knobs: two lanes, 8 values.",
              scr(s_editor), c, [(scr(s_editor), 1500), (scr(s_editor_knob), 1500)],
              "As Evil Genius's page pads: tap a group, tap it again for its next screen. The wheel steps the lanes. Back or Shift + step 9 leaves. Shift + step 10: the bass sound."))
    for k, (name, desc, s, c, frames, nt) in enumerate(L):
        x, y = pos(k)
        items.append(layout(x, y, name, desc, s, c, frames))
        items.append({"id": uid(), "x": x, "y": y + 640, "w": 1048, "h": 200, "type": "note", "text": nt, "color": "#e7c6ff"})
    items.append({"id": uid(), "x": X0 + 3 * DX, "y": Y0, "w": 760, "h": 420, "type": "note", "color": "#ffb3a7",
                  "text": "OWNER'S NOTES (2026-11-21) -> HERE\n- Lock: Shift + step 2 or Copy (C). Today it was Copy only, unlabelled.\n"
                          "- Loops stopping: switching to an EMPTY slot while playing stops at the loop's end (two full slots keep playing). Open: keep playing the old loop until you record, or run an empty bar?\n"
                          "- Step icons: FIXED (lit only for a loop longer than 16 steps).\n- Shift shortcuts, FM-1 menu language, wheel pages: C, D, E.\n"
                          "- No Shift knob layer: A.\n- Wheel = preset, click = engine, Shift = bass: A, B.\n- Full isomorphic: F.\n- Sound editor layout: G."})
    doc = {"format": "ableton-move-designer", "version": 3, "name": "ChoralRoot takeover, proposal 4", "items": items}
    json.dump(doc, open(OUT, "w"), indent=1)


main()
