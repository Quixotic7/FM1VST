# SPDX-License-Identifier: GPL-3.0-only
# ZEN-1 1.2 (https://github.com/aceyprime/zen-1, tag v1.2): the plat glue (core-glue/plat/README.md) over the
# unmodified submodule (on FuMi-1, on FoMni); the instrument (dsp/fumi.c) its own unit, as the firmware builds it.
# Generate: tools/build.py generate() writes felucca_font.h (Barlow, OFL); the samples (CC0, Freesound), the moon
# (NASA SVS, public domain) and the kanji (Noto Sans CJK, OFL) are committed headers
set(_zen1 ${CMAKE_SOURCE_DIR}/cores/zen-1)
fm1_add_core(zen1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_zen1} UNIT core_zen1.c DESC core_zen1_desc.c
             EXTRA_SOURCES zen1_fumi.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 1.2
             FLAGS -ffp-contract=off
             INCLUDES ${_zen1}/firmware/src/dsp ${_zen1}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: its GPL, its LICENSING.md, msfa's
# Apache-2.0, the fonts' OFL, and the recordings' and the moon's sources (CC0, public domain: assets/*/README.md)
fm1_add_rfm1(zen1 AUTHORS "aceyprime"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "CC0-1.0 (the recordings in {name}: their sources)=assets/sounds/README.md"
                           "Public domain (the moon image in {name}: its source)=assets/images/README.md")
