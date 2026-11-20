# SPDX-License-Identifier: GPL-3.0-only
# tkdemo: a tiny test firmware for the Move takeover (docs/RFM1-TAKEOVER.md, tk1), built as build/rfm1/tkdemo-1.0.rfm1
# in every -DFM1_RFM1=ON build (fm1_rfm1_finish includes this; -DFM1_RFM1_TKDEMO=OFF leaves it out). Not a core of the
# plugin: it is not in FM1_CORE_IDS, and its native module is built only on demand (tkdemo_core), out of build/cores.
# Its native object library (core_tkdemo_obj) is the same sources without FM1_HOST_TK: the FM-1 side alone.
fm1_add_core(tkdemo ${CMAKE_CURRENT_LIST_DIR}
    SOURCE ${CMAKE_CURRENT_LIST_DIR}
    UNIT tkdemo.c
    DESC tkdemo_desc.c
    EXTRA_SOURCES tkdemo_move.c
    FAMILY test
    NO_GEN
    VERSION 1.0)
set_target_properties(tkdemo_core PROPERTIES EXCLUDE_FROM_ALL ON
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/rfm1_build/tkdemo/native)
set_target_properties(core_tkdemo PROPERTIES EXCLUDE_FROM_ALL ON)
fm1_add_rfm1(tkdemo
    AUTHORS "FM1VST"
    MEMORY_MIB 1
    STACK_KIB 64
    WANTS tk1
    TAKEOVER_NAME TKDEMO
    TAKEOVER_CLAIMS screen pads steps knobs jog buttons
    RUN_ARGS --settle 500 --seconds 1)
