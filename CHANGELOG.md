# Changelog

FM1VST's releases. The version is `project(FM1VST VERSION ..)` in CMakeLists.txt (the plugin shows it in its
settings sheet); a release is the tag `v<version>` on it (.github/workflows/release.yml builds and publishes it).

## Unreleased

### LOOM-8, a new core

`loom`: Charles Vestal's [LOOM-8](https://github.com/charlesvestal/fm1-loom) (GPL-3.0-only, on fm1-base and
felucca-bsp, MPL-2.0), a pocket groovebox: eight tracks on nine engines, a 64-step sequencer with locks, weave, scenes
and a song, and a user sample bank loaded over USB. Pinned at **v0.6-beta** (2026-10-21), a beta on purpose: every
LOOM-8 release so far is a "-beta" tag, and this is the newest (the other pins stay on stable releases).

- Built like AMB-1 on the fm1base glue, unmodified; its second core's jobs run in order on the host's thread, as in
  LOOM-8's own simulator and browser emulator (the same split render, the same sums).
- The managers answer: the samples page and `tools/loom_samples.py` (the user sample bank), fm1-base's backup page
  (backup and restore of the projects and user presets, SEL on the panel to confirm), the installer's identity
  (FM-1_10000600) and its log readout, a screen reader. "Show to editors" names the port "LOOM-8 FM-1 (FM1VST)" and
  links the samples page.
