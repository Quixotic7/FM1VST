# SPDX-License-Identifier: GPL-3.0-only
# FiMba-1 (https://github.com/jadamsowers/fm1-fimba, tag v0.6.1): the plat glue (core-glue/plat/README.md) over the
# unmodified submodule; the instrument (dsp/kalimba.c) its own unit, as the firmware builds it
set(_fimba ${CMAKE_SOURCE_DIR}/cores/fm1-fimba)
fm1_add_core(fimba ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fimba} UNIT core_fimba.c DESC core_fimba_desc.c
             EXTRA_SOURCES fimba_kalimba.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 0.6.1
             FLAGS -ffp-contract=off
             INCLUDES ${_fimba}/firmware/src/dsp ${_fimba}/firmware/src/app)
include(${CMAKE_CURRENT_LIST_DIR}/plat_golden.cmake)
plat_golden(fimba GEN_TARGET gen_fimba SOURCES ${_fimba}/host/kalimba_host.c ${_fimba}/firmware/src/dsp/kalimba.c
            DEFINES OM_HOST INCLUDES ${_fimba}/firmware/src ${_fimba}/firmware/src/dsp ${_fimba}/build/gen
            SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_fimba.txt)
