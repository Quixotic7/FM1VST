# SPDX-License-Identifier: GPL-3.0-only
# DUELL 1.0 (https://github.com/oscillr/DUELL, tag v1.0, 2026-10-22: its first release that is not a pre-release):
# the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake.
# UN-GATED (the owner's call, 2026-10-22; CORES-PLAN.md 6): NOTICE.md, "The names DUELL and OSCILL/R and the logo
# artwork are the property of OSCILL/R; builds that are not the official release must not present themselves as
# official DUELL releases". The core is the unmodified v1.0 tree, named "DUELL (unofficial build)" (the
# descriptor's name: the plugin's firmware browser, its sheet and About page show it) with its commit in source_url,
# which satisfies those naming terms; the splash (the logo) is GPL-3.0-only as shipped. Keep the label and the link.
# 1.0 (re-read 2026-10-22): NOTICE.md's naming paragraph is word for word 0.2.20-beta's; it adds two credit lines
# (Velo's key-sense method, the third-party parts). The new CREDITS.md ends "The DUELL logo and artwork are the work
# of OSCILL/R and are not part of the GPL licence", against NOTICE.md's "the splash graphic as shipped in the
# firmware are GPL-3.0-only" (CORES-PLAN.md 6). The credits carry both files. 1.0's generate list adds
# midi_map_defaults.h (gen_midi_map.py).
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(duell ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/DUELL UNIT core_duell.c
             DESC core_duell_desc.c FAMILY felucca_web LICENSE GPL-3.0-only GEN_ROOT felucca-base
             CREDITS "An unofficial build: FM1VST compiles the unmodified DUELL v1.0 source as a plugin core. It is not an official DUELL release. DUELL's own notice and credits:"
             CREDITS_FILES NOTICE.md CREDITS.md
             GEN duell_version.h ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h splash_logo.h felucca_tables.h
                 felucca_fm6.h duell_sounds.h duell_multis.h midi_map_defaults.h
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/DUELL/felucca-base/firmware/src
             VERSION 1.0)
fw_golden(duell ${CMAKE_SOURCE_DIR}/cores/DUELL/felucca-base web/emu/felucca_web.c)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(duell AUTHORS "oscillr")
