# SPDX-License-Identifier: GPL-3.0-only
# DUELL 0.2.20-beta (https://github.com/oscillr/DUELL): the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake.
# UN-GATED (the owner's call, 2026-10-22; CORES-PLAN.md 6): NOTICE.md, "The names DUELL and OSCILL/R and the logo
# artwork are the property of OSCILL/R; builds that are not the official release must not present themselves as
# official DUELL releases". The core is the unmodified v0.2.20-beta tree, named "DUELL (unofficial build)" (the
# descriptor's name: the plugin's firmware browser, its sheet and About page show it) with its commit in source_url,
# which satisfies those naming terms; the splash (the logo) is GPL-3.0-only as shipped. Keep the label and the link.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(duell ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/DUELL UNIT core_duell.c
             DESC core_duell_desc.c FAMILY felucca_web LICENSE GPL-3.0-only GEN_ROOT felucca-base
             CREDITS "An unofficial build: FM1VST compiles the unmodified DUELL v0.2.20-beta source as a plugin core. It is not an official DUELL release. DUELL's own notice:"
             CREDITS_FILES NOTICE.md
             GEN duell_version.h ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h splash_logo.h felucca_tables.h
                 felucca_fm6.h duell_sounds.h duell_multis.h
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/DUELL/felucca-base/firmware/src
             VERSION 0.2.20-beta)
fw_golden(duell ${CMAKE_SOURCE_DIR}/cores/DUELL/felucca-base web/emu/felucca_web.c)
