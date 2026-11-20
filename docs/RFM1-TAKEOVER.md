# Move takeover (`tk1`): a firmware's own Ableton Move interface

Draft 1, 2026-11-19. Status: **a proposal for the owner**, nothing built yet.

A host that runs `.rfm1` firmwares on an Ableton Move (RemoteFM1 on the Move: remoteFM14Move with
rfm1cs-abletonmove) normally turns the Move into an FM-1: the pads are the keybed, the knobs the FM-1's encoders, the
screen a picture of the FM-1's. With `tk1` a firmware can also bring **its own Move interface**, made for the Move: it
draws the Move's 128 x 64 screen and its pad and button LEDs, and it gets the Move's raw input. The player switches
between the two with **Track 1**.

The extension sits beside `cpu2` and `rd1` (docs/RFM1.md): a few exports and a record in the module's memory, no
import, so every host that does not know `tk1` runs the file exactly as before.

## 1. What the player sees

- **Track 1 tapped in FM-1 mode** while the active slot's firmware has a takeover: its takeover mode. Its name is
  shown and said ("CHORDS"). **Track 1 tapped in the takeover** (when the firmware leaves the Track buttons to
  RemoteFM1): FM-1 mode again. A firmware without a takeover: Track 1 is what it always was.
- From MIDI, Control or Sequencer mode, Track 1 goes back to whichever of the two (FM-1 or takeover) the slot was
  left in. Each slot remembers its own.
- **Volume knob touched + Back pressed**: back to FM-1 mode from any takeover. It is the only way out when the
  firmware claims everything, and it works in every takeover, so the player never has to know which parts a firmware
  took.
- What the firmware does not claim keeps its RemoteFM1 meaning: unclaimed Track buttons still switch modes (Track 2
  MIDI, Track 3 Control, Track 4 Sequencer, Back + Track 1..3 the slots); an unclaimed volume knob is still MASTER; an
  unclaimed screen still shows RemoteFM1's.
- A slot switch (Back + Track n, the Firmwares view) leaves the takeover of the slot it leaves; the slot it comes to
  opens in the mode it was left in.
- With the screen reader on, entering and leaving are said ("Chords, takeover" / "FM-1 mode"), and the firmware can
  say its own words (section 4.3).

## 2. Claims

A firmware says which parts of the Move it takes. Each claim covers the controls' input **and** their LEDs.

