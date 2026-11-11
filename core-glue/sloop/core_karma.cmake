# SPDX-License-Identifier: GPL-3.0-only
# KARMA 3.1 (https://github.com/eatyourgreensstudio-dotcom/karma-fm1, tag v3.1 = 718de45), by Ariel Muchnik: a
# four-track groovebox after SLOOP 2.6, on Felucca. Its felucca.c includes what SLOOP's does (and ui_op.c): the
# SLOOP-family glue (core-glue/sloop) as FM1CORE_SL_KARMA, on one core (KARMA_DUAL_CORE 0, as its host builds).
# Plaits' and Elements' models are C++ (firmware/mi, MIT, and plx.cc / elx.cc, its way in): built as EXTRA_SOURCES
# with the flags of its own host build (tools/plx_host.sh: -std=c++11 -DTEST, no exceptions or RTTI).
# GATED as SLOOP: the firmware compiles assets/icons.png (tools/gen_icons.py -> felucca_icons.h), the Hugelton icon
# atlas, all rights reserved (LICENSING.md's GPL section 7 permission needs asset terms not yet published).
set(_karma_cc
    ../../cores/karma-fm1/firmware/mi/elements/dsp/multistage_envelope.cc
    ../../cores/karma-fm1/firmware/mi/elements/dsp/ominous_voice.cc
    ../../cores/karma-fm1/firmware/mi/elements/resources.cc
    ../../cores/karma-fm1/firmware/mi/elx.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/chords/chord_bank.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/bass_drum_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/fm_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/grain_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/hi_hat_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/modal_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/noise_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/particle_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/snare_drum_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/speech_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/string_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine/virtual_analog_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine2/chiptune_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine2/phase_distortion_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine2/six_op_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine2/string_machine_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine2/virtual_analog_vcf_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/engine2/wave_terrain_engine.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/fm/algorithms.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/fm/dx_units.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/physical_modelling/modal_voice.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/physical_modelling/resonator.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/physical_modelling/string.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/physical_modelling/string_voice.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/speech/lpc_speech_synth.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/speech/lpc_speech_synth_controller.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/speech/lpc_speech_synth_phonemes.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/speech/lpc_speech_synth_words.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/speech/naive_speech_synth.cc
    ../../cores/karma-fm1/firmware/mi/plaits/dsp/speech/sam_speech_synth.cc
    ../../cores/karma-fm1/firmware/mi/plaits/resources.cc
    ../../cores/karma-fm1/firmware/mi/plx.cc
    ../../cores/karma-fm1/firmware/mi/stmlib/dsp/atan.cc
    ../../cores/karma-fm1/firmware/mi/stmlib/dsp/units.cc
    ../../cores/karma-fm1/firmware/mi/stmlib/utils/random.cc)
fm1_add_core(karma ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/karma-fm1 UNIT core_karma.c
             DESC core_karma_desc.c FAMILY sloop LICENSE GPL-3.0-only
             EXTRA_SOURCES ${_karma_cc}
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/karma-fm1/firmware/mi
             CREDITS "KARMA by Ariel Muchnik (github.com/eatyourgreensstudio-dotcom/karma-fm1), after SLOOP, based on Felucca by Leo Kuroshita (Hugelton Instruments). Plaits, Elements, Clouds and Grids by Emilie Gillet. LXR-02 drum voices by Erica Synths (not affiliated)."
             GATED "the Hugelton icon atlas (assets/icons.png, all rights reserved, its GPL section 7 permission needs asset terms not yet published)"
             GEN felucca_font.h felucca_icons.h felucca_tables.h felucca_samples.h felucca_drumkits.h sloop_logo.h
             PYTHON_NEEDS numpy
             VERSION_FILE firmware/src/ui.c VERSION_REGEX "#define FELUCCA_VERSION \"KARMA ([0-9.]+)\"")
list(TRANSFORM _karma_cc PREPEND ${CMAKE_CURRENT_LIST_DIR}/)
set_source_files_properties(${_karma_cc} PROPERTIES
    COMPILE_OPTIONS "-w;-std=c++11;-fno-exceptions;-fno-rtti;-ffp-contract=off" COMPILE_DEFINITIONS "TEST")
# the .rfm1 (cmake/FM1Rfm1.cmake; docs/RFM1.md; gated, built as a hosted file: HOSTED_DESPITE_GATE): its GPL, msfa's
# Apache-2.0, Plaits' MIT, the LXR-02 notice, its LICENSING.md and the Terminus font's OFL (the screen font).
# The C++ units see firmware/cxx, the few standard headers the device build uses (no C++ library in the module:
# its <algorithm>'s sort, as on the FM-1)
fm1_add_rfm1(karma AUTHORS "Ariel Muchnik" MEMORY_MIB 16
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/karma-fm1/firmware/cxx
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "Apache-2.0 (msfa in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (Plaits, Elements, Clouds in {name})=LICENSES/MIT-plaits.txt"
                           "LXR-02 notice (Erica Synths, in {name})=LICENSES/LXR-NOTICE.md"
                           "LICENSING.md ({name})=LICENSING.md"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
             HOSTED_DESPITE_GATE "the owner's call, 2026-10-31: built as its author intended, hosted on RemoteFM1FW, not in the app")

# the golden check: KARMA's own tests/ui_pages_test.c (the real UI on its test doubles, with Plaits' models from its
# host library, as tests/run_tests.sh builds it) writes its pages; the core runs golden_karma.txt to the same state
# (SLOOP's script and comparison: the status corner differs, the HAL's USB host and battery). One more explained
# difference: the HOME page's section letters A..D (x 188..235, y 34..52): the test's doubles have sections A and B
# stored, a fresh unit none
# (not in an FM1_RFM1_ONLY build: no test library there)
if(TARGET karma_core AND NOT FM1_RFM1_ONLY AND NOT COMMAND fm1_sloop_golden)   # (FM1_ONLY_CORES=karma)
    include(${CMAKE_CURRENT_LIST_DIR}/core.cmake)
endif()
if(TARGET karma_core AND NOT FM1_RFM1_ONLY)
    add_library(karma_plx_host STATIC ${_karma_cc})      # (tools/plx_host.sh: build/host/libplx.a)
    target_include_directories(karma_plx_host PRIVATE ${CMAKE_SOURCE_DIR}/cores/karma-fm1/firmware/mi)
    fm1_sloop_ref(karma_ref_ui_pages ${CMAKE_SOURCE_DIR}/cores/karma-fm1/tests/ui_pages_test.c
                  ${CMAKE_SOURCE_DIR}/cores/karma-fm1 gen_karma)
    target_link_libraries(karma_ref_ui_pages PRIVATE karma_plx_host)
    set_target_properties(karma_ref_ui_pages PROPERTIES LINKER_LANGUAGE CXX)
    fm1_sloop_golden(karma REF karma_ref_ui_pages REF_ARGS %REF% SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_karma.txt
                     DRIVE_ARGS --compare 5 %REF%/page-splash.ppm --compare 2400 %REF%/page-tracks.ppm
                                --compare 3200 %REF%/page-edit.ppm --ignore 189 3 232 13 --ignore 188 34 235 52)
endif()
