# SPDX-License-Identifier: GPL-3.0-only
# Carnac OS (https://github.com/OwenKirby/carnac-os, tag v0.9.6 = 55cf5b5; GPL-3.0-only, LICENSING.md), OrbVorgis's
# polyphonic wavetable synth on X0X's platform (its pinned submodule x0x-firmware/, X0X 1.0.8+, Felucca's HAL): the
# plat glue (core-glue/plat/README.md) over the unmodified submodule. The device's unity build (build_fm1.py: X0X's
# x0x.c cut at its app, the synth's UI, screens and synth_panel.c) is core_carnacos.c; the DSP and sequencers one unit
# (carnacos_dsp.c, as build_fm1.py's synth_dsp.c, -O2, -ffp-contract=off as web/emu/build.sh); the PCM waves and the
# Foundations bank (synthesised by gen_pcm_waves.py / gen_foundations.py, committed) carnacos_images.c. Generated: X0X's
# screen font (x0x-firmware/tools/gen_font.py: Barlow Semi Condensed and Terminus, Pillow with libraqm).
set(_cn ${CMAKE_SOURCE_DIR}/cores/carnac-os)
fm1_add_core(carnacos ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_cn} UNIT core_carnacos.c DESC core_carnacos_desc.c
             EXTRA_SOURCES carnacos_dsp.c carnacos_images.c
             FAMILY plat LICENSE GPL-3.0-only SUBMODULES_RECURSIVE
             CREDITS "Carnac OS by OrbVorgis.\nOn the platform of X0X by Charles Vestal, a fork of Felucca by Leo Kuroshita (Hügelton Instruments).\nThe Barlow Semi Condensed and Terminus fonts, SIL OFL 1.1."
             GEN felucca_font.h GEN_ROOT x0x-firmware PYTHON_NEEDS raqm
             GEN_PYTHON "__import__('os').makedirs('build/gen', exist_ok=True) or __import__('gen_font').main('build/gen/felucca_font.h')"
             VERSION 0.9.6
             FLAGS -ffp-contract=off
             INCLUDES ${_cn}/src ${_cn}/src/dsp ${_cn}/src/hardware ${_cn}/x0x-firmware/firmware/src)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/carnacos_dsp.c ${CMAKE_CURRENT_LIST_DIR}/carnacos_images.c
                            PROPERTIES COMPILE_OPTIONS "-w;-ffp-contract=off")

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: its GPL, LICENSING.md (where each
# part comes from: X0X, Felucca) and the two fonts' OFL (Barlow, Terminus, from X0X's tree)
fm1_add_rfm1(carnacos AUTHORS "OrbVorgis"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Barlow font in {name})=x0x-firmware/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=x0x-firmware/assets/fonts/Terminus-LICENSE.txt"
             MEMORY_MIB 16)
