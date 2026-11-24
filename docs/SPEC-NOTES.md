# ep1: where the implementation departs from SPEC.md, and why

Stages 1 and 2 of the Engine page (engine-params/BUILD-PLAN.md), built on 2026-11-24 in FM1VST `rfm1-ep1`,
rfm1cs-abletonmove `session-view-fix` and RemoteFM14Move `ep1`. SPEC.md and DESIGN.md (engine-params/, read-only here)
are the reference; each item below says what the code does instead and why. Items not listed are as specified.

## The firmware's side (`core-api/rfm1_ep.h`) and the shim

1. **`epoch` is optional.** The shim calls `layer()` for every layer after each frame and compares the answers itself
   (two calls for ChoralRoot), so a firmware need not count its engine and channel changes. `epoch` (0 or a function)
   only says the description must be made again (a firmware that builds its tables at run time).
2. **`text()` has no parameter argument**, so one function cannot word many parameters. Kept for a firmware with one
   curve per function; ChoralRoot's adapter gives `names` instead, made at init from the firmware's own formatter for
   every value (see 9). Proposal for a later version: `text2(pid, v, buf, n)`.
3. **(Stage 3) The holds as SPEC.md 5, with these precisions.** Up to 64 a layer (a LOCK past them answers -4). The
   locked value kept is the one the firmware reads back after the write (`get`), not the one asked, so a parameter
   the firmware rounds or skips (an alias) is not taken for an outside edit. SET while held moves the base only while
   the firmware still holds the locked value; after an outside edit (the hold already stale) it is a plain SET. The
   check against outside edits runs after each frame and before a SET / LOCK of the layer; an engine switch is seen by
   the layer's engine in the record differing from the one at the first LOCK. FREE_ALL's layer 0xFF (or -1): every
   layer. LOCK / FREE refresh only that parameter's value in the record (not the whole record, as a SET does).
   **`rfm1_ep_host` is 2 while a lock's write or its return runs** (1 for a SET): ChoralRoot's adapter then puts the
   part's edited flag and its "slot's sound as stored" back, so a step lock never makes the sound "edited" (the
   ctest compares every lock write with the SET's bytes). A lock on the chord's send still turns FX on, as every send
   turned on the device does (`cu_send_set`).
4. **Every SET refreshes the whole record** (all values of all layers, the layers, the cells) before it returns, so
   a host reading right after its call sees the result. ChoralRoot: about 170 `get` calls, 65 µs in wasm3 on the Mac
   (the Move runs wasm2c: a fraction of that). Stage 3's locks may want a refresh of the one value only.
5. **A GLOBAL parameter's SET uses layer 0** whatever layer is named (none in ChoralRoot yet).
6. **The shim refuses the description** (no ep1: `rfm1_ep()` = 0) when a pid is 0, 0xFFFF or twice, when a callback
   is missing, or when the blob would pass 64 KiB.
7. **The cells**: a cell's name is the `words()` name when given, else the parameter's; its text is `words()`'s, else
   empty. The cells are kept for every watched knob, DYN or not (ChoralRoot words every cell; the core uses a cell's
   words for DYN parameters).
8. **`rfm1_run`**: `--ep-info [--out FILE]` (new: the record as JSON, the blob written; `rfm1_check --run` uses it),
   `--ep`, `--ep-set L:PID=V@MS`, `--ep-watch L:P@MS` (new). `--ep-lock` / `--ep-free` come with stage 3.

## ChoralRoot's adapter (`core-glue/choralroot/cr_ep.c`) against INVENTORY.md

