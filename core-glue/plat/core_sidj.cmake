# SPDX-License-Identifier: GPL-3.0-only
# SiDJ (https://github.com/thomasj/fm1-sidj, tag v1.3.0 = a91d24a; GPL-3.0-only, its SID engine GPL-2.0-or-later), a
# C64 music deck (a cycle-exact 6510 and reSID-derived SID chips playing .sid files, remixed live) on X0X's platform
# (Felucca's): the plat glue (core-glue/plat/README.md) over the unmodified submodule. The device's unity build
# (firmware/src/sidj.c: the UI, the settings, the song store and its SysEx transfer, the kept MIDI clocks) is
# core_sidj.c; the SID, the C64 and the engine its own units, as tools/build.py builds them (O2_UNITS: sidj_*.c,
# -O2). Generated, by tools/build.py generate(): the screen font (tools/gen_font.py: Barlow Semi Condensed and
# Terminus, Pillow with libraqm). No tunes are compiled in: the songs are the player's .sid files, added over SysEx
# from the songs page (web/site, #songs) as on the device.
set(_sidj ${CMAKE_SOURCE_DIR}/cores/fm1-sidj)
fm1_add_core(sidj ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_sidj} UNIT core_sidj.c DESC core_sidj_desc.c
             EXTRA_SOURCES sidj_sid.c sidj_sid_tables.c sidj_engine.c sidj_remix.c sidj_asid.c sidj_cpu6502.c
                           sidj_c64.c sidj_sidfile.c
             FAMILY plat LICENSE GPL-3.0-only
             CREDITS "SiDJ by Thomas Jansson (tubesockor), on Felucca's platform (Leo Kuroshita, Hügelton Instruments) by way of X0X (Charles Vestal).\nThe SID engine: a C port of reSID 0.16 by Dag Lem, GPL-2.0-or-later.\nThe fonts: Barlow Semi Condensed by Jeremy Tribby and Terminus by Dimitar Zhekov, SIL OFL 1.1."
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 1.3.0
             FLAGS -ffp-contract=off
             INCLUDES ${_sidj} ${_sidj}/firmware/src/app ${_sidj}/firmware/src/c64 ${_sidj}/sid)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The flash starts with no songs (a silent player), so
# the run test sends one over the songs page's SysEx first and plays it: sidj_test_tune.syx (make_test_tune.py: a
# tiny PSID written for this test, no one's music), PUT_BEGIN / PUT_DATA / PUT_END, then PLAY; the chord then plays
# voice 1 over it
fm1_add_rfm1(sidj AUTHORS "Thomas Jansson (tubesockor)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "GPL-2.0-or-later (the SID engine in {name}, after reSID 0.16)=third_party/licenses/GPL-2.0.txt"
                           "OFL-1.1 (the Barlow Semi Condensed font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
             RUN_ARGS --sysex ${CMAKE_CURRENT_LIST_DIR}/sidj_test_tune.syx)
