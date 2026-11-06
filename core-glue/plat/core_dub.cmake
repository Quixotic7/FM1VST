# SPDX-License-Identifier: GPL-3.0-only
# DUB-1 1.4.3 by Santiago Beguería (github.com/sbegueria/fm1-dub, tag v1.4.3 = e89138a): a generative dub techno
# firmware, a fork of AMB-1 (Charles Vestal) from before AMB moved onto fm1-base, so on the plat layout as HYP-1 and
# FoMni. The plat glue (core-glue/plat/README.md) over the unmodified submodule, from HYP-1's glue with FoMni's screen
# reader; the instrument (dsp/amb.c) its own unit, as the firmware builds it (tools/build.py O2_UNITS). Generate:
# tools/build.py generate() writes felucca_font.h (gen_font.py's "barlow" set: Barlow Semi Condensed, OFL) and
# ann_words.h (gen_words.py: the screen reader's words link); app/recipe_prints.h is a committed header
set(_dub ${CMAKE_SOURCE_DIR}/cores/fm1-dub)
fm1_add_core(dub ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_dub} UNIT core_dub.c DESC core_dub_desc.c
             EXTRA_SOURCES dub_amb.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h ann_words.h PYTHON_NEEDS raqm
             VERSION 1.4.3
             FLAGS -ffp-contract=off
             CREDITS "DUB-1 by Santiago Beguería, a fork of AMB-1 by Charles Vestal, on Felucca's platform layer (Leo Kuroshita, Hugelton Instruments) through X0X and FoMni. Dattorro's plate reverb from FoMni, the granular cloud after JIANT's CLOUD (juanjiant-bit, GPL-3.0), the screen reader's protocol (announce.c, MIT) from fm1-reader. Fonts: Barlow, Terminus (OFL-1.1)."
             INCLUDES ${_dub}/firmware/src/dsp ${_dub}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: its GPL, its LICENSING.md and the
# fonts' OFL (Barlow Semi Condensed on the screen; Terminus in assets/fonts/)
fm1_add_rfm1(dub AUTHORS "Santiago Beguería"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
