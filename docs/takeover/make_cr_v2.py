#!/usr/bin/env python3
"""ChoralRoot takeover, proposal 2: the owner's "Chord Proposal" (2026-11-20) worked out. A one-octave keyboard on the
two bottom rows, the chord mods top left, eight CHORD MEMORIES top right (hold a root, tap one: saved), the 16 steps
ChoralRoot's step sequencer (the looper's Step entry / loop editor, Elektron style).
Writes the items of a new group for the Ableton Move Designer (format 'ableton-move-designer' v3), placed at x0, y0:
    python3 make_cr_v2.py <rfm1cs font5x7.c> <out.json> [x0 y0]
out.json is a whole document (the group alone); merge its items into a board, or import it on its own."""
import json, os, random, sys

FONT, OUT = sys.argv[1], sys.argv[2]
X0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0
Y0 = int(sys.argv[4]) if len(sys.argv) > 4 else 0
HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv = [sys.argv[0], FONT, os.environ.get("TMPDIR", "/tmp") + "/cr-v1-png"]
sys.path.insert(0, HERE)
import make_cr_mockups as M  # noqa: E402
# make_cr_movedesign runs main() on import; borrow its helpers by exec'ing the file up to main()
src = open(os.path.join(HERE, "make_cr_movedesign.py")).read().split("\ndef main():")[0]
src = src.split("import make_cr_mockups as M")[1]
G = {"M": M, "__name__": "v1"}
exec("import base64, json, os, random, sys\n" + src, G)
pack, ALL, hexc, base_controls = G["pack"], G["ALL"], G["hexc"], G["base_controls"]

random.seed(20261120)
uid = lambda: "".join(random.choice("abcdefghijklmnopqrstuvwxyz0123456789") for _ in range(12))
dim = M.dim

VIOLET_D = (0x36, 0x10, 0x47)    # the owner's colours from "Chord Proposal"
ORANGE_D = (0x47, 0x21, 0x00)
KEY_D = (0x51, 0x51, 0x51)
MEMC = (0x13, 0xAB, 0xBF)
YELLOW = (0xFF, 0xD2, 0x28)
WHITE = (255, 255, 255)
SEQC = (40, 110, 255)            # a step with a chord
HEAD = (255, 255, 255)           # the playhead
HELD = (255, 120, 0)
NN = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
MODS = ["DIM", "MIN", "MAJ", "SUS", "6", "m7", "M7", "9"]
MEM = ["Dm7", "G7", "Cmaj7", "Am9", "Fsus", None, None, None]

# the sequence on slot 1: 16 steps, a chord on 1, 5, 9, 13 and a passing chord on 15
SEQ = {0: "Dm7", 4: "G7", 8: "Cmaj7", 12: "Am9", 14: "Fsus"}


def pads(pressed=(), held_mods=(), mem=MEM, mem_flash=None):
    """the designer's ids -> (label, colour); pressed: note pcs 0..12 (12 = the top C)"""
    c = {}
    for j, m in enumerate(MODS):
        pid = f"pad-{1 + j // 4}-{1 + j % 4}"
        base = VIOLET_D if j < 4 else ORANGE_D
        col = WHITE if j in held_mods else base
        c[pid] = (m, col)
    for j in range(8):
        pid = f"pad-{1 + j // 4}-{5 + j % 4}"
        if j == mem_flash:
            c[pid] = (f"{j + 1}\nsaved", WHITE)
        elif mem[j]:
            c[pid] = (f"{j + 1}\n{mem[j]}", MEMC)
        else:
            c[pid] = (f"{j + 1}", dim(MEMC, 0.18))
    whites = [0, 2, 4, 5, 7, 9, 11, 12]
    for k, pc in enumerate(whites):
        col = WHITE if pc in pressed else YELLOW if pc in (0, 12) else KEY_D
        c[f"pad-4-{k + 1}"] = (NN[pc % 12], col)
    blacks = {2: 1, 3: 3, 5: 6, 6: 8, 7: 10}
    for col_ in range(1, 9):
        if col_ in blacks:
            pc = blacks[col_]
            c[f"pad-3-{col_}"] = (NN[pc], WHITE if pc in pressed else KEY_D)
        else:
            c[f"pad-3-{col_}"] = ("", None)
    return c


