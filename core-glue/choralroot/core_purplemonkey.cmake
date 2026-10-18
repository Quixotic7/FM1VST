# SPDX-License-Identifier: GPL-3.0-only
# PurpleMonkey FM-1 0.11 (https://github.com/Quixotic7/PurpleMonkeyFM1, tag v0.11): its emulator's unit,
# tools/emu/pm_emu_fw.c, through core_purplemonkey.c and the family's glue (crf_glue.h)
include(${CMAKE_CURRENT_LIST_DIR}/crf.cmake)
set(_pm ${CMAKE_SOURCE_DIR}/cores/PurpleMonkeyFM1)
# GEN: tools/build.py generate() (ChoralRoot's list plus PurpleMonkey's four: pm_drumkits.h, pm_fm6.h,
# pm_sprites.h --names-only, pm_rig.h); gen_aa_font.py lays text out with Raqm
fm1_add_core(purplemonkey ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_pm} UNIT core_purplemonkey.c
             DESC core_purplemonkey_desc.c FAMILY choralroot LICENSE GPL-3.0-only
             GEN ui_fonts.h cr_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h
                 melodee_cz1.h felucca_samples.h pm_drumkits.h pm_fm6.h pm_sprites.h pm_rig.h
             PYTHON_NEEDS raqm
             VERSION 0.11
             INCLUDES ${_pm}/tools/emu)
# the golden check: the family's PurpleMonkey script against its own emulator (tools/emu/build_pm.sh -> pm_emu)
crf_golden(purplemonkey SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden/purplemonkey.txt REF ${_pm}/build/host/pm_emu
           BUILD sh tools/emu/build_pm.sh BUILD_DEPENDS gen_purplemonkey AUDIO)
