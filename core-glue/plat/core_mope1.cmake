# SPDX-License-Identifier: GPL-3.0-only
# MOPE-1 (https://github.com/slothpallas/mope-1, tag v0.4.1 = a584c57; GPL-3.0-only), a wavetable synth with a
# backing band, a FiMba-1 fork on Felucca's platform (by way of X0X and FoMni): the plat glue
# (core-glue/plat/README.md) over the unmodified submodule. The device's unity build (main.c) is core_mope1.c; the
# instrument (dsp/synth.c) its own unit, as tools/build.py builds it (O2_UNITS). Generated, by tools/build.py
# generate(): the screen font (tools/gen_font.py, Barlow Semi Condensed, Pillow with libraqm).
set(_mope1 ${CMAKE_SOURCE_DIR}/cores/mope-1)
fm1_add_core(mope1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_mope1} UNIT core_mope1.c DESC core_mope1_desc.c
             EXTRA_SOURCES mope1_synth.c
             FAMILY plat LICENSE GPL-3.0-only
             CREDITS "MOPE-1 by slothpallas, from FiMba-1 (jadamsowers) and FoMni and X0X (Charles Vestal), on Felucca by Leo Kuroshita of Hügelton Instruments.\nThe Barlow Semi Condensed font (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 0.4.1
             FLAGS -ffp-contract=off
             INCLUDES ${_mope1}/firmware/src/dsp ${_mope1}/firmware/src/app)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md)
fm1_add_rfm1(mope1 AUTHORS "slothpallas"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt")