def steps(play=None, held=None, length=16, slot_view=False, cur_slot=0, page=0):
    out = {}
    for i in range(16):
        if slot_view:
            if i < 10:
                full = i in (0, 1, 3)
                col = HELD if i == cur_slot else (dim(SEQC, 0.6) if full else dim(SEQC, 0.12))
                lab = f"Slot {i + 1}"
            elif i >= 12:
                col = WHITE if i - 12 == page else dim(WHITE, 0.25)
                lab = f"Page {i - 11}"
            else:
                col, lab = None, ""
        else:
            lab = SEQ.get(i, "")
            if i == held:
                col = HELD
            elif i == play:
                col = HEAD
            elif i >= length:
                col = None
            elif i in SEQ:
                col = SEQC
            else:
                col = (24, 24, 30)
        out[i] = (lab, col)
    return out


def scr_home(name="Dm7", play=4, perf="SEQ 1"):
    s = M.Scr()
    s.header("C MAJ", perf)
    s.text(1, 19, name, 4)
    # the 16 steps: a box each, filled = a chord, the playhead inverted bar under it
    for i in range(16):
        x = 1 + i * 8
        if i in SEQ:
            s.rect(x, 52, 7, 7)
        else:
            s.box(x, 52, 7, 7)
        if i == play:
            s.rect(x, 61, 7, 3)
    return s


def scr_saved(n, name):
    s = M.Scr()
    s.ctext(64, 0, f"Memory {n}", 2)
    s.ctext(64, 17, name, 4)
    s.ctext(64, 50, "saved", 2)
    return s


def scr_step(n, name):
    s = M.Scr()
    s.header(f"STEP {n}", "x1")
    s.text(1, 19, name, 4)
    s.text(1, 50, "len 1", 2)
    s.text(127 - M.Scr.tw("100%", 2), 50, "100%", 2)
    return s


