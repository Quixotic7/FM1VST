# SPDX-License-Identifier: GPL-3.0-only
# Fart Machine One (https://github.com/adamcharleswilsonmusic-code/Fart-Machine-One, tag V1.0.0): the plat glue
# (core-glue/plat/README.md) over the unmodified submodule; the sample player (dsp/omni.c) its own unit.
# GATED: the firmware embeds eleven recordings (samples/originals -> dsp/sample_bank.h) whose redistribution rights
# are unverified: samples/ATTRIBUTION.md, "user-reported provenance, not an independently verified licence for each
# recording"; "Pixabay's current general terms ... restrict standalone redistribution"; "The GPL code licence does
# not relicense the audio". Un-gated by a written licence for each recording (or the bank swapped for the firmware's
# own CC0 placeholder bank: tools/import_samples.py --placeholder, FMO_SAMPLE_BANK_HEADER="fmo_test_bank.h").
set(_fmo ${CMAKE_SOURCE_DIR}/cores/Fart-Machine-One)
fm1_add_core(fartmachine ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fmo} UNIT core_fartmachine.c
             DESC core_fartmachine_desc.c EXTRA_SOURCES fartmachine_omni.c
             FAMILY plat LICENSE GPL-3.0-only
             GATED "embeds 11 recordings said to be from Pixabay whose redistribution rights are unverified (samples/ATTRIBUTION.md)"
             GEN felucca_font.h PYTHON_NEEDS raqm
             VERSION 1.1.0
             FLAGS -ffp-contract=off
             INCLUDES ${_fmo}/firmware/src/dsp ${_fmo}/firmware/src/app)
include(${CMAKE_CURRENT_LIST_DIR}/plat_golden.cmake)
plat_golden(fartmachine GEN_TARGET gen_fartmachine SOURCES ${_fmo}/host/omni_host.c ${_fmo}/firmware/src/dsp/omni.c
            DEFINES OM_HOST INCLUDES ${_fmo}/firmware/src ${_fmo}/firmware/src/dsp ${_fmo}/build/gen
            SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_fartmachine.txt)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md), enabled by RFM1Manager. The licence texts: the
# firmware's own (its LICENSE, LICENSES/, LICENSING.md, the fonts')
fm1_add_rfm1(fartmachine AUTHORS "Trust This Mechanism"
             HOSTED_DESPITE_GATE "the bundled recordings (said to be from Pixabay, redistribution unverified): hosted at RemoteFM1FW only, as the author ships them, the owner's call of 1 November 2026"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "Apache-2.0 ({name})=LICENSES/AC79-SDK-LICENSE.txt"
                           "OFL-1.1 (the Barlow font in {name})=assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "CC0-1.0 (the AUDIO in {name})=samples/AUDIO-LICENSE.txt")
