# SPDX-License-Identifier: GPL-3.0-only
# Vector37 0.3.2 (https://github.com/genkigenki/Vector37, tag v0.3.2): the orbiting overtone synth's FM-1 build
# (build.py BOARD=fm1, experimental upstream: firmware/src/vector37.c with BOARD_SMK37 0, the panel panel_fm1.c,
# identity FM-1_700), on hand-written glue (core-glue/speak37: vector37_firmware.h, core_vector37.c) over the
# unmodified submodule cores/Vector37. Speak37's sibling (the same author and SLOOP / Felucca board layer): it runs on
# Speak37's host HAL, speak37_hal.h. The SMK-37 builds (BOARD=smk37, MODEL=elite / mke: another panel) are out of scope.
#   GEN: build.py generate(): felucca_font.h (Terminus, PIL), mk_tables.h
#   VERSION: vector37.c's MK_VERSION ("0.3.2"); the identity build.py gives BOARD=fm1 without --release (its --release
#            takes X.Y only: 0.3.2's package is the default build's, FM-1_700)
#   FLAGS: the .noinit section attribute (bootguard, mk_dbg), which Mach-O rejects
# The FM-1 build keeps no store of its own (scenes.c persist_boot): the core never writes its flash
# (conformance_vector37.txt: flash none).
# ref/ holds the Beetlecrab Vector's manual (CC BY-SA 4.0), for reference only: not compiled in, not carried.
fm1_add_core(vector37 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Vector37 UNIT core_vector37.c
             DESC core_vector37_desc.c FAMILY speak37 LICENSE GPL-3.0-only
             CREDITS "Vector37 by M. Geisler (genkigenki / makkuro), inspired by the Beetlecrab Vector and its Orbiter. Board layer from SLOOP by isod89, based on Felucca by Leo Kuroshita (Hügelton Instruments). Master stage after Melodee by Kerem Kilic (GPL-3.0). Plate reverb after J. Dattorro, Effect Design 1 (1997).\nThe screen's text is the Terminus font (Dimitar Zhekov, SIL Open Font License 1.1)."
             GEN felucca_font.h mk_tables.h
             VERSION_FILE firmware/src/vector37.c VERSION_REGEX "#define MK_VERSION \"([^\"]+)\""
             DEFINES FELUCCA_ID="FM-1_700"
             FLAGS "-Dsection(x)=aligned(4)")

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: its GPL and the Terminus font's OFL
fm1_add_rfm1(vector37 AUTHORS "genkigenki"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
