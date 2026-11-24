# SPDX-License-Identifier: GPL-3.0-only
# Felucca 1.8.2 (https://github.com/hugelton/Felucca): the felucca_web family's core (fw_core.h over the firmware's own web/emu/felucca_web.c, unmodified;
# since 1.8.2, which dropped web/emu, the glue's copy of 1.8.1's: felucca_web_181.c;
# cmake/FM1Cores.cmake fm1_add_core). The golden check: fw_common.cmake. Since 1.8 the hardware layer is the felucca-bsp
# submodule (firmware/bsp, MPL-2.0): its chip/ and boards/fm1/ are on the include path as web/emu/build.sh puts them,
# and gen_adpcm.py's felucca_adpcm.h is generated.
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(felucca ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/Felucca UNIT core_felucca.c
             DESC core_felucca_desc.c FAMILY felucca_web LICENSE GPL-3.0-only SUBMODULES_RECURSIVE
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
                 felucca_adpcm.h
             VERSION_FILE firmware/src/felucca.c VERSION_REGEX "^#define FELUCCA_VERSION \"v?([^\"]*)\""
             DEFINES FELUCCA_VERSION="v1.8.2"
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/Felucca/firmware/hal ${CMAKE_SOURCE_DIR}/cores/Felucca/firmware/bsp/chip
                      ${CMAKE_SOURCE_DIR}/cores/Felucca/firmware/bsp/boards/fm1)
fw_golden(felucca ${CMAKE_SOURCE_DIR}/cores/Felucca ../../core-glue/felucca_web/felucca_web_181.c FELUCCA_VERSION="v1.8.2")
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
# rd1 (docs/RFM1.md "Readouts (rd1)"): the Tier 2 map (core_felucca_params.c) through the shim's bridge: a knob turned
# reads out its target's name and value text, and the knobs' live labels follow knob_target
# (the test: HOME on ANALOG, KNOB 1 three detents up: E5 CUT 412 Hz; SELECT one up: Tempo 121 BPM)
fm1_add_rfm1(felucca AUTHORS "Leo Kuroshita (Hügelton Instruments)" WANTS rd1
             READOUT_TEST --turn K1@4100:3 --turn SELECT@4300:1 --seconds 1
                          --expect-readout ctl_3_E5_CUT_=_412_Hz --expect-readout ctl_0_Tempo_=_121_BPM)