| Bit | Name | The controls (Move surface MIDI, cable 0) | LEDs (section 4.2's list) |
| --- | --- | --- | --- |
| 0x001 | `SCREEN` | the 128 x 64 screen | |
| 0x002 | `PADS` | the 32 pads: notes 68..99 (0x90 / 0x80), poly pressure (0xA0), channel pressure (0xD0) | 0..31 |
| 0x004 | `STEPS` | the 16 step buttons: notes 16..31 | 32..47, the icons 76..91 |
| 0x008 | `KNOBS` | knobs 1..8: CC 71..78 (relative), touch notes 0..7 | the rings 55..62 |
| 0x010 | `JOG` | the wheel: CC 14 (relative), its click CC 3, touch note 9 | |
| 0x020 | `VOLUME` | the volume knob: CC 79 (relative), touch note 8 | |
| 0x040 | `BUTTONS` | Shift 49, Menu 50, Back 51, Capture 52, Down 54, Up 55, Undo 56, Loop 58, Copy 60, Left 62, Right 63, Mute 88, Delete 119, Play 85, Rec 86, Sample 118 | 48..50, 63..75 |
| 0x080 | `TRACKS` | Track 1..4: CC 43, 42, 41, 40 | 51..54 |
| 0x0FF | `ALL` | everything above | |

Not claimable (the host's, always): the power button, the jacks, the device on USB-A (that is MIDI: the firmware's
MIDI in, as ever).

**Reserved in every takeover:** while the volume knob is touched, a Back press is the way out (section 1) and is not
sent. The firmware has had the touch (if it claims `VOLUME`); the host sends it the touch's release as it leaves.

**Back with `BUTTONS` claimed and `TRACKS` not:** Back + Track n must still switch slots, so the host holds Back
back: the firmware gets Back's press and release together when Back is let go with no Track pressed meanwhile. Such a
firmware cannot use Back held. (Claim `TRACKS` too, or leave Back alone, to have it at once.)

A firmware may change its claims while it runs (one view takes everything, another leaves the Track buttons); the host
reads them after every call (section 4.1). Claims of 0 mean "no takeover now": Track 1 does not offer it, and a
takeover that is open closes.

## 3. The manifest

```json
"wants": ["tk1"],
"takeover": { "name": "CHORDS", "claims": ["screen", "pads", "steps", "knobs", "jog", "buttons"] }
```

`takeover` is informative (a catalog or a firmware browser can mark the firmware "made for Move" without loading it);
the record (section 4) is what the host follows. `name`: at most 15 printable ASCII characters. `claims`: the names of
section 2, lower case, or `["all"]`.

## 4. The module's side

### 4.1 The exports

| Export | Signature | What it does |
| --- | --- | --- |
| `rfm1_tk` | `() -> ptr` | the record below, at a stable address; called once after `rfm1_init` |
| `rfm1_tk_input` | `(pkt) -> i32` | one Move surface packet, a USB-MIDI packet as a u32 (bytes 0..3 little-endian, byte 0 the cable 0 / CIN, as `rfm1_midi_in`), only for claimed controls; returns 0 (reserved) |
| `rfm1_tk_state` | `(on, claims, flags) -> ()` | 1: the takeover is now shown, with these claims granted; 0: it is gone. flags bit 0: the screen reader is on |

The host calls them on the instance's thread like every other export, in time order between `rfm1_tick`s (an
event's time is the firmware's `tick` time when it arrives). Before `rfm1_tk_state(0, ..)` the host sends the release
of everything the firmware was sent as held (pads, steps, buttons, touches), so a firmware never keeps a stuck pad.
The firmware keeps running its FM-1 side meanwhile (its own screen, its panel); in a takeover the host just does not
show it or feed it the Move's controls.

`rfm1_check` requires all three exports or none, and `tk1` in `wants` with them.

### 4.2 The record

At a stable address in the module's memory, little-endian:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u16 | `version`: 1 |
| 2 | u16 | `size`: 1468 |
| 4 | u32 | `claims`: section 2's bits; 0 none now |
| 8 | u32 | `scr_seq`: moves when `screen` changed |
| 12 | u32 | `led_seq`: moves when any of `led` changed |
| 16 | u32 | `say_seq`: moves with each new `say` |
| 20 | u8 | `say_flags`: bit 0 interrupt (stop what is being said first) |
| 21 | u8[3] | reserved, 0 |
| 24 | char[16] | `name`: the mode's name, NUL-terminated (printable ASCII) |
| 40 | char[128] | `say`: words for the screen reader, NUL-terminated (printable ASCII) |
| 168 | u8[1024] | `screen`: 128 x 64, one bit a pixel: 8 pages of 128 columns, a byte 8 pixels down, bit 0 the top (Move Remote's `MR_SCREEN_BYTES`, Schwung's packed display) |
| 1192 | u8[92][3] | `led`: each LED's r, g, b (0..255); a white LED (63..91) uses r as its brightness |

The LEDs' order (rfm1cs-abletonmove's own): 0..31 the pads (bottom-left first, rows of 8), 32..47 the steps, 48
Play, 49 Rec, 50 Sample, 51..54 Track 1..4, 55..62 the knob rings 1..8, 63..75 the white buttons Shift, Menu, Back,
Capture, Down, Up, Undo, Loop, Copy, Left, Right, Mute, Delete, 76..91 the icons under the steps.

