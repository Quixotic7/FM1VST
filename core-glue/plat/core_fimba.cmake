# SPDX-License-Identifier: GPL-3.0-only
# FiMba-1 (https://github.com/jadamsowers/fm1-fimba, tag v0.7.0): the plat glue (core-glue/plat/README.md) over the
# unmodified submodule; the instrument (dsp/kalimba.c) its own unit, as the firmware builds it
set(_fimba ${CMAKE_SOURCE_DIR}/cores/fm1-fimba)
fm1_add_core(fimba ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fimba} UNIT core_fimba.c DESC core_fimba_desc.c
             EXTRA_SOURCES fimba_kalimba.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 0.7.0
             FLAGS -ffp-contract=off
             INCLUDES ${_fimba}/firmware/src/dsp ${_fimba}/firmware/src/app)
include(${CMAKE_CURRENT_LIST_DIR}/plat_golden.cmake)
plat_golden(fimba GEN_TARGET gen_fimba SOURCES ${_fimba}/host/kalimba_host.c ${_fimba}/firmware/src/dsp/kalimba.c
            DEFINES OM_HOST INCLUDES ${_fimba}/firmware/src ${_fimba}/firmware/src/dsp ${_fimba}/build/gen
            SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_fimba.txt)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
# rd1: its knob row as drawn, live labels and readouts (core_fimba.c, common/rd_cols.h)
fm1_add_rfm1(fimba WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_Material_=_Brass --expect-readout ctl_6_Tone_=_56
             AUTHORS "jadamsowers")
