# SPDX-License-Identifier: GPL-3.0-only
# FM1-TRAX (https://github.com/jartificer/fm1-trax, tag v0.9.5 = bb26ad0; GPL-3.0-only), a Mega Drive music tracker
# (YM2612 + SN76489) on FM1-GM's platform (Felucca's, by way of X0X and FoMni): the plat glue
# (core-glue/plat/README.md) over the unmodified submodule. The device's unity build (main.c: the UI, the settings and
# song slots, the store, the editor's link) is core_trax.c; the chips and the tracker its own units, as tools/build.py
# builds them (O2_UNITS: trax_*.c, -O2, -ffp-contract=off). Generated, by tools/build.py generate(): the screen font
# (tools/gen_font.py, Terminus, Pillow with libraqm) and the DAC kit (tools/gen_drums.py); the demo song
# (trk/demo_song_data.h) and the FM presets are committed. Scaffolded by RFM1Manager from fimba's glue, ported by hand.
set(_trax ${CMAKE_SOURCE_DIR}/cores/fm1-trax)
fm1_add_core(trax ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_trax} UNIT core_trax.c DESC core_trax_desc.c
             EXTRA_SOURCES trax_opn2.c trax_psg.c trax_mdsnd.c trax_song.c trax_player.c trax_presets.c
                           trax_presets_fm.c
             FAMILY plat LICENSE GPL-3.0-only
             CREDITS "FM1-TRAX by jartificer, on FM1-GM's platform (Felucca by Leo Kuroshita of Hügelton Instruments, X0X and FoMni by Charles Vestal).\nThe demo song “Sunset Beach Galaxy” by Tobikomi (the YM2017 music cart), GPL-3.0.\nThe Terminus font, SIL OFL 1.1."
             GEN drums.h felucca_font.h PYTHON_NEEDS raqm
             VERSION 0.9.5
             FLAGS -ffp-contract=off
             INCLUDES ${_trax}/firmware/src/dsp ${_trax}/firmware/src/trk ${_trax}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). RUN_ARGS: PLAY pressed twice before the chord: the
# first ends the boot intro (any press ends it: the chord alone would only do that, silently), the second starts the
# demo song, which the chord then plays along with
fm1_add_rfm1(trax AUTHORS "jartificer"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
             RUN_ARGS --press PLAY@2000 --tap 10)
