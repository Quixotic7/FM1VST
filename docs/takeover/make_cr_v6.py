#!/usr/bin/env python3
"""ChoralRoot takeover, proposal 5 as built (the owner's notes of 2026-11-20, reviewed by Fable; PROPOSAL-5.md): the
Move Designer document made from the firmware itself, as make_cr_v5.py: every screen and LED is the emulator's.
    python3 make_cr_v6.py <rfm1cs font5x7.c> <out.json> --run <rfm1_run> --file <choralroot.rfm1> [--x0 X --y0 Y]"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
G5 = {"__name__": "borrowed", "__file__": os.path.join(HERE, "make_cr_v5.py")}
exec(open(os.path.join(HERE, "make_cr_v5.py")).read().split("\ndef main():")[0], G5)
globals().update({k: v for k, v in G5.items() if not k.startswith("__")})
V4["random"].seed(20261124)                        # (ids of its own)

SCP = [T.pad(1 + i // 2, 7 + i % 2) for i in range(8)]   # the shortcut pads: KEY LOCK / LATCH PERF / B.PRF FX / BASS BEAT
SCNAME = ["KEY", "LOCK", "LATCH", "PERF", "B.PRF", "FX", "BASS", "BEAT"]
SCWHAT = ["Key Mode", "LOCK (the chord keys held stay latched)", "LATCH (what you play keeps sounding)",
          "Chord Perform", "Bass Perform", "the effects", "the bass", "the beat (metronome or drums)"]
SCHELD = ["the KEY settings", "the LOCK settings", "(as a tap)", "the PERF settings", "the Bass PERF settings",
          "the FX settings", "the BASS settings", "the beat's settings"]


def shift_meta(c):
    """Shift held: the pads of columns 7-8 the FM-1's buttons, S2 / S8"""
    for i in range(8):
        r, k = 1 + i // 2, 7 + i % 2
        c.setdefault(f"pad-{r}-{k}", {})["meta"] = {
            "label": SCNAME[i], "function": f"Shift + this pad: {SCWHAT[i]} on / off (the Shift screen's grid, pad for "
            "name)", "click": "On / off (no popup: the name inverted on the screen)", "hold": "Held: " + SCHELD[i]}
    for i in range(16):
        c.setdefault(f"step-{i + 1}", {})["meta"] = {"label": "", "function": "Nothing with Shift"}
    c["step-2"]["meta"] = {"label": "SETTINGS", "function": "Shift + S2: the Settings (RemoteFM1's list)",
                           "click": "Open / close", "description": "Its icon lit while Shift is held."}
    c["step-8"]["meta"] = {"label": "SOUND ED.", "function": "Shift + S8: the sound editor (the chord sound; its "
                           "eighth group pad: the bass)", "click": "Open / close", "description": "Its icon lit."}
    return c


def common5(c):
    m = lambda **k: {"meta": k}
    c["copy"] = {**m(label="COPY", function="Held + a step with a chord: copied (more join); + an empty step: pasted. "
                     "Let go after a copy: the next step tap pastes. Tapped alone: a waiting paste dropped, else the "
                     "loop into the next empty slot.", click="Drop a paste / copy the loop to a slot",
                     hold="Copy held + steps"), "led": c.get("copy", {}).get("led", {"brightness": 0.3})}
    c["delete"] = {**m(label="DELETE", function="Held + a step: cut (the next step pastes it). Held + a memory: "
                       "cleared. Held + Play: the loop cleared. Shift + Delete: PANIC.", click="A hint (no PANIC)",
                       hold="Delete held + step / memory / Play", shift="PANIC"),
                   "led": c.get("delete", {}).get("led", {"brightness": 0.3})}
    c["back"] = {**m(label="BACK", function="Home, from anywhere (the Settings, a settings layer, the editor). While a "
                     "Settings value is edited: done first. Touch Volume + Back: FM-1 mode.", click="Home"),
                 "led": c.get("back", {}).get("led", {"brightness": 0.3})}
    return c


