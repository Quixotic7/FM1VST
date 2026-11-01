# SPDX-License-Identifier: GPL-3.0-only
# niagara-drift (https://github.com/evanyerburgh/niagara-drift, tag v0.2; GPL-3.0-only): scaffolded by RFM1Manager from fomni's glue (core-glue/plat); review it
# FoMni-1 (https://github.com/evanyerburgh/niagara-drift, tag v0.4): the plat glue (core-glue/plat/README.md) over the
# unmodified submodule; the instrument (dsp/omni.c) its own unit, as the firmware builds it
set(_fomni ${CMAKE_SOURCE_DIR}/cores/niagara-drift)
fm1_add_core(niagaradrift ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fomni} UNIT core_niagaradrift.c DESC core_niagaradrift_desc.c
             EXTRA_SOURCES niagaradrift_omni.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 0.2
             FLAGS -ffp-contract=off
             INCLUDES ${_fomni}/firmware/src/dsp ${_fomni}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(niagaradrift AUTHORS "Evan Yerburgh"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Barlow font in {name})=web/emu/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the IBMPlexMono font in {name})=web/emu/fonts/IBMPlexMono-OFL.txt"
                           "OFL-1.1 (the InterTight font in {name})=web/emu/fonts/InterTight-OFL.txt")
