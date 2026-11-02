# SPDX-License-Identifier: GPL-3.0-only
# SPELLBOX 0.3 (https://github.com/pjac1810/spellbox, tag v0.3 = d1b90ea): a talking spelling toy after the 1978
# Speak & Spell, on foma (konsumer/foma, Felucca's platform). Not Felucca's web/emu/felucca_web.c (SPELLBOX has none,
# so not fw_core.h either): foma's own browser emulator, platform/web.c (fm1.h's API over a simulated FM-1),
# unmodified, driven by core_spellbox.c as web/worklet.js drives it (cmake/FM1Cores.cmake fm1_add_core). The family's
# descriptor (fw_desc.h), as Hortator's and MONO2K's.
# Built as tools/build.py builds the firmware: the app (src/main.c, speech.c, tts.c) its own units (EXTRA_SOURCES),
# platform, platform/hal, platform/src and platform/gen on the path (compile_flags.txt), APP_NAME / APP_VER and the
# package identity FELUCCA_ID from firmware.json (name SPELLBOX, version 0.3, "identity" FM-1_57 + the minor
# version's last digit: FM-1_573), its options all on (usb_audio, trs_midi, midi_clock: build.py's FOMA_* defines),
# FOMA_BLUETOOTH 0 and FOMA_MVAVE 0 (the "felucca" platform: firmware.json names none). The headers in platform/gen
# and src/speech.h are committed (NO_GEN). The version: firmware.json.
set(_sb ${CMAKE_SOURCE_DIR}/cores/spellbox)
fm1_add_core(spellbox ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_sb} UNIT core_spellbox.c
             DESC core_spellbox_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             EXTRA_SOURCES ../../cores/spellbox/src/main.c ../../cores/spellbox/src/speech.c
                           ../../cores/spellbox/src/tts.c
             CREDITS "SPELLBOX by pjac1810, GPL-3.0-only, on foma by konsumer (the platform, the emulator, the API), which carries Felucca by Leo Kuroshita (Hügelton Instruments) with the changes of SLOOP (isod89), Dinghy (zvenson) and X0X (Charles Vestal). The speech: made with espeak-ng (GPL-3.0, only its output, coded to LPC frames by tools/gen_speech.py). The pronouncing dictionary's word list: from wordfreq by Robyn Speer (CC BY-SA 4.0). The screen's type: Terminus (ter-u16n, SIL OFL 1.1). No data of the original toys' ROMs is used. Speak & Spell is a trademark of Texas Instruments, and SPELLBOX is not made or endorsed by Texas Instruments or M-VAVE."
             NO_GEN
             VERSION_FILE firmware.json VERSION_REGEX "\"version\": *\"([^\"]*)\""
             INCLUDES ${_sb}/src ${_sb}/platform ${_sb}/platform/hal ${_sb}/platform/src ${_sb}/platform/gen
             DEFINES APP_NAME="SPELLBOX" APP_VER="0.3" FELUCCA_ID="FM-1_573"
                     FOMA_USB_AUDIO=1 FOMA_TRS_MIDI=1 FOMA_MIDI_CLOCK=1 FOMA_BLUETOOTH=0 FOMA_MVAVE=0)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled in, the GPL
# (SPELLBOX, foma, Felucca's platform parts, espeak-ng's output), LICENSING.md (who wrote what, the wordfreq word
# list's CC BY-SA 4.0), the Terminus font's OFL (platform/gen/felucca_font.h, the screen's type). Not platform/mvave
# (M-VAVE's SDK and its binary engine: the "mvave" platform, not built here), nor the JieLi boot resources'
# Apache-2.0 (device packages only). No RUN_ARGS: setup() says "Hello" and the first word at power-on.
fm1_add_rfm1(spellbox AUTHORS "pjac1810"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=platform/gen/Terminus-LICENSE.txt")