The host reads the record after each `rfm1_frame` and after each `rfm1_tk_input` (a pad's light follows the press
without waiting a frame). It takes `screen` when `scr_seq` moved, the claimed LEDs when `led_seq` moved (only the
changed ones go to the Move), and `say` when `say_seq` moved. The firmware writes the record only from its own calls,
never from `rfm1_audio`.

### 4.3 Speech

`say` is for the screen reader: what a sighted player learns from the screen at that moment ("Chord D minor seven",
"Arp, one eighth"). The host passes it on only while speech is on (`rfm1_tk_state`'s flag), so a firmware can skip
making words otherwise. Short phrases; numbers as digits are fine. The host also says the mode's `name` on entering,
so the firmware need not.

## 5. The firmware's side: `core-api/rfm1_move.h`

A header to vendor (MPL-2.0, like `rfm1_ext.h`). An `.rfm1` built with `fm1_add_rfm1(... WANTS tk1)` defines
`FM1_HOST_TK`; the FM-1 build does not, and a firmware's Move interface lives in its own source files that only the
`tk1` build compiles, so the device image is unchanged.

```c
#include "rfm1_move.h"

static void my_input(const uint8_t pkt[4]);            /* a claimed control: decode as the Move sends it */
static void my_state(int on, unsigned claims, unsigned flags);

static const rfm1_tk_ops_t OPS = { my_input, my_state };
rfm1_tk_register("CHORDS", RFM1_TK_ALL & ~RFM1_TK_TRACKS, &OPS);   /* once, from init */

/* in the UI (the firmware's frame, or its input handler) */
uint8_t *px = rfm1_tk_screen();                          /* the 1024 bytes; draw, then */
rfm1_tk_screen_done();                                   /* scr_seq moves */
rfm1_tk_led(RFM1_TK_L_PAD0 + 5, 255, 120, 0);           /* led_seq moves if it changed */
rfm1_tk_white(RFM1_TK_L_SHIFT, 127);
rfm1_tk_claims(RFM1_TK_ALL);                             /* a view that takes the Track buttons too */
rfm1_tk_say("Chord D minor seven", 1);
int shown = rfm1_tk_shown();                             /* 1 while the takeover is on screen */
```

Constants: `RFM1_TK_SCREEN` .. `RFM1_TK_ALL` (section 2), `RFM1_TK_L_*` (section 4.2's indices), and the Move's
numbers for decoding (`RFM1_TK_NOTE_PAD0` 68, `RFM1_TK_CC_BACK` 51 ..).

The shim (`RFM1_TK` in `core-glue/common`) holds the record and makes the three exports call the registered ops.

## 6. What the host does (rfm1cs-abletonmove + remoteFM14Move)

1. After init, `rfm1_tk()`; a record with the right version and size makes the slot takeover-capable.
2. **Track 1** and the **volume + Back** gesture as section 1 says; the core's mode for it is
   `RFM_MOVE_MODE_TAKEOVER`, beside FM-1, MIDI, Control and Sequencer.
3. In the takeover the core decodes each Move event as now; an event of a claimed part goes to the host as an action
   (`RFM_MOVE_ACT_TK_INPUT`, the raw packet) for `rfm1_tk_input`; the rest goes through the core's usual handling,
   as in FM-1 mode.
4. The screen: the firmware's when it claims `SCREEN`, with the core's own popups over it only for the mode's name on
   entering and the host's messages (a load that failed, the Move's battery); else the core's FM-1 screen.
5. The LEDs: each claimed LED from the record, the rest as the core draws them in FM-1 mode.
6. Speech: the core says the mode's name on entering, "FM-1 mode" on leaving, and the firmware's `say` in between.
7. The other slots keep running, unshown, as they do now; MIDI out of the takeover firmware follows the Routing page
   (rfm1cs 0.6.6) like any firmware's.

## 7. Versions

`version` 1, `size` 1468. A later version only adds fields at the end (a host reads what it knows). `wants: tk1`; a
firmware for a later version names `tk2` and keeps the `tk1` exports working.
