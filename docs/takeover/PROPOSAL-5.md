# ChoralRoot takeover — proposal 5 (the owner's notes, 2026-11-20)

The owner's notes, then what each becomes. "Now" is the build on the Move (as built, make_cr_v5.py).

| # | Owner's note | Now | Proposal 5 |
|---|---|---|---|
| 1 | PANIC on Shift + Delete, not Delete | Delete tapped alone: PANIC | **Shift + Delete: PANIC** (the inverted PANIC screen). Delete alone does nothing but hold (the clear modifier) |
| 2 | Back brings you home, out of settings | Back closes one level (Options group -> Options -> ..., a layer over the editor -> the editor) | **Back: home, from anywhere** (settings, Options, the editor, popups at once). Said "Home" |
| 3 | The Shift shortcuts on the pads, mapped as the screen shows them | Shift + steps 1-10 / 16 | **Shift held: the eight FM-1 buttons on the pads of columns 7-8**, rows 1-4: the screen's 2 x 4 grid, pad for name (KEY LOCK / LATCH PERF / B.PRF FX / BASS BEAT). Lit in their colours, full = on, dim = off. Tap: on / off (no popup: the inverted name says it; speech stays). Held: its settings. Shift + a chord key still LOCKs it (columns 1-4 unchanged) |
| 3a | Shift + steps for other things, the icon LEDs showing which | — | **Shift held: a step with a job lights its icon** (and the step). **S2: Settings** (the global menu, RemoteFM1-style: a list, the wheel moves, a click edits, Back home): Fixed velocity, Velocity curve, MIDI, Hold time ... (the FM-1's Options). **S8: Sound editor**. Other steps dark |
| 4 | An in-key isomorphic 4ths layout | ISO: chromatic 4ths | **ISO K**: all 32 pads in the key only, each row 3 scale degrees up (a 4th in the scale), the tonic yellow. Menu held: the pad right of ISO |
| 5 | Sequencer: Copy / Delete as RemoteFM14Move | Delete + step: clear; Copy: LOCK | **As RemoteFM14Move's seq_ui.c**: Copy held + a step with a chord: copied (more steps in the same hold join, gaps kept, anchored at the first); Copy held + an empty step: pasted. Copy let go without a paste: paste armed, the next plain step tap pastes once. Delete held + a step: cut (copied, cleared); the next steps in that hold paste (replacing), the same one again cuts. Copied steps blink white, cut red; the Copy LED blinks while a paste is armed; said "Step 3 copied", "Pasted to step 7", "Step 7 replaced". Copy tapped alone: drops a pending paste, else LOCK on / off (kept from proposal 4) |
| 5a | A step held and stopped plays a preview; a quick tap does not | — | **A step held past the hold time while stopped: its chord sounds until let go** |
| 5b | A step held lights its root and its chord keys | — | **Held: the root's pad white, its type and extension pads white** (as if played) |
| 5c | Delete held + Play: the loop's steps cleared | — | **Delete + Play: the slot's loop cleared** ("Loop cleared"; Undo brings it back) |
| 6 | The sound editor: obvious, on Shift + S8 with its icon | Shift + S9 / S10, unlabelled | **Shift + S8** (icon lit). Entering: a full-screen card "Sound editor / chord" for a moment, the memory pads become the group pads (labelled on the screen's foot), the eighth chord / bass. **Back: home** |
| 7 | Perf amount too twitchy | a tick a step | **3 ticks a step** (as the FM-1's KNOB 3 at its detent) |