def scr_slots():
    s = M.Scr()
    s.header("SLOTS", "PG 1")
    for i in range(10):
        x = 1 + (i % 5) * 25
        y = 20 + (i // 5) * 22
        full = i in (0, 1, 3)
        if i == 0:
            s.rect(x, y, 24, 20)
            s.ctext(x + 12, y + 3, str(i + 1), 2, 0)
        else:
            s.box(x, y, 24, 20, 1, dotted=not full)
            s.ctext(x + 12, y + 3, str(i + 1), 2)
    return s


PROGC = (150, 90, 255)
LAYOUTS = ["KEYS", "SCALE", "CHROM", "PROG"]
PROGS = ["Pop", "Jazz", "Neo", "Gosp", "Epic", "Dor"]


def pads_menu(cur=0, prev=1, bank=0, prog=None):
    """Menu held: mods block = the layouts, memories block = the banks, the two bottom rows = the 16 progressions"""
    c, meta = {}, {}
    for j in range(8):
        pid = f"pad-{1 + j // 4}-{1 + j % 4}"
        if j < 4:
            lab = LAYOUTS[j] + ("\nnow" if j == cur else "\nprev" if j == prev else "")
            col = WHITE if j == cur else dim(WHITE, 0.45) if j == prev else dim(WHITE, 0.15)
            meta[pid] = {"label": LAYOUTS[j], "function": f"Menu held + this pad: the {LAYOUTS[j].title()} layout on the two bottom rows",
                         "click": "Pick it (Menu let go: it plays)"}
        else:
            lab, col = "", None
            meta[pid] = {"label": "", "function": "Free (a later layout)"}
        c[pid] = (lab, col)
    for j in range(8):
        pid = f"pad-{1 + j // 4}-{5 + j % 4}"
        c[pid] = (f"Bank {j + 1}", MEMC if j == bank else dim(MEMC, 0.5 if j in (1, 2) else 0.15))
        meta[pid] = {"label": f"Bank {j + 1}", "function": f"Menu held + this pad: memory bank {j + 1} (8 memories each, 64 in all)",
                     "click": "Pick the bank (lit: it holds chords)"}
    for i in range(16):
        r, k = divmod(i, 8)
        pid = f"pad-{3 + r}-{k + 1}"
        has = i < len(PROGS)
        c[pid] = (f"{i + 1}" + (f"\n{PROGS[i]}" if has else ""), WHITE if i == prog else dim(PROGC, 0.85) if has else dim(PROGC, 0.12))
        meta[pid] = {"label": f"Prog {i + 1}", "function": f"Menu held + this pad: progression {i + 1}; the layout becomes Prog with it",
                     "click": "Pick it (dim: empty)"}
    return c, meta


PCH = ["Em9", "Cmaj9", "G6/B", "Am11", "Em9", "D/F#", "Cmaj7", "B7#5"]


def pads_prog(pressed=None):
    c, meta = pads(), {}
    for i in range(16):
        r, k = divmod(i, 8)
        pid = f"pad-{4 - r}-{k + 1}"
        has = i < len(PCH)
        c[pid] = (f"{i + 1}" + (f"\n{PCH[i]}" if has else ""), WHITE if i == pressed else dim(PROGC, 0.85) if has else (14, 14, 18))
        meta[pid] = {"label": f"Chord {i + 1}", "function": f"Prog layout: the progression's chord {i + 1} (Shift + Left / Right: chords 17-32, 33-48)",
                     "click": "Play it (a mod held: your own type on its root)",
                     "hold": "Held + a memory pad: saved there; held + a step: the step gets it"}
    return c, meta


def scr_layout():
    s = M.Scr()
    s.header("LAYOUT", "BK 1")
    s.ctext(64, 19, "KEYS", 4)
    s.text(1, 50, "prev SCALE", 2)
    return s


def scr_prog2():
    s = M.Scr()
    s.header("NEO SOUL", "4")
    s.text(1, 19, "Am11", 4)
    for i in range(16):
        x = 1 + i * 8
        (s.rect if i < 8 else s.box)(x, 52, 7, 7, 1, **({} if i < 8 else {"dotted": True}))
        if i == 3:
            s.rect(x, 61, 7, 3)
    return s


KN_MAIN = [("Voicing", M.ORANGE), ("Perform", M.VIOLET), ("Rate", M.VIOLET), ("Direction", M.VIOLET),
           ("Bass reg.", (255, 90, 30)), ("Key", YELLOW), ("Scale", YELLOW), ("Transpose", YELLOW)]
KN_STEP = [("Note len", HELD), ("Chance", HELD), ("Velocity", HELD), ("Ratchet", HELD), ("Condition", HELD),
           ("Voicing", HELD), ("Transpose", HELD), ("Steps", SEQC)]
KN_SHIFT = M.KNOBS_SHIFT
STEPWORDS = {"Note len": "The step's notes: 1/4, 1/2, 3/4 of a step, then 1..64 steps (moves no other step)",
             "Chance": "100, 90, 75, 60, 50, 40, 25, 10 %", "Velocity": "The step's velocity",
             "Ratchet": "Off, x2, x3, x4", "Condition": "Always; 1:2, 2:2; 1:3..3:3; 1:4..4:4; 1st; not 1st",
             "Voicing": "Follow, -12..+12", "Transpose": "The step's notes, a semitone a detent",
             "Steps": "The loop's length, 1..64 steps"}


def controls(padmap, stepmap, knobs, knob_kind, screen_meta, shift=False, menu_held=False, padmeta=None):
    c = base_controls()
    # buttons that change from proposal 1
    m = lambda **k: {"meta": k}
    c["menu"] = {**m(label="LAYOUT", function="The two bottom rows' layout: Keys, Scale (Key Mode), Chromatic, Prog. The mods and memories stay in every layout.",
                     click="Quick tap: the previous layout (flips between the last two)",
                     hold="Held: the pads become the picker: layouts, the 8 memory banks, the 16 progressions"), "led": {"brightness": 1 if menu_held else 0.3}}
    c["left"] = m(label="PAGE <", function="The sequencer's page: steps 1-16, 17-32, 33-48, 49-64 (the step icons show which).", click="Previous page")
    c["right"] = m(label="PAGE >", function="The next page of 16 steps (a loop of 64 steps at most).", click="Next page")
    c["play"] = {**m(label="SEQ PLAY", function="The sequencer (the looper's slot): play / stop from step 1.", click="Play / stop",
                     shift="Shift + Play: restart from step 1"), "led": {"color": "#00c800"}}
    c["record"] = {**m(label="SEQ REC", function="Records what is played into the loop, live, in the record mode (Overwrite, Advance, Overdub, Replace, Step).",
                       click="Record / stop recording", shift="Shift + Rec: the next record mode"), "led": {"color": "#440000"}}
    c["undo"] = {**m(label="UNDO", function="The looper's undo: the layer in progress, else the last layer.", click="Undo"), "led": {"brightness": 0.3}}
    c["delete"] = {**m(label="CLEAR / PANIC", function="Held + a step: clears it. Held + a memory pad: clears it. Tapped alone: PANIC (all notes off, here and to every routed firmware).",
                       click="Panic", hold="Delete held + step / memory: clear it"), "led": {"brightness": 0.3}}
    c.update(G["knob_controls"](knobs, shift)) if knob_kind == "main" else None
    if knob_kind != "main":
        for i, (lab, col) in enumerate(knobs):
            c[f"knob-{i + 1}"] = {"meta": {"label": lab.upper(),
                                           "function": ("Step held: " + STEPWORDS[lab]) if knob_kind == "step" else f"Shift page: {lab}",
                                           "turn": f"Change {lab}", "touch": f"{lab} and its value, full screen"}}
            c[f"knobled-{i + 1}"] = {"led": {"color": hexc(col)}}
    for pid, (lab, col) in padmap.items():
        r, k = int(pid.split("-")[1]), int(pid.split("-")[2])
        if r in (1, 2) and k <= 4:
            j = (r - 1) * 4 + k - 1
            meta = {"label": lab, "function": (f"Chord type {MODS[j]}" if j < 4 else f"Extension {MODS[j]}") +
                    ": held + a key plays that chord; mods stack (MIN + MAJ = SUS)",
                    "click": "Held: the next key plays it", "shift": "Shift + mod: LOCK it (stays held)"}
        elif r in (1, 2):
            j = (r - 1) * 4 + k - 5
            meta = {"label": lab.replace("\n", " "),
                    "function": f"Chord memory {j + 1}: plays the chord saved in it, like a key (velocity, pressure, perform, bass).",
                    "click": "Play its chord (empty: nothing)",
                    "hold": "A KEY held (with any mods) + this pad: the chord sounding is saved here (white flash, \"Memory n saved\")",
                    "shift": "Delete held + pad: clear it. A step held + pad: the step gets this chord",
                    "description": "8 memories a bank, 8 banks (Menu held + a memory pad: the bank), kept with the session (the global save). Saved: root, type, extensions and the voicing."}
        else:
            meta = {"label": lab, "function": "A key: plays the chord on this root (the mods held, or Key Mode's). Velocity from the pad, pressure = aftertouch.",
                    "click": "Play the chord", "hold": "Held + a memory pad: save the chord to it. Held + a step: the step gets it",
                    "description": "One octave, C to C (the top C yellow too). Up / Down move it an octave."} if lab else {"label": "", "function": "Not used (no black key here)."}
        c[pid] = {"meta": meta, "led": {"color": hexc(col) if col else None}}
    for i, (lab, col) in stepmap.items():
        if shift:
            meta = {"label": lab, "function": ("Shift + step 1-10: that loop slot (its sequence)" if i < 10 else
                                               "Shift + step 13-16: that page" if i >= 12 else "Not used"),
                    "click": "Pick it"}
        else:
            meta = {"label": lab or f"{i + 1}",
                    "function": f"Step {i + 1} of the page: lit blue = a chord in it, white = playing now, dark = empty, off = past the loop's end.",
                    "click": "Empty: the last chord played goes in. Holding a chord: cleared",
                    "hold": "Held: play a key or a memory pad = that chord goes in (several keys add up); the knobs edit the step",
                    "shift": "Shift + step: a loop slot (1-10) or a page (13-16)"}
        c[f"step-{i + 1}"] = {"meta": meta, "led": {"color": hexc(col) if col else None}}
        c[f"stepicon-{i + 1}"] = {"led": {"brightness": (1 if i == 0 else 0.25) if i < 4 else 0}}
    for pid, meta in (padmeta or {}).items():
        c[pid]["meta"] = meta
    c["screen"] = {"meta": screen_meta}
    if shift:
        c["shift"]["led"] = {"brightness": 1}
    return c


def layout(x, y, name, desc, scr, ctl, frames=None):
    it = {"id": uid(), "x": x, "y": y, "w": 1048, "h": 616, "type": "layout", "name": name, "description": desc,
          "scope": ALL, "controls": ctl, "screen": pack(scr)}
    if frames:
        full = G["FULL"]
        it["screenFrames"] = [{"id": uid(), "duration": d, "image": pack(f),
                               "layers": [{"id": uid(), "type": "raster", "name": "Frame", "visible": True, "x": 0, "y": 0,
                                           "data": pack(f), "alpha": full}]} for f, d in frames]
    return it


def main():
    DX, DY = 1148, 920
    pos = lambda k: (X0 + (k % 3) * DX, Y0 + (k // 3) * DY)
    items = [{"id": uid(), "x": X0 - 60, "y": Y0 - 60, "w": 3 * DX + 860, "h": 2 * DY + 220, "type": "group",
              "title": "ChoralRoot takeover - proposal 2 (the owner's Chord Proposal), 2026-11-20", "color": "#13abbf"}]
    play_frames = [(scr_home(SEQ.get(p, "Dm7" if p < 4 else "G7" if p < 8 else "Cmaj7" if p < 12 else "Am9" if p < 14 else "Fsus"), p), 125) for p in range(16)]
    L = [
        ("A. Home: keyboard, mods, memories, sequencer",
         "The sequencer plays slot 1 (step 5 now, white). D pressed with m7 held: Dm7. Memories 1-5 hold chords, 6-8 are empty.",
         scr_home("Dm7", 4), controls(pads(pressed=(2,), held_mods=(5,)), steps(play=4), KN_MAIN, "main",
                                      {"label": "Chord view", "function": "The key and the slot in the bar, the chord big, the 16 steps of the page (filled: a chord), the playhead under them.",
                                       "description": "Animated: the playhead runs (hover)."}),
         play_frames,
         "Keys: one octave, C to C on the bottom row, the black keys above (Up / Down: the octave). Mods held + a key: that chord. Menu: the layout (tap: the previous one; held: the picker, E).\n"
         "Memories (cyan): tap = play the saved chord. Steps: the sequence of the loop slot, 16 a page (Left / Right: up to 64 steps)."),
        ("B. Saving a chord to a memory",
         "D held with m7 (Dm7 sounding), memory 6 tapped: Dm7 is saved there. The pad flashes white, the screen says so.",
         scr_saved(6, "Dm7"), controls(pads(pressed=(2,), held_mods=(5,), mem_flash=5), steps(play=6), KN_MAIN, "main",
                                       {"label": "Saved popup", "function": "\"Memory 6\", the chord big, \"saved\"; gone after a second. Said: \"Memory 6, D minor seven\"."}),
         None,
         "Hold a key (with any mods), tap a memory pad: saved, over whatever was there. Tap it later alone: the chord plays, as a key would (perform mode, bass, voicing as saved).\n"
         "Delete held + a memory pad: cleared. A step held + a memory pad: the step gets that chord."),
        ("C. A step held: edit it",
         "Step 9 held: its chord on the screen, the knobs are the step's (as the FM-1's loop editor, Step + Trig pages in one). Play a key or a memory now: the step gets it.",
         scr_step(9, "Cmaj7"), controls(pads(), steps(play=10, held=8), KN_STEP, "step",
                                        {"label": "Step view", "function": "STEP n and its ratchet in the bar, the chord big, its note length and chance at the foot; a knob touched: its popup."}),
         None,
         "Tap an empty step: the last chord played goes in (quick entry). Tap a step with a chord: cleared (Delete + step too).\n"
         "Held: keys / memories write it (one push replaces, the keys of one push add up), the knobs edit it. Rec records live over the loop in the record mode."),
        ("D. Shift held: slots and pages",
         "Shift held: steps 1-10 are the loop slots (1 playing, orange; 2 and 4 hold sequences), 13-16 the pages. The knobs' second page.",
         scr_slots(), controls(pads(), steps(slot_view=True), KN_SHIFT, "shift",
                               {"label": "Slots view", "function": "The ten slots: filled = holds a sequence (dotted: empty), the current inverted; the page in the bar."}, shift=True),
         None,
         "Shift + step 1-10: that slot's sequence (it switches at the loop's end while playing). Shift + 13-16: page 1-4. Shift + Rec: the next record mode. Shift + a mod: LOCK it."),
    ]
    pm, pmeta = pads_menu()
    L.append(("E. Menu held: the picker",
              "Menu held: the mods block picks the layout (Keys now, Scale the previous), the memories block the bank (1 of 8), the bottom rows the 16 progressions. A quick Menu tap flips Keys <-> Scale.",
              scr_layout(), controls(pm, steps(play=4), KN_MAIN, "main",
                                     {"label": "Layout picker", "function": "LAYOUT and the bank in the bar, the current layout big, the previous one at the foot."},
                                     menu_held=True, padmeta=pmeta), None,
              "Menu held: picker; let go: back to playing. Picking a progression opens the Prog layout with it. Quick Menu tap (no pad): the previous layout, so two layouts are one tap apart.\n"
              "Only the bottom rows change with the layout; the mods and the memories (of the bank picked) stay."))
    pp, ppmeta = pads_prog(pressed=3)
    L.append(("F. Prog layout",
              "Progression 3 (Neo Soul) picked: the bottom rows are its chords 1-16. Chord 4 pressed: Am11. Mods and memories as ever.",
              scr_prog2(), controls(pp, steps(play=4), KN_MAIN, "main",
                                    {"label": "Prog view", "function": "The progression's name and the chord's number in the bar, the chord big, its 16 chords (filled: has one), the last played marked."},
                                    padmeta=ppmeta), None,
              "Shift + Left / Right: chords 17-32, 33-48 (Left / Right alone stay the sequencer's pages). A mod held + a chord pad: your own type on its root, as the FM-1's Prog keyboard."))
    for k, (name, desc, scr, ctl, frames, nt) in enumerate(L):
        x, y = pos(k)
        items.append(layout(x, y, name, desc, scr, ctl, frames))
        items.append({"id": uid(), "x": x, "y": y + 640, "w": 1048, "h": 200, "type": "note", "text": nt, "color": "#bff3fa"})
    x, y = X0 + 3 * DX, Y0
    items.append({"id": uid(), "x": x, "y": y, "w": 700, "h": 300, "type": "note", "color": "#ffb3a7",
                  "text": "DECIDED (2026-11-20)\n- Progressions and layouts: Menu held = the picker (E); a quick tap flips the last two layouts.\n- Memories: 8 banks of 8, one set for ChoralRoot; the bank is picked with Menu held.\n"
                          "- MIDI: as the FM-1 firmware's Options (Perform ch 1, Bass ch 2, Raw Chord off); one route.\n\nOPEN\n- Capture and Sample are free: ideas?"})
    doc = {"format": "ableton-move-designer", "version": 3, "name": "ChoralRoot takeover, proposal 2", "items": items}
    json.dump(doc, open(OUT, "w"), indent=1)


main()
