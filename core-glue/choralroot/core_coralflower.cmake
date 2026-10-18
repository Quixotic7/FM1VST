# SPDX-License-Identifier: GPL-3.0-only
# CoralFlower 0.2 (https://github.com/FicusFicusov/CoralFlower, tag v0.2): its emulator's unit, tools/emu/emu_fw.c,
# through core_coralflower.c and the family's glue (crf_glue.h)
include(${CMAKE_CURRENT_LIST_DIR}/crf.cmake)
set(_cf ${CMAKE_SOURCE_DIR}/cores/CoralFlower)
# GEN: tools/build.py generate() (cf_fonts.h: gen_pixfont.py, Departure Mono; gen_aa_font.py lays text out with
# Raqm); and an empty melodee_cz1.h: nothing includes it (FELUCCA_CZ 0; the tree has no CZ-1 tones to make it from),
# but tools/emu/build.sh (the golden check's reference) runs the whole device build (tools/build.py: the JieLi SDK)
# when it is missing. VERSION: the hub's 0.2
# (coralflower.c's FELUCCA_VERSION still says 0.1)
fm1_add_core(coralflower ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cf} UNIT core_coralflower.c DESC core_coralflower_desc.c
             FAMILY choralroot LICENSE GPL-3.0-only
             GEN ui_fonts.h cf_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h
                 felucca_samples.h melodee_cz1.h
             EXTRA_GEN "-c \"open('build/gen/melodee_cz1.h', 'w').write('/* CoralFlower: no CZ-1 (FELUCCA_CZ 0) */')\""
             PYTHON_NEEDS raqm
             VERSION 0.2
             INCLUDES ${_cf}/tools/emu)
# the golden check: the family's CoralFlower script against its own emulator (tools/emu/build.sh -> emu)
crf_golden(coralflower SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden/coralflower.txt REF ${_cf}/build/host/emu
           BUILD sh tools/emu/build.sh BUILD_DEPENDS gen_coralflower AUDIO)
