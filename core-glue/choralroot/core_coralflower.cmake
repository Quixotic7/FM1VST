# SPDX-License-Identifier: GPL-3.0-only
# CoralFlower 0.5 (https://github.com/FicusFicusov/CoralFlower, tag v0.5): its emulator's unit, tools/emu/emu_fw.c,
# through core_coralflower.c and the family's glue (crf_glue.h)
include(${CMAKE_CURRENT_LIST_DIR}/crf.cmake)
set(_cf ${CMAKE_SOURCE_DIR}/cores/CoralFlower)
# GEN: tools/build.py generate()'s whole list (cf_fonts.h: gen_pixfont.py, Departure Mono; gen_aa_font.py lays text
# out with Raqm). 0.4 dropped melodee_cz1.h from tools/emu/build.sh's list (nothing includes it: FELUCCA_CZ 0), so the
# empty stand-in 0.2 needed is gone. VERSION: the tag's 0.5 (coralflower.c's FELUCCA_VERSION still says 0.1;
# tools/emu/build.sh takes web/releases.json's newest, which at the tag is still 0.4.2)
fm1_add_core(coralflower ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cf} UNIT core_coralflower.c DESC core_coralflower_desc.c
             FAMILY choralroot LICENSE GPL-3.0-only
             GEN ui_fonts.h cf_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h
                 felucca_samples.h
             PYTHON_NEEDS raqm
             VERSION 0.5
             INCLUDES ${_cf}/tools/emu)
# the golden check: the family's CoralFlower script against its own emulator (tools/emu/build.sh -> emu), built with
# CR_VERSION=0.5 so that its splash reads as the core's ("CoralFlower 0.5")
crf_golden(coralflower SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden/coralflower.txt REF ${_cf}/build/host/emu
           BUILD env CR_VERSION=0.5 sh tools/emu/build.sh BUILD_DEPENDS gen_coralflower AUDIO)
