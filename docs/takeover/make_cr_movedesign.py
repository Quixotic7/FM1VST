#!/usr/bin/env python3
"""ChoralRoot's Move takeover as an Ableton Move Designer document (quixotic7.github.io/abletonmovedesigner, format
'ableton-move-designer' version 3): six layouts with every control's metadata and LEDs, their screens pixel for pixel
(the same drawings as make_cr_mockups.py), the arp screen animated, and notes.
    python3 make_cr_movedesign.py <rfm1cs font5x7.c> <out.movedesign.json>"""
import base64, json, os, random, sys

FONT, OUT = sys.argv[1], sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv = [sys.argv[0], FONT, HERE]
sys.path.insert(0, HERE)
import make_cr_mockups as M  # noqa: E402  (draws the PNGs again too)

random.seed(7)
uid = lambda: "".join(random.choice("abcdefghijklmnopqrstuvwxyz0123456789") for _ in range(12))


def pack(scr):
    """row-major, 16 bytes a row, MSB the leftmost pixel"""
    b = bytearray(1024)
    for y in range(64):
        for x in range(128):
            if scr.px[y][x]:
                b[y * 16 + x // 8] |= 0x80 >> (x % 8)
    return base64.b64encode(bytes(b)).decode()


FULL = base64.b64encode(b"\xff" * 1024).decode()
hexc = lambda c: "#%02x%02x%02x" % c

ALL = (["screen"] + [f"knob-{i}" for i in range(1, 9)] + [f"knobled-{i}" for i in range(1, 9)] +
       ["volume", "wheel", "back", "menu"] + [f"track-{i}" for i in range(1, 5)] +
       [f"pad-{r}-{c}" for r in range(1, 5) for c in range(1, 9)] +
       ["capture", "sample", "loop", "mute", "delete", "copy", "undo", "shift", "play", "record"] +
       [f"step-{i}" for i in range(1, 17)] + [f"stepicon-{i}" for i in range(1, 17)] +
       ["left", "right", "up", "down", "power"])

TYPES = ["DIM", "MIN", "MAJ", "SUS"]
EXTS = ["6", "m7", "M7", "9"]


def base_controls():
    """what every layout shares: the buttons, the wheel, the volume, the track buttons"""
    c = {}
    m = lambda **k: {"meta": k}
    c["track-1"] = {**m(label="FM-1 / CHORDS", function="RemoteFM1's (not claimed): Track 1 cycles FM-1 mode and this takeover.",
                         click="Back to FM-1 mode (again: the takeover)", hold="RemoteFM1's Shift layer (FM-1 mode only)"),
                    "led": {"color": "#c43aff"}}
    for i, (lab, col) in enumerate([("MIDI", "#c8ff2a"), ("CONTROL", "#1ae4ff"), ("SEQ", "#ff2a2a")], start=2):
        c[f"track-{i}"] = {**m(label=lab, function=f"RemoteFM1's (not claimed): Track {i}'s mode, as ever.",
                                click=f"{lab.title()} mode", shift="Back + Track 1..3: the firmware slots"),
                           "led": {"color": col}}
    c["volume"] = m(label="MASTER", function="RemoteFM1's (not claimed): the master volume.",
                    turn="Master volume", touch="Touched + Back: out of any takeover to FM-1 mode")
    c["back"] = {**m(label="BACK", function="Closes a popup or the Prog layout. Touch Volume + Back: FM-1 mode.",
                     click="Close / up one level (sent at release: Back + Track switches slots)",
                     description="ChoralRoot claims the buttons but not the Track buttons, so the host gives Back at its release."),
                 "led": {"brightness": 0.3}}
    c["menu"] = {**m(label="LAYOUT", function="The root pads' layout: Scale <-> Chromatic.", click="Scale / Chromatic"),
                 "led": {"brightness": 0.3}}
    c["wheel"] = m(label="SOUND", function="The chord sound, by name (popup).", turn="Next / previous sound",
                   shiftTurn="The engine (FM6, FM TONE, VA, CZ-1, P5, WHEEL, GRAIN)", click="Favourite on / off",
                   touch="The sound's name")
    c["play"] = {**m(label="LOOP PLAY", function="The looper: play / stop.", click="Play / stop", shift="The PLAY menu's next slot"),
                 "led": {"color": "#004400"}}
    c["record"] = {**m(label="LOOP REC", function="The looper: record (the record mode in Shift).", click="Record / overdub"),
                   "led": {"color": "#440000"}}
    c["loop"] = {**m(label="LATCH", function="LATCH: chords keep sounding after the pads are let go.", click="Latch on / off"),
                 "led": {"brightness": 0.3}}
    c["copy"] = {**m(label="LOCK", function="LOCK: the chord keys held now stay lit (latched); the roots play them.", click="Lock on / off"),
                 "led": {"brightness": 0.3}}
    c["mute"] = {**m(label="BASS", function="The bass part on / off.", click="Bass on / off", shift="The bass sound's popup"),
                 "led": {"brightness": 1}}
    c["delete"] = {**m(label="PANIC", function="All notes off, here and to every routed firmware (CC 123).", click="Panic"),
                   "led": {"brightness": 0.3}}
    c["shift"] = {**m(label="SHIFT", function="Held: the knobs' second page.", hold="Knobs page 2 on the screen"),
                  "led": {"brightness": 0.3}}
    c["up"] = {**m(label="OCT +", function="The root pads an octave up (-2..+2).", click="Octave up"), "led": {"brightness": 0.3}}
    c["down"] = {**m(label="OCT -", function="The root pads an octave down (-2..+2).", click="Octave down"), "led": {"brightness": 0.3}}
    c["left"] = m(label="PROG <", function="In Prog: the previous 24 chords (keys 1-24).", click="Keys 1-24")
    c["right"] = m(label="PROG >", function="In Prog: the next 24 chords (keys 25-48).", click="Keys 25-48")
    c["capture"] = m(label="", function="Not used yet.")
    c["sample"] = m(label="", function="Not used yet.")
    c["undo"] = m(label="", function="Not used yet.")
    return c


def knob_controls(knobs, shift=False):
    c = {}
    main = M.KNOBS_MAIN
    sh = M.KNOBS_SHIFT
    words = {"Voicing": "Revoice: up moves the lowest note an octave up, down the highest down (-12..+12)",
             "Perform": "Off, Strum, Strum 2, Slop, Arp, Arp 2, Pattern, Harp",
             "Rate": "The mode's first value: strum rate, slop amount, arp division, pattern, harp rate",
             "Direction": "The mode's second value (Direction; Pattern: its division)",
             "Bass reg.": "The bass register, -2..+4 octaves", "Key": "The tonic (Key Mode on)",
             "Scale": "Major / Minor", "Transpose": "-24..+24 semitones, everything played"}
    for i in range(8):
        lab, col = knobs[i]
        c[f"knob-{i + 1}"] = {"meta": {
            "label": lab.upper(), "function": words.get(lab, lab) if not shift else f"Shift page: {lab}",
            "turn": f"Change {lab}", "touch": f"Fullscreen popup: {lab} and its value",
            "shiftTurn": f"{sh[i][0]}" if not shift else f"{main[i][0]} (Shift let go)"}}
        c[f"knobled-{i + 1}"] = {"led": {"color": hexc(col)}}
    return c


def pad_controls(pads):
    """pads: bottom-left first (make_cr_mockups); the designer's row 1 is the top"""
    c = {}
    for i, (lab, col) in enumerate(pads):
        r, col_ = divmod(i, 8)
        pid = f"pad-{4 - r}-{col_ + 1}"
        name = lab.replace("\n", " ")
        if r == 3:
            j = col_
            if j < 4:
                fn = f"Chord type {TYPES[j]}: held + a root pad = that type" + (" (SUS: also MIN + MAJ together)" if j == 3 else "")
            else:
                fn = f"Extension {EXTS[j - 4]}: held + a root pad adds it; stacks with the others"
            meta = {"label": name, "function": fn, "click": "Held: the next root plays it",
                    "shift": "In Key Mode: MAJ / MIN set the scale"}
        else:
            meta = {"label": name, "function": "A root: plays its chord (velocity from the pad, pressure = aftertouch)",
                    "click": "Play the chord", "hold": "Sounds while held (LATCH: keeps sounding)"}
        c[pid] = {"meta": meta, "led": {"color": hexc(col) if col != (0, 0, 0) else None}}
    return c


def step_controls(steps):
    c = {}
    names = ["Pop Four", "Jazz ii-V-I", "Neo Soul", "Gospel Walk", "Minor Epic", "Dorian Days"]
    for i, (lab, col) in enumerate(steps):
        nm = names[i] if i < 6 else f"Progression {i + 1} (empty)"
        c[f"step-{i + 1}"] = {"meta": {"label": lab.upper(), "function": f"Progression {i + 1}: {nm}",
                                       "click": "Prog: the root pads play its chords (the lit step again: back to Scale)",
                                       "shift": "Its name in a popup"},
                              "led": {"color": hexc(col) if sum(col) > 100 else (hexc(col) if i < 6 else None)}}
        c[f"stepicon-{i + 1}"] = {"led": {"brightness": 0}}
    return c


def layout(x, y, name, desc, scr, pads, steps, knobs, screen_meta, frames=None, shift=False):
    ctl = base_controls()
    ctl.update(knob_controls(knobs, shift))
    ctl.update(pad_controls(pads))
    ctl.update(step_controls(steps))
    ctl["screen"] = {"meta": screen_meta}
    if shift:
        ctl["shift"]["led"] = {"brightness": 1}
    it = {"id": uid(), "x": x, "y": y, "w": 1048, "h": 616, "type": "layout", "name": name, "description": desc,
          "scope": ALL, "controls": ctl, "screen": pack(scr)}
    if frames:
        it["screenFrames"] = [{"id": uid(), "duration": d, "image": pack(f),
                               "layers": [{"id": uid(), "type": "raster", "name": "Frame", "visible": True, "x": 0, "y": 0,
                                           "data": pack(f), "alpha": FULL}]} for f, d in frames]
    return it


def note(x, y, text, w=1048, h=220, color="#ffe58a"):
    return {"id": uid(), "x": x, "y": y, "w": w, "h": h, "type": "note", "text": text, "color": color}


def arp_frame(i):
    s = M.Scr()
    s.header("C MAJ", "ARP")
    s.text(1, 19, "Dm7", 3)
    s.text(127 - M.Scr.tw("1/8", 2), 19, "1/8", 2)
    for j, (x, n) in enumerate(zip([10, 40, 70, 100], ["D", "F", "A", "C"])):
        if j == i:
            s.rect(x - 4, 38, 22, 22, 1)
            s.text(x + 2, 42, n, 2, 0)
        else:
            s.text(x + 2, 50, n, 2)
    s.hline(0, 61, 128)
    return s


def main():
    X0, Y0, DX, DY = 0, 120, 1148, 920
    pos = lambda k: (X0 + (k % 3) * DX, Y0 + (k // 3) * DY)
    st = M.prog_steps()
    items = []
    items.append({"id": uid(), "x": -60, "y": -60, "w": 3 * DX + 80, "h": 2 * DY + 220, "type": "group",
                  "title": "ChoralRoot takeover (tk1) - proposal 1, 2026-11-19", "color": "#d36cff"})
    L = [
        ("1. CHORDS: Scale (Key Mode)", "The takeover's home. D pressed with m7 held: Dm7. Top row the chord keys, rows 1-3 the key's degrees, an octave a row.",
         M.scr_chord(voicing="+2"), M.scale_pads(pressed=(1, 1), chord_pcs=(2, 5, 9, 0), held=(5,)), st, M.KNOBS_MAIN,
         {"label": "Chord view", "function": "The key (top left) and the performance mode (top right) in the bar; the chord's name big, its numeral, the voicing; the voiced notes on a two-octave keyboard (lit white keys: a block at the foot; lit black keys: hollow).",
          "description": "The name stays until the next chord, as on the FM-1's Chord view."}, None, False,
         "Pads, rows 1-3: degrees I..VII and I' of the key, bottom row C3; each plays the scale's chord (Key Mode). The tonic yellow, the sounding chord's tones cyan, the pressed pad white.\nTop row: DIM MIN MAJ SUS (violet), 6 m7 M7 9 (orange); held or locked, they override the scale's chord. Two types: the last pressed (MIN + MAJ = SUS).\nSteps: the 16 progressions (lit: the six that have chords)."),
        ("2. CHORDS: Chromatic (Menu)", "For chords outside the key: rows a 4th apart from C3, as Move's chromatic layout. Hold SUS, press Eb: Ebsus.",
         M.scr_chord(name="Ebsus", roman="", lit=(51, 56, 58), perf="STRUM"), M.chrom_pads(pressed=(0, 3)), st, M.KNOBS_MAIN,
         {"label": "Chord view", "function": "As the Scale layout's; no numeral for a chord outside the key."}, None, False,
         "The key's notes lit dim, the tonic yellow, the others dark. Menu again: back to Scale."),
        ("3. PROG: a progression on the pads", "Step 3 (Neo Soul) tapped: the 24 root pads play its chords 1-24 (Left / Right: 25-48). Chord 4 pressed: Am11.",
         M.scr_prog(), M.prog_pads(pressed=3), M.prog_steps(sel=2), M.KNOBS_MAIN,
         {"label": "Prog view", "function": "The progression's name and the chord's number in the bar; the chord big; which keys the pads hold."}, None, False,
         "A chord key held + a pad: your own chord on that pad's progression root, as the FM-1's Prog keyboard. The lit step tapped again: back to Scale. Empty chords: dark pads, silent."),
        ("4. A knob touched", "Voicing touched: its name, then its value, full screen (RemoteFM1's rule for every knob). Gone a second after it is let go.",
         M.scr_popup("Voicing", "+2", 14 / 24), M.scale_pads(), st, M.KNOBS_MAIN,
         {"label": "Knob popup", "function": "The name (2x) at the top, the value as big as fits, a bar for a value with a range (none for a choice).",
          "description": "Said by the screen reader: \"Voicing: plus 2\"."}, None, False,
         "Every knob, touched or turned; the wheel's sound popup the same way (the engine above, the sound's name big)."),
        ("5. Shift held: knobs page 2", "Shift held: the eight knobs' second page, their names where the knobs are. Touch one: its popup.",
         M.scr_shift(), M.scale_pads(), st, M.KNOBS_SHIFT,
         {"label": "Shift page", "function": "The page's eight knob names, 1-4 on the left, 5-8 on the right; a value only in a knob's popup."}, None, True,
         "Sound, Bass sound, Gate, Swing, Bass level, the progression's Velocity and Humanize, Tempo."),
        ("6. An arpeggio running", "Perform = Arp: the chord's notes on a line, the sounding one lifted onto a block that hops, as the FM-1's Chord view (animated: hover).",
         arp_frame(0), M.scale_pads(pressed=(1, 1), chord_pcs=(2, 5, 9, 0), held=(5,)), st, M.KNOBS_MAIN,
         {"label": "Arp view", "function": "The chord's name, the division top right, the notes in a row, the sounding one inverted on a block.",
          "description": "Strum, Slop and Harp keep the Chord view."}, [(arp_frame(i), 250) for i in range(4)], False,
         "Its MIDI (Perform on channel 1, the bass on 2) goes where the Routing page sends ChoralRoot's MIDI: other firmwares, Out."),
    ]
    for k, (name, desc, scr, pads, steps, knobs, smeta, frames, shift, nt) in enumerate(L):
        x, y = pos(k)
        items.append(layout(x, y, name, desc, scr, pads, steps, knobs, smeta, frames, shift))
        items.append(note(x, y + 640, nt, h=200))
    gx = 3 * DX + 100
    items.append(note(gx, 120, "TAKEOVER (tk1), the rules every firmware shares:\n- Track 1 tapped in FM-1 mode: the firmware's takeover (its name shown and said); tapped in the takeover: FM-1 mode. From MIDI / Control / Seq: back to whichever of the two the slot was left in.\n- Volume touched + Back: FM-1 mode from any takeover (the only way out when a firmware claims everything).\n- What a firmware does not claim keeps RemoteFM1's meaning. ChoralRoot claims all but the Track buttons and Volume.\n- Screen reader: \"Chords, takeover\" on entering, \"FM-1 mode\" on leaving; the firmware's own words between (\"Chord D minor seven\").",
                      w=700, h=420, color="#e0c3ff"))
    items.append(note(gx, 580, "OPEN QUESTIONS\n1. Steps: the 16 progressions (proposed), or something else (performance modes, looper slots)?\n2. Chords and bass to different firmwares (a per-channel route in the core), or one route for both?\n3. Capture, Sample, Undo: unused here. Ideas?", w=700, h=300, color="#ffb3a7"))
    doc = {"format": "ableton-move-designer", "version": 3, "name": "ChoralRoot takeover", "items": items}
    json.dump(doc, open(OUT, "w"), indent=1)


main()
