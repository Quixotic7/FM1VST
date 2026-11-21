#!/usr/bin/env python3
"""ChoralRoot takeover as built (2026-11-21): the Move Designer document made from the firmware itself. Every screen
and every LED is the emulator's (rfm1_run --tk, the build under test), so the document and the Move cannot drift;
the controls' words are proposal 4's (make_cr_v4.py), corrected to what was built.
    python3 make_cr_v5.py <rfm1cs font5x7.c> <out.json> --run <rfm1_run> --file <choralroot.rfm1> [--x0 X --y0 Y]"""
import argparse, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ap = argparse.ArgumentParser()
ap.add_argument("font")
ap.add_argument("out")
ap.add_argument("--run", required=True)
ap.add_argument("--file", required=True)
ap.add_argument("--x0", type=int, default=0)
ap.add_argument("--y0", type=int, default=0)
ARGS = ap.parse_args()
X0, Y0 = ARGS.x0, ARGS.y0

sys.path.insert(0, os.path.join(ROOT, "tools", "rfm1"))
import rfm1_cr_tk_test as T  # noqa: E402
T.A = argparse.Namespace(run=ARGS.run, file=ARGS.file, png=None)
T.GLYPH = T.font()

sys.argv = ["make_cr_v4.py", ARGS.font, os.devnull]
V4 = {"__name__": "borrowed", "__file__": os.path.join(HERE, "make_cr_v4.py")}
_cwd = os.getcwd()
os.chdir(HERE)
exec(open("make_cr_v4.py").read().split("\ndef main():")[0], V4)
os.chdir(_cwd)
V, M = V4["V"], V4["M"]
V4["random"].seed(20261123)                        # (ids of its own: v4's seed would repeat its ids)
hexc, uid, ctl, knobs = V4["hexc"], V4["uid"], V4["ctl"], V4["knobs"]
VIOLET, ORANGE, BASSC, GREEN, CYAN, RED = (V4[k] for k in ("VIOLET", "ORANGE", "BASSC", "GREEN", "CYAN", "RED"))
WHITE, YELLOW, SEQC = V4["WHITE"], V4["YELLOW"], V4["SEQC"]

DM7 = T.DM7
S = "button:shift@200+%d"
WHITES = {"shift": 63, "menu": 64, "back": 65, "capture": 66, "down": 67, "up": 68, "undo": 69, "loop": 70, "copy": 71,
          "left": 72, "right": 73, "mute": 74, "delete": 75}


def run(ev, end):
    r = T.Run(ev, end)
    assert r.rc == 0 and len(r.screen) == 64, "the emulator failed on %s" % ev
    return r


def scr(r):
    s = M.Scr()
    for y, row in enumerate(r.screen):
        for x, c in enumerate(row[:128]):
            s.px[y][x] = 1 if c == "1" else 0
    return s


def leds(c, r):
    """the build's LEDs over the controls (the words stay)"""
    def put(key, col, kind="color"):
        e = c.setdefault(key, {})
        if kind == "color":
            e["led"] = {"color": hexc(col) if any(col) else None}
        else:
            e["led"] = {"brightness": round(max(col) / 255, 2)}
    for rr in range(1, 5):
        for k in range(1, 9):
            put(f"pad-{rr}-{k}", r.led(T.pad(rr, k)))
    for i in range(16):
        put(f"step-{i + 1}", r.led(32 + i))
        put(f"stepicon-{i + 1}", r.led(76 + i), "white")
    for i in range(8):
        put(f"knobled-{i + 1}", r.led(55 + i))
    for k, i in WHITES.items():
        put(k, r.led(i), "white")
    put("play", r.led(48))
    put("record", r.led(49))
    put("sample", r.led(50), "white")
    return c


def labels_from_leds(c):
    """the pads' and steps' labels where the build shows nothing (an empty memory, an empty step) cleared"""
    for rr in (1, 2):
        for k in range(5, 9):
            e = c.get(f"pad-{rr}-{k}", {})
            if e.get("led", {}).get("color") in (None, hexc(T.pct(T.MEM, 18))):
                e.setdefault("meta", {})["label"] = "M%d" % ((rr - 1) * 4 + k - 4)
    for i in range(16):
        e = c.get(f"step-{i + 1}", {})
        if e.get("led", {}).get("color") != hexc(SEQC) and e.get("meta", {}).get("label", "") in V["SEQ"].values():
            e.setdefault("meta", {})["label"] = ""          # (the mockup's sequence: the build's steps are empty)
    return c


