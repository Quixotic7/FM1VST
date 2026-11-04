# SPDX-License-Identifier: GPL-3.0-only
# Niagara Drift (https://github.com/evanyerburgh/niagara-drift, tag v0.2.1 = 86f47d2; GPL-3.0-only), a FoMni fork that
# replaced the instrument with a two-drone engine: the plat glue (core-glue/plat/README.md) over the unmodified
# submodule; the drone engine (dsp/drone.c, with drone_fx.c) its own unit, as the firmware builds it (tools/build.py
# O2_UNITS; host/build_host.sh: -DOM_HOST -ffp-contract=off). Scaffolded by RFM1Manager from fomni's glue, ported by hand.
set(_nd ${CMAKE_SOURCE_DIR}/cores/niagara-drift)
fm1_add_core(niagaradrift ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_nd} UNIT core_niagaradrift.c DESC core_niagaradrift_desc.c
             EXTRA_SOURCES niagaradrift_drone.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 0.2.1
             FLAGS -ffp-contract=off
             INCLUDES ${_nd}/firmware/src/dsp ${_nd}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: the firmware's own (its LICENSE,
# LICENSING.md), the fonts' (Barlow, the UI face; Inter Tight and IBM Plex Mono, its browser emulator's faces)
fm1_add_rfm1(niagaradrift AUTHORS "Evan Yerburgh"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the IBMPlexMono font in {name})=web/emu/fonts/IBMPlexMono-OFL.txt"
                           "OFL-1.1 (the InterTight font in {name})=web/emu/fonts/InterTight-OFL.txt"
             RUN_ARGS --tap 10)   # PLAY: the drones start stopped (ui.c: PLAY fades them in)
