# SPDX-License-Identifier: GPL-3.0-only
# FoMni-1 (https://github.com/charlesvestal/fm1-fomni, tag v0.4): the plat glue (core-glue/plat/README.md) over the
# unmodified submodule; the instrument (dsp/omni.c) its own unit, as the firmware builds it
set(_fomni ${CMAKE_SOURCE_DIR}/cores/fm1-fomni)
fm1_add_core(fomni ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fomni} UNIT core_fomni.c DESC core_fomni_desc.c
             EXTRA_SOURCES fomni_omni.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h ann_words.h PYTHON_NEEDS raqm
             VERSION 0.4
             FLAGS -ffp-contract=off
             INCLUDES ${_fomni}/firmware/src/dsp ${_fomni}/firmware/src/app)
include(${CMAKE_CURRENT_LIST_DIR}/plat_golden.cmake)
plat_golden(fomni GEN_TARGET gen_fomni SOURCES ${_fomni}/host/omni_host.c ${_fomni}/firmware/src/dsp/omni.c
            DEFINES OM_HOST INCLUDES ${_fomni}/firmware/src ${_fomni}/firmware/src/dsp ${_fomni}/build/gen
            SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_fomni.txt)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(fomni AUTHORS "Charles Vestal")
