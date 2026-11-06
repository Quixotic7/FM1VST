# SPDX-License-Identifier: GPL-3.0-only
# NACHTFAHRT 1.0.0 by Cranky Man (github.com/FartyPants/fm1-nachtfahrt, tag v1.0.0 = 3325768, the release
# "NACHTFAHRT 1.0.0"): a generative Berlin School instrument, a fork of DUB-1 (sbegueria), itself a fork of AMB-1
# (Charles Vestal), on the plat layout. The plat glue (core-glue/plat/README.md) over the unmodified submodule, from
# HYP-1's glue; the instrument (dsp/amb.c with seq.c, fm.c, the rain loop and the piano) its own unit, as the firmware
# builds it. Generate: tools/build.py generate() writes felucca_font.h (gen_font.py: Barlow, OFL) and ann_words.h
# (gen_words.py: the screen reader's words link)
set(_nachtfahrt ${CMAKE_SOURCE_DIR}/cores/fm1-nachtfahrt)
fm1_add_core(nachtfahrt ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_nachtfahrt} UNIT core_nachtfahrt.c
             DESC core_nachtfahrt_desc.c
             EXTRA_SOURCES nachtfahrt_amb.c
             FAMILY plat LICENSE GPL-3.0-only
             GEN felucca_font.h ann_words.h PYTHON_NEEDS raqm
             VERSION 1.0.0
             FLAGS -ffp-contract=off
             CREDITS "NACHTFAHRT by Cranky Man, a fork of DUB-1 by sbegueria and of AMB-1 by Charles Vestal, on Felucca's platform layer (Leo Kuroshita, Hugelton Instruments) through X0X and FoMni. Dattorro's plate reverb from FoMni. Shimmer, Choir and the Atmospheres' noise after Felucca and Skiff. Pluck after fm1_open's pluck.c (Electrosmith, Emilie Gillet, M-VAVE: DaisySP, MIT). The SEA codec after Daninet/sea-codec (Dani Biro, MIT) as in fm1_open. The FM voices after fm1_open's demo recipes (M-VAVE) with MSFA's envelope (Google, Pascal Gauthier: Apache-2.0). The Piano: three notes of the VCSL Kawai grand (Versilian Studios, CC0). The rain and water recordings: the author's own. Fonts: Barlow, Terminus (OFL-1.1)."
             INCLUDES ${_nachtfahrt}/firmware/src/dsp ${_nachtfahrt}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: its GPL, its LICENSING.md (with
# the MIT notice of Pluck and the origins of the SEA code, the piano and the recordings) and the fonts' OFL. It plays
# from power-on (ui_init: en_play(1)): no RUN_ARGS
fm1_add_rfm1(nachtfahrt AUTHORS "Cranky Man"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt")
