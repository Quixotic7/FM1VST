# SPDX-License-Identifier: GPL-3.0-only
# MNM-1 1.0.1 (https://github.com/galfaroth/mnm1, tag v1.0.1 = 2c7aee4): galfaroth's six-track synth on Hortator
# 0.23.3 (DEADACTIVE), on Felucca. As Hortator: no web/emu/felucca_web.c, its own browser simulator (tests/sim_core.c
# over tests/sim_host.h, unmodified), driven by core_mnm1.c (core_hortator.c's unit with MNM-1's names).
# Built as tools/build_sim.sh builds fm1sim.wasm: the generated headers on the path (build.py generate()'s
# felucca_font.h / felucca_tables.h / reson_tables.h / felucca_samples.h, and sim_keymap.h: hal/fm1_input.h's
# FM1_KEYMAP, which build_sim.sh cuts out with awk, here the same lines in the same GEN_PYTHON expression; chr(59) is
# the ';' of '};', a CMake list separator), firmware/src and tests on the include path, and FELUCCA_VERSION
# "MNM1-<VERSION.txt>" (tools/version.py firmware_string: what build.py and build_sim.sh give it).
if(EXISTS ${CMAKE_SOURCE_DIR}/cores/mnm1/VERSION.txt)
    file(STRINGS ${CMAKE_SOURCE_DIR}/cores/mnm1/VERSION.txt _mn_v LIMIT_COUNT 1)
    string(STRIP "${_mn_v}" _mn_v)
else()
    set(_mn_v "DEV")                               # (build_sim.sh's own fallback; fm1_add_core reports the empty tree)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(mnm1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/mnm1 UNIT core_mnm1.c
             DESC core_mnm1_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "MNM-1 by galfaroth, forked from Hortator by DEADACTIVE, built on Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. Calibrated machines, audio chain, effects, LFOs, arpeggiator and factory sounds ported from MONO2K by Inigo Hernandez, GPL-3.0-only. Ported in: Grids by Emilie Gillet (Mutable Instruments), GPL-3.0-or-later. Streams, Plaits and stmlib by Emilie Gillet (Mutable Instruments), MIT. DaisySP (Electrosmith, Emilie Gillet), MIT. The sample kit: Felucca's own drum sounds (tools/gen_waves.py, the Hügelton Sample Pack: Copyright (C) 2026 Hügelton Instruments, all rights reserved, combined under the section 7 permission of LICENSING.md) and Versilian Studios' VSCO-2 CE and VCSL instruments (CC0 1.0). The screen's pixel fonts are drawn by hand (GPL-3.0-only)."
             CREDITS_FILES LICENSES/MIT-Mutable-Instruments.txt LICENSES/MIT-DaisySP.txt
             GEN felucca_font.h felucca_tables.h reson_tables.h felucca_samples.h sim_keymap.h
             GEN_PYTHON "(__import__('build').generate(), __import__('pathlib').Path('build/gen/sim_keymap.h').write_text(__import__('re').search(r'(?m)^.*static const int8_t FM1_KEYMAP(?s:.*?)^}' + chr(59), open('firmware/hal/fm1_input.h').read()).group(0) + chr(10)))"
             VERSION_FILE VERSION.txt VERSION_REGEX "^([0-9]+\\.[0-9]+\\.[0-9]+)"
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/mnm1/tests
             DEFINES FELUCCA_VERSION="MNM1-${_mn_v}")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled into the core
# (LICENSING.md's table): the GPL (MNM-1, MONO2K's ports, Hortator, Felucca, the Grids port), LICENSING.md (with its
# section 7 permission for the Hügelton Sample Pack in felucca_samples.h), the MIT ports (Streams / Plaits / stmlib,
# DaisySP) and the CC0 samples' provenance. Not the JieLi SDK's Apache-2.0 (device packages only), jl-uboot-tool
# (the recovery tool), Terminus (the web page's typeface), the Unifraktur faces (a boot-screen study in tools/) or
# Fukiai (the web editor): none is in the firmware. RUN_ARGS: PLAY tapped before the chord (as Hortator's).
fm1_add_rfm1(mnm1 AUTHORS "galfaroth" "DEADACTIVE" "Leo Kuroshita (Hügelton Instruments)" "Inigo Hernandez"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "MIT (Mutable Instruments' Streams, Plaits and stmlib in {name})=LICENSES/MIT-Mutable-Instruments.txt"
                           "MIT (DaisySP in {name})=LICENSES/MIT-DaisySP.txt"
                           "CC0-1.0 (the samples in {name})=assets/samples-cc0/ATTRIBUTION.txt"
             RUN_ARGS --tap 10)   # PLAY