# ---- the knobs as built
KN_HOME = V4["KN_HOME"]
KN_PERF = [("Rate", VIOLET), ("Direction", VIOLET), ("Range", VIOLET), ("Hold", VIOLET)]
KN_LOOP = [("Length", SEQC), ("Rate", SEQC), ("Quantize", RED), ("Count-in", RED), ("Level", SEQC), ("Beat", RED),
           ("Rec mode", RED), ("Tempo", WHITE)]
KN_EDIT = [("Level", CYAN), ("Coarse", CYAN), ("Fine", CYAN), ("Detune", CYAN),
           ("Level", CYAN), ("Coarse", CYAN), ("Fine", CYAN), ("Detune", CYAN)]


def knobs_as_built(c, kn, where, words=None, note=""):
    knobs(c, kn, where, words)
    for i in range(len(kn), 8):
        c[f"knob-{i + 1}"] = {"meta": {"label": "", "function": "Nothing here: the FM-1's settings pages have four knobs"}}
    if note:
        for i in range(len(kn)):
            c[f"knob-{i + 1}"]["meta"]["description"] = note
    return c


def main():
    DX, DY = 1148, 920
    pos = lambda k: (X0 + (k % 3) * DX, Y0 + (k // 3) * DY)
    L = []
    items = [{"id": uid(), "x": X0 - 60, "y": Y0 - 60, "w": 3 * DX + 860, "h": 4 * DY + 220, "type": "group",
              "title": "ChoralRoot takeover - as built (proposal 4 + proposal 3's screens), from the firmware, 2026-11-21",
              "color": "#28dc5a"}]
    words = V4["KN_HOME_W"]

    # A. home
    held = ["pad:25@200+1200", "pad:17@205+1200", "pad:1@300+1000"]
    a = run(held, 1100)
    c = ctl(V["pads"](pressed=(1,), held_mods=(1, 5)), V4["steps_seq"](), KN_HOME, "Home",
            {"label": "Chord view", "function": "The chord big, its notes; Key C / Iso / M6 top left when they say "
                                                "something; LK when LOCK is on; playing: slot, step and the 16 steps."},
            words=words)
    fr = [(scr(a), 1500), (scr(run([T.KEY_ON] + DM7, 2000)), 1500), (scr(run(["button:copy@200+50"] + DM7, 2600)), 1500),
          (scr(run(DM7 + ["step:0@1500+50", "step:4@1550+50", "button:play@1700+50"], 2600)), 1500)]
    L.append(("A. Home: the chord, one layer of knobs, the wheel the sound",
              "MIN + m7 held and D: Dm7, its notes. The knobs: Voicing, Bass reg., Perf amount, FX send, Key, Scale, "
              "Transpose, Tempo; Shift does not change them.",
              scr(a), leds(c, a), fr,
              "Animated (hover): home; Key Mode (Key C top left); LOCK (the LK tag); playing (slot 1, the step, the 16 "
              "steps, the playhead tall).\nWheel: the chord preset; click: the engine; Shift + wheel: the bass. Sample: "
              "favourite. Copy: LOCK."))

    # B. the wheel
    e1, p1, b1 = run(["button:jogclick@200+50", "knob:0@500:1"], 800), run(["knob:0@500:1"], 800), \
        run([S % 600, "knob:0@400:1"], 900)
    c = ctl(V["pads"](), V4["steps_seq"](), KN_HOME, "Home", {"label": "Sound", "function": "Preset: the engine top "
            "left, its number, the name big. Engine: n / 7, the name, a dash each. Bass: an inverted tag."},
            words=words, wheel_engine=True)
    L.append(("B. The wheel: preset, engine (clicked), bass (Shift)",
              "The wheel turns the chord preset; a click makes it the engine (FM6, FM TONE, VA, CZ-1, P5, WHEEL, GRAIN); "
              "Shift + wheel the bass sound, Shift + click its engine.",
              scr(p1), leds(c, p1), [(scr(p1), 1500), (scr(e1), 1500), (scr(b1), 1500),
                                     (scr(run([S % 900, "button:jogclick@300+50", "knob:0@500:1"], 1000)), 1500)],
              "Animated (hover): preset, engine, bass preset, bass engine. Each shows for 1.5 s after a turn, then home."))

    # C. Shift held
    sh = run([S % 900], 1000)
    c = ctl(V["pads"](), {i: ("", None) for i in range(16)}, KN_HOME, "Home",
            {"label": "Shortcuts", "function": "The eight toggles; inverted = on."}, words=words, shift=True)
    L.append(("C. Shift held: the FM-1's buttons on the steps",
              "KEY, LOCK, LATCH, PERF, B.PRF, FX, BASS, BEAT, EDIT, B.EDIT ... OPTIONS on 16. Tap: on / off (lit = on). "
              "Held: its settings (D).", scr(sh), leds(c, sh), None,
              "A tap is the button's on / off, a hold opens its settings, which stay open after you let go (Back or the "
              "same step held again closes)."))

    # D. PERF settings
    pf = run([S % 900, "step:3@250+600"], 1400)
    c = ctl(V["pads"](), V4["steps_seq"](), KN_PERF, "PERF settings", {"label": "PERF settings", "function":
            "PERF, on / off, the page big (the wheel turns it), a dash each; a knob touched: name, then value."})
    knobs_as_built(c, KN_PERF, "PERF settings", note="The page's knob (the FM-1's KNOB 1-4 of this page)")
    c["wheel"]["meta"]["turn"] = "The PERF pages: the modes (Strum, Strum 2 Octaves, Slop, Arp ...)"
    L.append(("D. Shift + step 4 held: the PERF settings",
              "PERF's settings open (and stay): the wheel turns the page, knobs 1-4 are the page's four (5-8 dark, the "
              "FM-1's pages have four). Keys, mods, memories and steps play as ever.",
              scr(pf), leds(c, pf), [(scr(pf), 1500), (scr(run([S % 900, "step:3@250+600", "knob:2@1500:1"], 1700)), 1500),
                                     (scr(run([S % 900, "step:5@250+600"], 1400)), 1500)],
              "Animated (hover): PERF, a knob turned (Direction Up), FX (on: the inverted tag). Every settings layer "
              "works this way: KEY, LOCK, PERF, B.PERF, FX, BASS, BEAT, OPTIONS."))

    # E. Loop held
    lh = run(["button:loop@200+900"], 800)
    sl = {i: (f"Slot {i + 1}" if i < 10 else "", None) for i in range(16)}
    c = ctl(V["pads"](), sl, KN_LOOP, "Loop held", {"label": "Looper", "function": "The ten slots (filled: a loop, "
            "dotted: empty), the slot big, the tempo."})
    knobs_as_built(c, KN_LOOP, "Loop held")
    for i in range(10):
        c[f"step-{i + 1}"]["meta"] = {"label": f"Slot {i + 1}", "function": "Loop held + a step: that slot (at the "
                                      "loop's end while playing; an empty one runs an empty bar)", "click": "Pick it"}
    L.append(("E. Loop held: the slots and the looper's settings",
              "Steps 1-10 the slots, the knobs Length, Rate, Quantize, Count-in, Level, Beat, Rec mode, Tempo (the FM-1's "
              "PLAY menu). A tap of Loop alone: LATCH.", scr(lh), leds(c, lh), None,
              "A slot picked while playing switches at the loop's end; an empty slot runs a 16-step bar, so playback "
              "never stops (the owner, 2026-11-21)."))

    # F. Iso
    iso = run(["button:menu@200+400", "pad:16@300+50", "pad:20@900+900"], 1500)
    pi, pim = V4["pads_iso"](pressed=(9,))
    c = ctl(pi, V4["steps_seq"](), KN_HOME, "Home", {"label": "Iso view", "function": "Iso top left, the chord big."},
            words=words, padmeta=pim)
    L.append(("F. Iso layout: all 32 pads",
              "All four rows are notes a 4th apart (the Move's own layout), C bottom left; a root plays the LOCKed chord "
              "or Key Mode's. Shift held: the chord keys come back, to LOCK one.", scr(iso), leds(c, iso), None,
              "Picked with Menu held + the first pad of the second row."))

    # G. the editor
    ed = run([S % 300, "step:8@250+50"], 1100)
    pe, pem = V4["pads_editor"](0)
    c = ctl(pe, V4["steps_seq"](), KN_EDIT, "Editor, OSC", {"label": "Editor", "function": "The engine top left, the "
            "group big, the screen's name."}, padmeta=pem)
    knobs_as_built(c, KN_EDIT, "Editor, OSC", note="Knobs 1-4 the lane on the wheel, 5-8 the lane under it")
    c["wheel"]["meta"]["turn"] = "The group's screens and lanes (the FM-1's SELECT)"
    L.append(("G. Editor layout: the sound editor",
              "Shift + step 9: the editor on the chord sound (step 10: the bass). The memories' pads are its groups "
              "(OSC FILT ENV LFO / MOD FX MIX, chord / bass); the mods and keys play as ever.",
              scr(ed), leds(c, ed), [(scr(ed), 1500), (scr(run([S % 300, "step:8@250+50", "pad:29@900+50"], 1300)), 1500),
                                     (scr(run([S % 300, "step:8@250+50", "knob:8@900:3"], 1100)), 1500)],
              "Animated (hover): OSC, FILT (its pad tapped), a knob turned. A group tapped again: its next screen."))

    # H. Menu held
    mn = run(["button:menu@200+600"], 700)
    pm, pmm = V["pads_menu"](cur=0, prev=1, bank=0)
    c = ctl(pm, {i: ("", None) for i in range(16)}, KN_HOME, "Home", {"label": "Picker", "function": "The layout big, "
            "a dash each, the bank top right."}, words=words, menu=True, padmeta=pmm)
    L.append(("H. Menu held: layouts, banks, progressions",
              "The mods' block: the layouts (Keys, Scale, Chrom, Prog; Iso under Keys), the memories' block the 8 banks, "
              "the two bottom rows the progressions.", scr(mn), leds(c, mn), None,
              "A quick tap of Menu: the previous layout."))

    # I. a step held
    st = run(DM7 + ["step:0@1500+50", "step:8@2000+2000", "pad:24@2100+300", "pad:1@2150+200", "knob:4@2500:4",
                    "knob:1@2550:1"], 3800)
    c = ctl(V["pads"](), V4["steps_seq"](), V["KN_STEP"], "Step held", {"label": "Step", "function": "Step n top left, "
            "the ratchet top right, the chord big; length and chance only when not the default."})
    L.append(("I. A step held: its chord, the step's knobs",
              "Step 9 held: D + DIM wrote Ddim; Ratchet x2, Note len 2 steps. An empty step: a dash, 'play chord'.",
              scr(st), leds(c, st), [(scr(st), 1500), (scr(run(["step:9@200+900"], 1000)), 1500)],
              "The step's knobs: Note len, Chance, Velocity, Ratchet, Condition ..."))

    for k, (name, desc, s, c, frames, nt) in enumerate(L):
        x, y = pos(k)
        labels_from_leds(c)
        items.append(V4["layout"](x, y, name, desc, s, c, frames))
        items.append({"id": uid(), "x": x, "y": y + 640, "w": 1048, "h": 200, "type": "note", "text": nt,
                      "color": "#c6f5d2"})

    # every screen, as built
    G = [("home", DM7, 1400), ("key mode", [T.KEY_ON] + DM7, 2000), ("lock", ["button:copy@200+50"] + DM7, 2600),
         ("memory played", DM7 + ["pad:21@500+100", "pad:21@2500+600"], 2900), ("memory saved", DM7 + ["pad:21@500+100"], 900),
         ("voicing turned", DM7 + ["touch:0@1500+300", "knob:1@1550:2"], 1700), ("knob, a range", ["touch:0@200+300", "knob:1@250:2"], 400),
         ("playing", DM7 + ["step:0@1500+50", "step:4@1550+50", "button:play@1700+50"], 2600),
         ("arp", ["button:shift@200+1400", "step:3@250+50", "step:3@500+600", "knob:0@1300:1", "knob:0@1310:1",
                  "knob:0@1320:1", "button:back@1700+50", "pad:25@1900+1500", "pad:17@1900+1500", "pad:1@2000+1400"], 2900),
         ("prog", ["button:menu@200+400", "pad:10@300+50", "pad:3@800+300"], 1200),
         ("step held", st_ev := DM7 + ["step:0@1500+50", "step:8@2000+2000", "pad:24@2100+300", "pad:1@2150+200",
                                       "knob:4@2500:4", "knob:1@2550:1"], 3800),
         ("empty step", ["step:9@200+900"], 1000), ("menu held", ["button:menu@200+600"], 700),
         ("loop held", ["button:loop@200+900"], 800), ("shift held", [S % 900], 1000),
         ("PERF settings", [S % 900, "step:3@250+600"], 1400), ("FX settings", [S % 900, "step:5@250+600"], 1400),
         ("KEY settings", [S % 900, "step:0@250+600"], 1400), ("editor OSC", [S % 300, "step:8@250+50"], 1100),
         ("editor FILT", [S % 300, "step:8@250+50", "pad:29@900+50"], 1300),
         ("editor knob", [S % 300, "step:8@250+50", "knob:8@900:3"], 1100),
         ("options", [S % 300, "step:15@250+50", "knob:0@800:1", "button:jogclick@1000+50"], 1300),
         ("wheel: preset", ["knob:0@500:1"], 800), ("wheel: engine", ["button:jogclick@200+50", "knob:0@500:1"], 800),
         ("wheel: bass", [S % 600, "knob:0@400:1"], 900),
         ("wheel: bass engine", [S % 900, "button:jogclick@300+50", "knob:0@500:1"], 1000),
         ("iso", ["button:menu@200+400", "pad:16@300+50", "pad:20@900+300"], 1100),
         ("panic", DM7[:1] + ["button:delete@400+50"], 700)]
    gy = Y0 + 3 * DY + 40
    items.append({"id": uid(), "x": X0 - 60, "y": gy - 80, "w": 3 * DX + 860, "h": 5 * 380 + 120, "type": "group",
                  "title": "ChoralRoot takeover - every screen as built (the emulator's), 2026-11-21", "color": "#28dc5a"})
    for k, (name, ev, end) in enumerate(G):
        r = run(ev, end)
        items.append({"id": uid(), "x": X0 + (k % 7) * 600, "y": gy + (k // 7) * 380, "w": 560, "h": 340,
                      "type": "screen", "name": "%d %s" % (k + 1, name), "description": " / ".join(r.says)[-120:],
                      "screen": V["pack"](scr(r))})
    items.append({"id": uid(), "x": X0 + 3 * DX, "y": Y0, "w": 760, "h": 520, "type": "note", "color": "#ffb3a7",
                  "text": "AS BUILT (2026-11-21), made from the firmware: what differs from proposal 4\n"
                          "- The screens are proposal 3's, drawn in ChoralRoot's own bold (Inter Tight, the FM-1's), "
                          "not Arial.\n- Home stopped: the chord and its notes only; the steps show on their LEDs. "
                          "Playing: slot, step, the 16 steps.\n- Key Mode / Iso / a memory: the label top left, the "
                          "chord at x 30.\n- Settings layers: knobs 1-4 (the FM-1's pages have four); 5-8 dark.\n"
                          "- Loop held knobs: Length, Rate, Quantize, Count-in, Level, Beat, Rec mode, Tempo.\n"
                          "- Editor: knobs 1-4 the lane, 5-8 the lane under it.\n- Not built: a Recording screen "
                          "(Rec, bar.beat)."})
    doc = {"format": "ableton-move-designer", "version": 3, "name": "ChoralRoot takeover, as built", "items": items}
    json.dump(doc, open(ARGS.out, "w"), indent=1)
    print("items", len(items))


main()
