# SPDX-License-Identifier: GPL-3.0-only
# fm1-trax (https://github.com/jartificer/fm1-trax, tag v0.9.3; GPL-3.0-only): scaffolded by RFM1Manager from fimba's glue (core-glue/plat); review it
# FiMba-1 (https://github.com/jartificer/fm1-trax, tag v0.6.1): the plat glue (core-glue/plat/README.md) over the
# unmodified submodule; the instrument (dsp/kalimba.c) its own unit, as the firmware builds it
set(_fimba ${CMAKE_SOURCE_DIR}/cores/fm1-trax)
fm1_add_core(trax ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fimba} UNIT core_trax.c DESC core_trax_desc.c
             EXTRA_SOURCES trax_kalimba.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN drums.h felucca_font.h PYTHON_NEEDS raqm
             VERSION 0.9.3
             FLAGS -ffp-contract=off
             INCLUDES ${_fimba}/firmware/src/dsp ${_fimba}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(trax AUTHORS "Radu Mailat"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
