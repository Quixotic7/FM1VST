# SPDX-License-Identifier: GPL-3.0-only
# Mini-Tone 1.0.1 (https://github.com/eldontyrellcorp/Mini-Tone, tag minitone-1.0.1): the felucca_web family's core
# (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified; cmake/FM1Cores.cmake fm1_add_core). The golden
# check: fw_common.cmake. Public (the owner's call, 2026-10-22).
# THE LICENCE TEXTS: Mini-Tone's LICENSING.md names LICENSES/ files its tree does not carry (it has only
# GPL-2.0-reSID.txt): the MIT texts of Braids, DaisySP, Rings and the Fukiai font, msfa's and the JieLi SDK's Apache-2.0,
# the Inter Tight and DotGothic16 OFL. FM1VST supplies them in licences/minitone/ (Felucca 1.6.1's texts for the parts
# Mini-Tone took from Felucca; Braids' MIT text written out): they are the descriptor's credits here, and
# tools/package_release.sh copies them into the release's licences/cores/minitone/LICENSES/.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
set(_mt_lic ${CMAKE_CURRENT_LIST_DIR}/licences/minitone)
fm1_add_core(minitone ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Mini-Tone UNIT core_minitone.c
             DESC core_minitone_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "Mini-Tone by Stephane Schmitz, on Felucca 1.5 by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. Ported in: Braids by Emilie Gillet (Mutable Instruments), MIT. reSID 0.16 by Dag Lem, GPL-2.0-or-later (LICENSES/GPL-2.0-reSID.txt). DaisySP (Electrosmith, Emilie Gillet) and Rings (Emilie Gillet), MIT. msfa (Google Inc., Pascal Gauthier), Apache-2.0. The UI's text is Inter Tight (SIL OFL 1.1), its icons the Fukiai font (Hügelton Instruments, MIT). the samples are Versilian Studios' VSCO-2 CE and VCSL (CC0 1.0). TB-303, Analog Four, Digitone, Braids and Mutable Instruments are names of their owners, used as references only. The MIT and Apache-2.0 texts below are supplied by FM1VST: the Mini-Tone repository names them but does not carry them."
             CREDITS_FILES ${_mt_lic}/MIT-Braids.txt ${_mt_lic}/MIT-DaisySP.txt ${_mt_lic}/MIT-Rings.txt
                           ${_mt_lic}/MIT-Fukiai.txt ${_mt_lic}/Apache-2.0-msfa.txt
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_transients.h
             VERSION 1.0.1 DEFINES FELUCCA_VERSION="v1.0.1")
fw_golden(minitone ${CMAKE_SOURCE_DIR}/cores/Mini-Tone web/emu/felucca_web.c FELUCCA_VERSION="v1.0.1")