9. **The words are the firmware's own.** For every parameter the editor's text (`ce_text`) is made for every value;
   a format (INT with or without a sign and a unit, FIXED 1 or 2, PERCENT) is used only when it gives exactly the same
   words for every value, else a TABLE of them (62 tables, shared by content). No parameter needed numbers instead of
   its words. So ON/OFF parameters are a TABLE "off" / "on" (the editor's lower case), not ONOFF's "Off" / "On".
10. **Two lanes on one page: lane A on knobs 1-4, lane B on 5-8, always** (INVENTORY wrote "MOD 1/2: Src, Dest,
    Amount (each slot), -, -"; built: Source 1, Dest 1, Amount 1, -, Source 2, Dest 2, Amount 2, -), so a parameter's
    knob is the same on every page of a kind, and matches the editor's four-knob lanes.
11. **EXPR is VEL dest, VEL amt, AT dest, AT amt** (knobs 1-4). The sources are not editable in the editor (the rows
    are fixed VEL and AT), so they are not parameters. A slot not yet set reads as the defaults the editor's knob
    starts from (AMP / CUT, 63 / the aftertouch amount), its words "AMP" / "CUT" and "auto" (DYN), and its first SET
    writes those defaults first, as `ce_knob` does.
12. **Pages as built** (117 in all, 632 parameters, 732 knob slots): FM6 23, FM TONE 17 (CRUSH and AMP 2 are newer
    than INVENTORY's list), VA 19, CZ-1 26 (each step envelope's END page is a page; INVENTORY grouped them), P5 14
    (FILTER alone; FILT ENV = the plan's "ENV 2 · filter" with its "ENV 2 +" (Velocity, Key trk, Aftertch); AMP ENV
    likewise), WHEEL 6, GRAIN 14 (AMP ENV and MOD ENV from the plan's titles). Single-lane pages are paired with the
    next single lane of their group when it is their "+" page or has a single cell (the VA's VOICE / Uspread beside
    LFO SYN, as INVENTORY's "LFO SYNC").
13. **The optional layer pages of INVENTORY 1.5** (PERFORM, VOICING, the global FX) are not offered: stage 2 is the
    engines and the shared EXPR / FX / MIX pages. They can be appended later (new pids).
14. **The engine's name is the firmware's own**: "P5" (the owner's name for the player, 2026-10-13), not "PROPHET".
15. **The ids' keys**: `<engine id>/<deep page title>/<column label>` for an engine's own cell (the editor's middle dot
    in a plan's title as "."), `*/TRK/<label>` for a part parameter several engines share (Level, Pan, the sends, the
    platform's ENV / LFO / MOD), `<engine id>/TRK/E<n>` for an engine's P_E slots (WHEEL), `*/EXPR/<VEL|AT> <DEST|AMT>`.
    The frozen table `cr_ep_ids.h` holds key, range, default and list-ness per pid; the ctest fails on a cell without
    a pid, a pid whose cell is gone (it must be marked retired), or a changed range / default / list-ness. SPEC.md's
    separate `ep1_ids` against "the previous release's table": there is no release yet; the committed table is that
    record (append-only), and when 0.45-ep1 ships the test should also compare against the tagged copy.
16. **FM TONE's AMP Drive** is a deep cell that is the part's P_DIST (`ce_send_ref`): it has its own pid (FM TONE's
    AMP page) and shares its value with the FX page's Drive pid, as the editor shows one value in both places.
17. **The chord's sends are the FX amounts** (`cu_send_set`: and FX on, as every send turned on the device); not
    flagged SOUND. The bass's are its own.
18. **An F_ENUM SET skips aliases in the direction of the change** (params.c `enum_step`, as the editor's knob), so a
    Move knob stepping through GRAIN's source types never sticks on a retired alias. FM TONE's LFO DEST, which the
    FM-1's knob steps in the Digitone's order, steps in stored-value order on the Move (the id's values are the stored
    ones).
19. **F_FIX2 (hundredths)**: FIXED 2, step 100 (the editor's whole number a tick), Shift a hundredth.
20. **The layers**: Chord's channel is Options > MIDI Perform, Bass's MIDI Bass (0..15, or none when Off); the bass
    layer's engine is none while BASS is off (`cs.bass_on`), so the core shows "Off".

## The host (RemoteFM14Move)

21. **The record is asked for again after a snapshot's restore** (`fw_snap_restore` calls `rfm1_ep()` once): the
    snapshot format is unchanged (no new field), and once the module opened ep1 the call changes nothing.
22. **The input queue is 512** (stage 3); a snapshot holds 256 queued inputs (the stored format's), so a snapshot asked
    while more wait is refused for that moment (the host tries again, as for any refused snapshot).
23. **`/api/status`'s per-slot `ep1`**: `desc`, `acts`, `watches`, `sets`, `refused`, `poll_us`, `layers`, `wlayer`,
    `wpage`, `cells` (SPEC.md 8 named "layers, engines, desc length, counts of sets / locks a second": counts since
    the slot's boot, not a rate).
24. **"Read back through the firmware's own readout"**: a host SET draws nothing and pushes no rd1 readout (SPEC.md 5),
    so the e2e test reads the firmware's own words from the record's watched cells (`ce_text` of the value now), which
    is what the FM-1's screen would say.

## The core (rfm1cs-abletonmove, from its builder's report)

25. The page counts as present only once `rfm_move_ep_layers` was called after a description.
26. A new description resets the layers and values: the host sends `_layers` and every layer's `_values` after
    `_describe` (the host does: `ep_poll`). A brief EP_WATCH -1 can come between describe and layers.
27. "Back to FM-1" for a page no longer offered happens at the next input or block, not inside the API calls (so a
    NULL description followed by a new one keeps the page).
28. The arrival popup also shows when the track's target slot or layer changes while ENGINE shows; not when Menu
    flips the view onto the track view.
29. The engine-change popup covers off -> an engine ("Bass / FM6") and an engine -> off ("Bass / Off").
30. On a layer that is off, the jog's click opens no list; it shows the "Off" popup again.
31. A held step on ENGINE gives the knobs to the TRIG page's parameters, as on the FM-1 page, until stage 3's locks.
    Delete + a knob on ENGINE is a plain turn for now.
32. A DYN cell's words are used while no sent value still waits for the record (the shown value is the firmware's).
33. The per-engine page memory is per slot, shared by its layers (a track on the bass layer arrives on the page last
    shown on that engine, whichever layer showed it); each track remembers (slot, engine id, page id), RAM only.
34. `rfm_move_ep_check` accepts a header larger than 32, engines with no pages, repeated page ids and empty tables; it
    enforces unique pids, the default inside the range, FIXED 1..4, NOTE 3..4 and table strings of at most 10
    printable characters; it does not check bytes after a name's NUL, reserved fields or the group.
35. NOTE names use sharps ("C#3"); negative values floor for the octave.
36. The value bar shows for every format, LIST and ONOFF included.
37. The list's title line and the arrival popup's layer / engine line use the 1x font (the only small text), as
    DESIGN.md's layouts do; every value and name is 2x or larger.
38. The watch includes the track: selecting another track sends EP_WATCH again even for the same page.

## A proposal: recording knob turns as locks (the owner's answer 4, a later stage)

The owner: "Yes but we should have a record toggle for automation", for CCs and engine parameters together. Proposal:

- **Where**: a row **Automate** (Off / On) in the Record Shift menu (Shift + the Record step, beside the count-in and
  the capture rows), so it sits with the other recording choices and is reached the same way; **Shift + Record**
  (the button) toggles it directly, at the press, with a one-second popup "Automate On" / "Off" and speech.
- **What it shows**: while armed, the Record button lights in a second colour (amber, against the note recording's
  red), so arming is visible at a glance and never confused with note recording; when both are on, it alternates.
- **What it does**: with Automate on and the transport playing in a clip of the track, a knob turned on ENGINE (or on
  a CC bank) writes a lock on each step the playhead passes while the knob is touched (the Move's own automation),
  one undo step per touch; a touched, unturned knob writes nothing. The steps written show the lock dot as any lock.
  Recording notes (Record) and Automate are independent.
- **Why a separate toggle**: the owner's word; and a performance turn of a knob must stay a performance unless asked
  for, as on Elektron boxes (their "parameter recording" needs REC armed too, but here REC already means notes).
