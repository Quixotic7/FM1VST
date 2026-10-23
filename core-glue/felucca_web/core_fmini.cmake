# SPDX-License-Identifier: GPL-3.0-only
# FMini-1 1.8.0 (https://github.com/ruppert/fmini-1, tag v1.8.0): the felucca_web family's core (fw_core.h over the
# firmware's own web/emu/felucca_web.c, unmodified; cmake/FM1Cores.cmake fm1_add_core), with its Felucca-Engines
# submodule (firmware/engines: SUBMODULES_RECURSIVE). The golden check: fw_common.cmake. Public (the owner's call,
# 2026-10-22): a few of its factory patch names name artists or marks (EMERSON, NUMAN, MOOG BASS ..): names only, the
# patches are the author's own (tools/fmini_bank.py).
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(fmini ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/fmini-1 UNIT core_fmini.c
             DESC core_fmini_desc.c FAMILY felucca_web LICENSE GPL-3.0-only SUBMODULES_RECURSIVE
             CREDITS "FMini-1 by ruppert, on Felucca 1.6.1 by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. The FMINI ladder's calibration is read from the circuit notes of CA-72 by idlefoundry, the code is the firmware's own. Felucca-Engines (Hügelton Instruments), MIT. DaisySP and Rings (Electrosmith, Emilie Gillet), MIT. msfa (Google Inc., Pascal Gauthier), Apache-2.0. Inter Tight (SIL OFL 1.1), the Fukiai icon font (MIT). samples VSCO-2 CE and VCSL (CC0 1.0). Some factory patch names refer to artists or brands as a tribute: names only, no data of theirs. The full notices: the release's licences/cores/fmini/ (LICENSING.md, CREDITS.md, LICENSES/)."
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
             VERSION 1.8.0 DEFINES FELUCCA_VERSION="v1.8.0")
fw_golden(fmini ${CMAKE_SOURCE_DIR}/cores/fmini-1 web/emu/felucca_web.c FELUCCA_VERSION="v1.8.0")