def main():
    DX, DY = 1148, 920
    pos = lambda k: (X0 + (k % 3) * DX, Y0 + (k // 3) * DY)
    L = []
    items = [{"id": uid(), "x": X0 - 60, "y": Y0 - 60, "w": 3 * DX + 860, "h": 4 * DY + 220, "type": "group",
              "title": "ChoralRoot takeover - proposal 5 as built (the owner's notes, Fable's review), from the "
                       "firmware, 2026-11-20", "color": "#ffb000"}]
    words = V4["KN_HOME_W"]

    # A. Shift held: the pads of columns 7-8
    sh = run([S % 900], 1000)
    c = ctl(V["pads"](), {i: ("", None) for i in range(16)}, KN_HOME, "Home",
            {"label": "Shortcuts", "function": "The eight FM-1 buttons as on the pads; inverted = on."}, words=words)
    shift_meta(c)
    L.append(("A. Shift held: the FM-1's buttons on the pads (columns 7-8)",
              "The Shift screen's 2 x 4 grid is the pads of columns 7-8, pad for name: KEY LOCK / LATCH PERF / B.PRF FX "
              "/ BASS BEAT. Tap: on / off; held: its settings. S2 Settings, S8 sound editor (icons lit).",
              scr(sh), leds(common5(c), sh), None,
              "Shift + a chord key still LOCKs it (columns 1-4 as before). No popup on a tap: the inverted name and the "
              "pad's light say it, the screen reader says \"Key, on\"."))

    # B. a shortcut held: its settings; Back: home
    pf = run([S % 900, "pad:%d@250+600" % SCP[3]], 1400)
    c = ctl(V["pads"](), V4["steps_seq"](), KN_PERF, "PERF settings", {"label": "PERF settings", "function":
            "PERF, on / off, the page big, a dash each."})
    knobs_as_built(c, KN_PERF, "PERF settings")
    L.append(("B. Shift + the PERF pad held: its settings; Back: home",
              "A pad held past the hold time opens its settings, which stay open; the wheel turns the page, knobs 1-4 "
              "the page's four. Back: home at once.", scr(pf), leds(common5(c), pf),
              [(scr(pf), 1500), (scr(run([S % 900, "pad:%d@250+600" % SCP[3], "button:back@1500+50"], 1800)), 1500)],
              "Animated (hover): the PERF settings, then Back: home (said \"Home\")."))

    # C. the Settings
    se0 = run([S % 300, "step:1@250+50"], 900)
    se1 = run([S % 300, "step:1@250+50", "knob:0@600:1", "button:jogclick@700+50", "knob:0@800:-30"], 1000)
    c = ctl(V["pads"](), V4["steps_seq"](), KN_HOME, "Settings", {"label": "Settings", "function": "RemoteFM1's list: "
            "four rows, the row inverted; editing: framed, the value inverted."}, words=words)
    for i in range(8):
        c[f"knob-{i + 1}"] = {"meta": {"label": "", "function": "Nothing in the Settings (as RemoteFM1's)"}}
    c["wheel"]["meta"] = {"label": "ROW / VALUE", "function": "The row; after a click, its value", "turn": "Row / value",
                          "click": "Edit / done"}
    L.append(("C. Shift + S2: the Settings",
              "Velocity (Dynamic, Fixed, Soft, Hard), Fixed level, then the FM-1's Play Style, Extension Addition, "
              "Secret Chords, Sus window, chords, bass mode, single notes, split, MIDI channels, clock, CC.",
              scr(se0), leds(common5(c), se0), [(scr(se0), 1500), (scr(se1), 1500)],
              "Animated (hover): the list; Fixed level being edited. Values apply at once; Back while editing: done, "
              "again: home. Saved with Capture."))

    # D. the sound editor
    ec = run([S % 300, "step:7@250+50"], 700)
    ed = run([S % 300, "step:7@250+50"], 2000)
    ef = run([S % 300, "step:7@250+50", "pad:29@1800+50"], 2100)
    pe, pem = V4["pads_editor"](0)
    c = ctl(pe, V4["steps_seq"](), KN_EDIT, "Editor, OSC", {"label": "Editor", "function": "A card on entering; then "
            "the engine, the group big, the screen's name."}, padmeta=pem)
    knobs_as_built(c, KN_EDIT, "Editor, OSC", note="Knobs 1-4 the lane on the wheel, 5-8 the lane under it")
    L.append(("D. Shift + S8: the sound editor",
              "Entering: a card (Sound editor / Chord), said \"Sound editor, chord, Oscillators\". The memories' pads "
              "are the groups, each its colour (current full); the eighth chord / bass. Back: home.",
              scr(ed), leds(common5(c), ed), [(scr(ec), 1200), (scr(ed), 1500), (scr(ef), 1500)],
              "Animated (hover): the card, OSC, FILT. Shift + S8 again or Back: home, the knobs and pads the home's."))

    # E. Copy / paste
    cp = run(DM7 + ["step:0@1500+50", "step:4@1550+50", "button:copy@1700+1000", "step:0@1800+50"], 2400)
    c = ctl(V["pads"](), V4["steps_seq"](), KN_HOME, "Home", {"label": "Chord view", "function": "Home; the "
            "clipboard speaks (\"Step 1 copied\", \"Pasted to step 3\")."}, words=words)
    L.append(("E. Copy held + a step: copied; + an empty step: pasted",
              "As RemoteFM14Move's sequencer: the copied step blinks white, the Copy LED lit while held, blinking while "
              "a paste waits (Copy let go after a copy: the next step tap pastes).",
              scr(cp), leds(common5(c), cp), None,
              "Copy tapped alone: a waiting paste dropped (\"Paste dropped\"), else the loop copied into the next "
              "empty slot (\"Loop copied, slot 2\")."))

    # F. Delete: cut, Delete + Play, Shift + Delete
    ct = run(DM7 + ["step:0@1500+50", "button:delete@1700+1200", "step:0@1800+50"], 2400)
    c = ctl(V["pads"](), V4["steps_seq"](), KN_HOME, "Home", {"label": "Chord view", "function": "Home."}, words=words)
    L.append(("F. Delete held + a step: cut; + Play: the loop cleared",
              "Delete held + a step: cut (it blinks red), the next step pastes it (replacing), a step after that cuts "
              "again. Delete + a memory: cleared. Delete + Play: the loop cleared. Shift + Delete: PANIC.",
              scr(ct), leds(common5(c), ct),
              [(scr(ct), 1500), (scr(run(DM7 + ["step:0@1500+50", "button:delete@1700+300", "button:play@1800+50"],
                                         2300)), 1500),
               (scr(run([S % 300, "button:delete@250+50"], 600)), 1500)],
              "Animated (hover): the cut, Delete + Play (Loop cleared), Shift + Delete (PANIC). Delete tapped alone: "
              "a hint, nothing else."))

    # G. a step held
    sh_ = run(DM7 + ["step:0@1500+50", "step:0@2200+900"], 3000)
    c = ctl(V["pads"](), V4["steps_seq"](), V["KN_STEP"], "Step held", {"label": "Step", "function": "Step n, its "
            "chord big."})
    L.append(("G. A step held: its root and chord keys lit, a preview",
              "Step 1 holds Dm7: D's pad and MIN, m7 lit white while it is held. Stopped, held past the hold time: the "
              "step is heard once (its length, its ratchet); a quick tap: not.", scr(sh_), leds(common5(c), sh_), None,
              "No preview while Copy or Delete is held."))

    # H. Iso Key
    ik = run(["button:menu@200+400", "pad:17@300+50", "pad:9@900+900"], 1500)
    pi, pim = V4["pads_iso"]()
    c = ctl(pi, V4["steps_seq"](), KN_HOME, "Home", {"label": "Iso Key", "function": "Iso Key top left, the chord."},
            words=words, padmeta=pim)
    for rr in range(1, 5):
        for k in range(1, 9):
            c[f"pad-{rr}-{k}"]["meta"]["description"] = ("In key only: each row up to the scale note nearest a 4th "
                                                          "(7 notes: 3 degrees; pentatonics: 2), the tonic yellow.")
    L.append(("H. Iso Key: all 32 pads in the key",
              "Menu held + the pad right of Iso (under Scale). Each row starts on the scale note nearest a fourth above "
              "the row below; every pad in the key, the tonic yellow.", scr(ik), leds(common5(c), ik), None,
              "Said \"Layout Isomorphic, in key\". Shift held: the chord keys back on columns 1-4, to LOCK one."))

    # I. Perf amount
    pa = run(["touch:2@200+400", "knob:3@250:3"], 500)
    c = ctl(V["pads"](), V4["steps_seq"](), KN_HOME, "Home", {"label": "Knob", "function": "Name, value big, a bar."},
            words=words)
    c["knob-3"]["meta"]["description"] = "Three encoder ticks a step (proposal 5: a tick a step was too twitchy)."
    L.append(("I. Perf amount: three ticks a step",
              "Knob 3 (Perf amount) now moves a step every three encoder ticks, as the FM-1's KNOB 3 at its detent.",
              scr(pa), leds(common5(c), pa), None, "The other home knobs as before."))

    for k, (name, desc, s, c, frames, nt) in enumerate(L):
        x, y = pos(k)
        labels_from_leds(c)
        items.append(V4["layout"](x, y, name, desc, s, c, frames))
        items.append({"id": uid(), "x": x, "y": y + 640, "w": 1048, "h": 200, "type": "note", "text": nt,
                      "color": "#ffe2a6"})
    items.append({"id": uid(), "x": X0 + 3 * DX, "y": Y0, "w": 760, "h": 560, "type": "note", "color": "#ffb3a7",
                  "text": "THE OWNER'S NOTES (2026-11-20) -> HERE\n- PANIC: Shift + Delete (F). Delete alone: a hint.\n"
                          "- Back: home from anywhere (B, C, D).\n- Shift shortcuts on the pads, as the screen shows "
                          "them; no popups (A).\n- Shift + steps with icons: S2 Settings, RemoteFM1-style, Velocity "
                          "Fixed etc. (C); S8 the sound editor (D).\n- Iso Key, in-key 4ths (H).\n- Copy / Delete as "
                          "RemoteFM14Move: cut, copy, paste (E, F); Delete + Play clears the loop (F).\n- A step "
                          "held: preview (stopped), root and chord keys lit (G).\n- Perf amount: 3 ticks a step (I).\n"
                          "Fable's review: shortcut pads on columns 7-8; Copy alone as RemoteFM14Move; the in-key row "
                          "rule; no labels at the editor's foot; one-shot preview."})
    doc = {"format": "ableton-move-designer", "version": 3, "name": "ChoralRoot takeover, proposal 5", "items": items}
    json.dump(doc, open(ARGS.out, "w"), indent=1)
    print("items", len(items))


main()
