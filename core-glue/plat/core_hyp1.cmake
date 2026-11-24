# SPDX-License-Identifier: GPL-3.0-only
# HYP-1 1.5.54 (https://github.com/RustyPawz/fm1-HYP1, main @ 57c456db61826331d356458c9c1a136517da7cbf): owner's
# exception: no release, main @ 57c456d, 2026-11-01. The repository has no HYP-1 release: its tags v1.1 / v0.9 are
# Charles Vestal's AMB-1 commits inherited from the fork; the firmware hub's 1.5.54 (FM-1_7010554) is this commit's
# CI build (firmware.yml run 87: 1.5.<run - 33>), the version the glue passes as EN_VERSION (app.h: "DEV" otherwise).
# The plat glue (core-glue/plat/README.md) over the unmodified submodule (an AMB-1 fork on the plat layout); the
# instrument (dsp/amb.c) its own unit, as the firmware builds it. Generate: tools/build.py generate() writes
# felucca_font.h (gen_font.py's "chakra" set: Chakra Petch, OFL); dsp/pats.h is a committed header
set(_hyp1 ${CMAKE_SOURCE_DIR}/cores/fm1-HYP1)
fm1_add_core(hyp1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_hyp1} UNIT core_hyp1.c DESC core_hyp1_desc.c
             EXTRA_SOURCES hyp1_amb.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 1.5.54
             FLAGS -ffp-contract=off
             INCLUDES ${_hyp1}/firmware/src/dsp ${_hyp1}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: its GPL, its LICENSING.md and the
# fonts' OFL (Chakra Petch on the screen; Barlow and Terminus in assets/fonts/)
# rd1: the knob row as drawn, live labels and readouts (core_hyp1.c, common/rd_cols.h)
fm1_add_rfm1(hyp1 WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press ENV@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_Level_=_24 --expect-readout ctl_6_Detune_=_31
             AUTHORS "RedtailWorks"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Chakra Petch font in {name})=assets/fonts/ChakraPetch-OFL.txt"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
