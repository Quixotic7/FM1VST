# SPDX-License-Identifier: GPL-3.0-only
# Ravel_FM1 (https://github.com/De-Pattern/Ravel_FM1, tag v1.5.0; GPL-3.0-only): scaffolded by RFM1Manager from fimba's glue (core-glue/plat); review it
# FiMba-1 (https://github.com/De-Pattern/Ravel_FM1, tag v0.6.1): the plat glue (core-glue/plat/README.md) over the
# unmodified submodule; the instrument (dsp/kalimba.c) its own unit, as the firmware builds it
set(_fimba ${CMAKE_SOURCE_DIR}/cores/Ravel_FM1)
fm1_add_core(ravel_fm1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fimba} UNIT core_ravel_fm1.c DESC core_ravel_fm1_desc.c
             EXTRA_SOURCES ravel_fm1_kalimba.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 1.5.0
             FLAGS -ffp-contract=off
             INCLUDES ${_fimba}/firmware/src/dsp ${_fimba}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(ravel_fm1 AUTHORS "De-Pattern"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "THIRD_PARTY.md ({name})=THIRD_PARTY.md")