- **In the public release** (14 cores with the fm1emu engine): its factory 909 hi-hat ships unchanged, the owner's
  call ("ship the original, it is from the emulated 909 on the schwung firmware for Ableton Move that Charles Vestal
  made"); the About page's credits name that origin. X0X stays gated (CORES-PLAN.md 6).

### Firmware updates

Each pin moved to its firmware's latest stable release:

| core | was | now | glue |
|---|---|---|---|
| ChoralRoot | 0.25.1 | 0.26.1 | version only |
| CoralFlower | 0.2 | 0.4.2 | flash_sync writes 0.4's settings log, its sequencer store and the parts' sound edits; the generate list (no melodee_cz1.h); the golden reference built at CR_VERSION 0.4.2 |
| Melodee | 1.4.4 | 1.4.5 | version only; its USB port is now "Melodee FM-1", and so is the editor port's name |
| SLOOP | 2.7.1 | 2.8 | version only (gated) |
| sloopDX | 4.0 (eeec0a6) | 4.0 (tag v4.0) | none: the tag adds CI and installer changes only (gated) |
| No Man Sky FM | 0.4 | 0.5 (32e7f4c, no tag: its README's current version) | two more generated headers; projects in format 7 (gated) |
| OberVave | 1.5.1 | 1.5.3 | version only |
| Frodhes | 0.9.2 | 0.9.4 | version only; the Inter Tight font's OFL in its credits |
| FM-106 | Beta4 | Beta5 | version only (gated); Beta5 still refuses the commit to flash (the user bank does not fit its 8 KiB), as sysex_test records |

Unchanged, already at their latest stable release: Felucca 1.6.1, GHOULBOX 2.3.2, PurpleMonkey 0.11, X0X 1.1.0,
FoMni-1 0.4, FiMba-1 0.6.1, AMB-1 1.3, Fart Machine One 1.0.0, ORBIT 0.7.1, DUELL 0.2.20-beta, Rainbow Mode 1.1.5-rb
(upstream changed only its landing page), JIANT FM 0.9.9 (upstream's installer still calls 0.9.9 stable; its newer
builds are beta previews).

### Shift-click to hold

A mouse holds one control at a time; now several stay down. **Shift-click** a button (FX .. REC, OCT- / OCT+) or a
key and it latches: down on the click, still down after the mouse is up, marked by a ring and a dot. Plain clicks on
the others stay momentary (latch HOME, then tap keys); Shift-click latches more. Any click on a latched control lets
it go; **Esc** lets go of all of them (Esc is OCT- again once none is latched), and so do the focus leaving the editor
for good, a reboot or a firmware switch, and closing the editor. Right-click / ctrl-click still latch as before.
Shift on a knob changes nothing; the computer keys are unchanged. The first time Shift is held over the panel, a
toast says "Shift-click to hold" (once: settings.json remembers it). A power-on now lets go of whatever the panel
held, so a latched button never reaches a firmware's boot.

## 0.5.1 (2026-10-19): copy the screen

### Copy the screen

The firmware's screen can now go straight into an image editor (Photoshop, Figma, Affinity, Pixelmator, Preview's
File > New from Clipboard) for mock-ups: exactly the 240 × 240 frame the plugin shows, pixel for pixel, in the
colours it draws them.

- **Right-click (or ctrl-click) the screen**, in the device view (the panel's screen or the big LCD) and in the
  screen view, for its menu: **Copy screen** (240 × 240), **Copy screen ×2 / ×4** (480 / 960, each pixel a sharp
  block), **Save screen as PNG** (×1, ×2 or ×4; the file chooser starts at
  `~/Desktop/<firmware>-<version>-<date>-<time>.png`), **Advanced** (the raw frame), **Freeze screen** and
  **Settings…**. It works in the clean views too, with the settings out of sight. A right-click elsewhere off the
  controls still opens the settings.
- **Option + right-click** copies at once, without the menu.
- **Shift + Cmd + C** copies while the plugin has the keyboard focus. The Standalone app always gets it; some hosts
  keep the key for themselves.
- **The settings sheet** has a **Screen** row under View: Copy, and the scale (×1, ×2, ×4) that Copy and the
  shortcut use, with Save as PNG, the raw frame and Freeze in its menu.
- **On the clipboard:** a PNG (public.png) and a TIFF (public.tiff), both sRGB with no gamma or colour conversion, so
  apps that paste only one kind still get it. A short note over the screen confirms each copy ("Screen copied
  (240×240)").
- **Freeze screen** holds the shown frame while the firmware and the sound run on, so you can pick the moment to copy.
  Choose it again to unfreeze.
- **Advanced: Copy raw RGB565 (.bin)** puts the framebuffer itself on the clipboard: 115,200 bytes, RGB565
  little-endian, row by row. **Save raw RGB565 as .bin…** writes the same bytes to a file.

## 0.5.0 (2026-10-19): the firmwares' own editors and managers

### Show to editors (macOS)

A new switch in the settings sheet, **Show to editors**, makes a virtual MIDI port named like the loaded firmware's
USB port with " (FM1VST)" after it: "Felucca (FM1VST)", "ChoralRoot FM-1 (FM1VST)", "Melodee (FM1VST)", "JIANT FM
(FM1VST)" and so on. A firmware's own web editor or manager in Chrome finds FM1VST as if it were an FM-1 and talks to
the firmware running in the plugin: its info, its backup (list, download, restore) and its preset stores. The sheet
links the firmware's manager page where one is known. A second instance gets "(FM1VST 2)". The switch is saved with
the project.

- **The firmwares answer.** The cores now run their own SysEx services, as on the device: the ChoralRoot family's
  device manager and backup, Felucca's editor and backup page and those of its forks (JIANT, GHOULBOX, Rainbow,
  DUELL), Melodee's editor and shared library, the SLOOP family's editors (SLOOP, sloopDX, ORBIT, NoMansky),
  X0X's backup page, FM-106's Librarian, and the M-UPGRADE identity the installers and fm1-editor.com ask for.
- **Also while the host is stopped.** When no audio block has come for 50 ms, the port runs the firmware's clock
  itself, so a manager is answered (in a few ms) with the transport stopped, and a press on the plugin's panel still
  reaches the firmware (X0X's restore asks for SEL). A firmware's own reboot (ChoralRoot's RESTART) powers it on
  again from its flash.
- **Kept apart from the DAW.** The port's traffic never appears in the track's MIDI, and the firmware's SysEx goes
  to the port only while it is on.
- **No updates through it.** The update keys an installer sends (UBOOT, OTA start) are dropped, and the cores ignore
  them: an installer page pointed at the port cannot halt the firmware.

## 0.4.0 (2026-10-19): every Hub firmware on an emulated FM-1; ChoralRoot and Melodee

### Every Firmware Hub firmware, emulated

A new core, `fm1emu` ("FM-1 (emulated)"), runs Joakim Eriksson's [fm1-emu](https://github.com/joakimeriksson/fm1-emu)
(MIT): an emulation of the FM-1's chip that runs a firmware's own update package (`.fwsc`), so any firmware on
[The Firmware Hub](https://thefirmwarehub.com/) plays in FM1VST, not just the ones built as native cores. The settings
sheet has one firmware list for both kinds, with an All / Native / Emulated filter and engine badges; a firmware with
both offers an Engine switch. Picking a Hub firmware downloads its package once, checks its SHA-256 and size against
the Hub's catalog and keeps it in ~/Library/Application Support/FM1VST/hub/ (offline: the cached catalog, or a bundled
list). "Your own .fwsc" loads a local package, the stock M-VAVE firmware included. FM1VST bundles no firmware for
this: each package is downloaded at your request.

- **Emulated clock:** Auto, 240, 320, 360, 480 or 640 MHz. The FM-1 runs at about 320-360 MHz; Auto starts at 360 and
  steps down when this Mac can't keep up. In a DAW it waits for the transport to stop, since a step reboots the
  firmware.
- **Saves:** the emulated flash is kept in the project (about 0.5 MB, gzipped) with the package's id and SHA-256; a
  project whose package isn't cached fetches it again.
- **Source link** for each firmware, in the sheet.
- fm1-emu carries RemoteFM1's patches: the P33 power block (Melodee boots), the TIMER4 count, and the JIT and pacing
  work, so emulation renders steadily (100 ms LED averaging, as on the website).

### ChoralRoot and Melodee in the public build

ChoralRoot 0.25.1 and Melodee are no longer gated (the owner's call, 2026-10-19): the public build carries 12 cores,
and ChoralRoot is its default firmware (it was Felucca while ChoralRoot was left out). Their CZ-1 tones (Casio) and
Prophet-5 programs (Sequential) ship compiled into the firmware, as the firmwares' own packages carry them, and the
cores' credits name both owners. No raw .syx goes out: tools/package_release.sh fails a release that holds a .syx,
anything from a core's assets/cz1-factory or assets/prophet5-factory, or a file with the content of any core's .syx.
Melodee: 1.4.3 -> 1.4.4 (a release-tooling update; the firmware's only change is its version string).

## 0.3.1 (2026-10-19): an installer package

### An installer package

The release now has `FM1VST-0.3.1-macOS-arm64.pkg` beside the zip: a macOS installer with a choice of the Desktop
app (FM1VST.app into /Applications), the VST3 (into /Library/Audio/Plug-Ins/VST3) and the Audio Unit (into
/Library/Audio/Plug-Ins/Components), all three on by default. It replaces an older FM1VST in those places (the
bundles are not relocatable, so a copy elsewhere is left alone) and refreshes the Audio Unit cache after installing.
It is unsigned, like the zip: right-click > Open, or System Settings > Privacy & Security > Open Anyway (INSTALL.txt).
What it installs carries no quarantine flag, so there is no `xattr` step. A 0.3.0 copied by hand into
~/Library/Audio/Plug-Ins is not touched: delete it, or a DAW lists FM1VST twice. The zip stays, for copying by hand.

### Licences

`licences/JUCE/AGPL-3.0.txt`: the full text of the AGPLv3, the licence JUCE 8 is used under (the FSF's text).

## 0.3.0 (2026-10-18): the first public release

The first build published as a download: macOS 11 or later, Apple silicon, AU, VST3 and Standalone, ad-hoc signed
(not notarised: INSTALL.txt in the zip says how to allow it).

### 21 firmware cores

FM1VST runs FM-1 firmwares as loadable core modules (`.fm1core`), each built from its firmware's own unmodified
source (a pinned submodule), and switches between them in place. 21 firmwares build as cores (CORES-PLAN.md):

| family | cores |
|---|---|
| felucca_web | Felucca 1.6.1, GHOULBOX 2.3.2, JIANT FM 0.9.9 (0912abb), Rainbow Mode 1.1.5-rb, DUELL 0.2.20-beta |
| choralroot | ChoralRoot 0.25.1, PurpleMonkey 0.11, CoralFlower 0.2 |
| plat / fm1base | FoMni-1 0.4, FiMba-1 0.6.1, AMB-1 1.3, X0X 1.1.0, Fart Machine One 1.0.0 |
| dinghy | OberVave 1.5.1, Frodhes 0.9.2, FM-106 Beta4 |
| sloop | SLOOP 2.7.1, sloopDX 4.0, ORBIT 0.7.1, No Man Sky FM 0.4 |
| melodee | Melodee 1.4.3 |

The public build (`-DFM1_PUBLIC_BUILD=ON`) carries the 10 whose licensing is settled: **Felucca** (the default
firmware), **GHOULBOX**, **JIANT FM**, **PurpleMonkey**, **CoralFlower**, **FoMni-1**, **FiMba-1**, **AMB-1**,
**OberVave** and **Frodhes**. The other 11 are gated (third-party sound data, artwork or naming terms not yet cleared:
CORES-PLAN.md 6) and build only for personal use.

### Remote FM-1

A real M-VAVE FM-1 running the [RemoteFM1](https://quixotic7.github.io/RemoteFM1/) firmware can be the panel of the
core inside the plugin: its keys, buttons, knobs and MASTER play the core, the core's screen and LEDs show on the
unit. Vendor bulk over libusb (linked statically), SysEx as the fallback; discovery talks only to what can be an FM-1;
one instance on the Mac drives the unit at a time; the FM-1's own MIDI is filtered out of the track.

### The editor: the FM-1 alone, settings in a sheet

The editor is the FM-1 panel itself (LEDs, LCD, mouse and computer keyboard, colour themes). Everything else moved
into a settings sheet behind a corner button: the firmware browser, presets, Remote FM-1, playing, host control,
themes, view, device and About.

### Core ABI 6

The core ABI (`core-api/fm1core.h`) owns its panel struct (`fm1panel_t`, ABI 5: no firmware header in the ABI; key
velocity and aftertouch in, the LED brightness out) and carries each firmware's credits and notices (ABI 6),
shown in the About page.

### Also since 0.2.0

- The eight physical knobs as host parameters that follow the firmware's screen (the Roto-Control map), with
  Stepped mode for controllers whose knobs click in steps; the firmware's own parameters by name as an opt-in
  (`-DFM1_TIER2_SLOTS=79`).
- The panel's buttons as host parameters with Tap, Hold and Toggle-hold modes.
- One conformance test every core passes, golden checks against each firmware's own host build, `core_bench`.

## 0.2.0

Development builds only (the JUCE plugin: AU, VST3, Standalone with the ChoralRoot core); not released.
