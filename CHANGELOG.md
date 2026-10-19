# Changelog

FM1VST's releases. The version is `project(FM1VST VERSION ..)` in CMakeLists.txt (the plugin shows it in its
settings sheet); a release is the tag `v<version>` on it (.github/workflows/release.yml builds and publishes it).

## Unreleased

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
