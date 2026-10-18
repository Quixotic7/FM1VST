# SPDX-License-Identifier: GPL-3.0-only
# ChoralRoot 0.25.1 (https://github.com/Quixotic7/ChoralRootFM1, tag v0.25.1): its own emulator's unit,
# tools/emu/emu_fw.c, through core_choralroot.c and the family's glue (crf_glue.h; cmake/FM1Cores.cmake: fm1_add_core)
include(${CMAKE_CURRENT_LIST_DIR}/crf.cmake)
set(_cr ${CMAKE_SOURCE_DIR}/cores/ChoralRootFM1)
# the Tier 2 parameter map (core_choralroot_params.c): the sound editor's pages get host slots of their own too (30
# more; off, they are FM1P_HIDDEN: the knobs reach them in the editor). The plugin then needs -DFM1_TIER2_SLOTS=103 or
# more
option(FM1_EXPOSE_EDITOR "ChoralRoot's Tier 2 map: also slots for the sound editor's ENV / LFO / MOD / MIX pages" OFF)
set(_cr_defs "")
if(FM1_EXPOSE_EDITOR)
    set(_cr_defs FM1_EXPOSE_EDITOR=1)
endif()
# the version: firmware/src/choralroot.c's FELUCCA_VERSION ("ChoralRoot 0.25.1", the device build's default and
# tools/emu/build.sh's); the unit shows the same string (core_choralroot.c)
# GATED: LICENSING.md lists the Casio CZ-1 preset tones ("the rights stay with Casio", "Casio's (no licence granted)")
# and Sequential's Prophet-5 factory programs ("the rights stay with Sequential", "Sequential's (no licence granted)");
# the emulator unit compiles both in (FELUCCA_CZ 1; FELUCCA_PROPHET 1, FELUCCA_P5_FACTORY 1). Un-gated by a build
# without them (FELUCCA_P5_FACTORY 0 and a CZ-1 engine without its factory tones, or FELUCCA_CZ 0: a different
# engine list) or by the rights holders' permission
# GEN: tools/build.py generate()'s whole list (0.25: + cr_layouts.h, melodee_prophet_factory.h)
fm1_add_core(choralroot ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cr} UNIT core_choralroot.c DESC core_choralroot_desc.c
             PREFIX CR FAMILY choralroot LICENSE GPL-3.0-only
             GATED "it bundles Casio CZ-1 and Sequential Prophet-5 factory patches (LICENSING.md: no licence granted)"
             GEN ui_fonts.h cr_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h cr_layouts.h
                 felucca_fm6.h melodee_cz1.h melodee_prophet_factory.h felucca_samples.h
             VERSION_FILE firmware/src/choralroot.c
             VERSION_REGEX "^#define FELUCCA_VERSION \"(ChoralRoot )?([^\"]*)\"" VERSION_GROUP 2
             INCLUDES ${_cr}/tools/emu DEFINES ${_cr_defs})
# the golden check: the submodule's own D major script (screens and samples) against its reference emulator (the
# top-level reference_emu: tools/emu/build.sh at the pinned tree, the replay tests' ground truth)
crf_golden(choralroot SCRIPT ${_cr}/tools/emu/scripts/cr_dmaj.txt REF ${_cr}/build/host/emu REF_TARGET reference_emu
           AUDIO)
