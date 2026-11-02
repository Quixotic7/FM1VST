# SPDX-License-Identifier: GPL-3.0-only
# PERF-1 0.16.0 by deamonSLINKY (https://github.com/deamonSLINKY/PERF-1, tag v0.16.0 = 22c295d6a098fc837f285c641aba4a4acb8c0077,
# the release "PERF-1 0.16.0"): a fork of HYP-1 (RedtailWorks), itself a fork of AMB-1 (Charles Vestal), on the plat
# layout. The plat glue (core-glue/plat/README.md) over the unmodified submodule, from HYP-1's glue; the instrument
# (dsp/amb.c with the FM6 engine) its own unit, as the firmware builds it. Generate: tools/build.py generate() writes
# felucca_font.h (gen_font.py: Chakra Petch, OFL); dsp/pats.h, sint_tab.h and the FM6 voices are committed headers
set(_perf1 ${CMAKE_SOURCE_DIR}/cores/PERF-1)
fm1_add_core(perf1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_perf1} UNIT core_perf1.c DESC core_perf1_desc.c
             EXTRA_SOURCES perf1_amb.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 0.16.0
             FLAGS -ffp-contract=off
             CREDITS "PERF-1 by deamonSLINKY, a fork of HYP-1 by RedtailWorks and of AMB-1 by Charles Vestal, on Felucca's platform layer (Leo Kuroshita, Hugelton Instruments) through X0X and FoMni. The FM6 engine, its DX7 voices, the USB MIDI host and the TRS MIDI fix from Melodee by Kerem Kilic (Ellic Studio), after Dexed (Pascal Gauthier) on MSFA (Raph Levien, Google, Apache-2.0). The DY voices: public-domain DX7 banks (Bobby Blues' and Dave Benson's DX7 pages). Dattorro's plate reverb from FoMni. Ideas from Tuna by delirioux (and Marbles by Emilie Gillet). Fonts: Chakra Petch (Cadson Demak), Barlow, Terminus (OFL-1.1)."
             INCLUDES ${_perf1}/firmware/src/dsp ${_perf1}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: its GPL, its LICENSING.md and the
# fonts' OFL (Chakra Petch on the screen; Barlow and Terminus in assets/fonts/)
fm1_add_rfm1(perf1 AUTHORS "deamonSLINKY"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Chakra Petch font in {name})=assets/fonts/ChakraPetch-OFL.txt"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
