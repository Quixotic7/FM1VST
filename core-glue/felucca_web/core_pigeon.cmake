# SPDX-License-Identifier: GPL-3.0-only
# PIGEON 0.4.1 (8f34e81, https://github.com/TheShrimpBlimp/pigeon): the felucca_web family's core (fw_core.h over the
# firmware's own web/emu/felucca_web.c, unmodified; cmake/FM1Cores.cmake fm1_add_core). The golden check:
# fw_common.cmake. A sampler on Felucca 1.5.1 (the PIGEON profile: FELUCCA_KO, KO_DUAL, SAMPLER_ONLY, KO_STORAGE,
# PIGEON_READER, all set by the web unit itself, as build-sampler.sh sets them for the device).
#
# The MP3 song decoder: firmware/src/ko_mp3.c and the nine libmad units it uses (firmware/vendor/libmad, GPL-2.0-or-
# later), compiled from the firmware's tree with the flags its own builds give them (tools/ko_codec_host.py on the
# host, web/emu/build.sh in the browser: fixed point FPM_64BIT, and malloc / calloc / free renamed to ko_mp3.c's
# ko_mad_* stubs: the decoder runs on preallocated state, no heap). The renaming is on those ten units only, never on
# the firmware unit or the glue. SIZEOF_LONG is the compiler's own (8 here, 4 under Emscripten, as build.sh says).
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(pigeon ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/pigeon UNIT core_pigeon.c
             DESC core_pigeon_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "PIGEON by The Shrimp Blimp, on Felucca 1.5.1 by Leo Kuroshita (Hügelton Instruments). The splash logo is by Dendy0097, as the firmware ships it. Its MP3 decoder is libmad 0.15.1b (Underbit Technologies, GPL-2.0-or-later). Ports: DaisySP and Mutable Instruments Rings (MIT), msfa (Apache-2.0). Fonts: Little Character by Disastrid, DotGothic16, Inter Tight (OFL-1.1), Fukiai (MIT). Instrument samples: VSCO-2 and VCSL (CC0). Drums: the Hügelton Sample Pack. Their notices:"
             CREDITS_FILES LICENSES/MIT-DaisySP.txt LICENSES/MIT-Rings.txt LICENSES/Apache-2.0-msfa.txt
                           firmware/vendor/libmad/COPYRIGHT
             GEN ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h
                 brand_splash.h ann_words.h
             VERSION_FILE firmware/src/project_identity.h VERSION_REGEX "^#define FW_DISPLAY_VERSION \"v([^\"]*)\""
             DEFINES FELUCCA_VERSION="v1.5.1")
if(TARGET core_pigeon_obj)
    set(_pg_fw ${CMAKE_SOURCE_DIR}/cores/pigeon/firmware)
    set(_pg_mp3 ${_pg_fw}/src/ko_mp3.c)
    foreach(_u bit fixed frame huffman layer12 layer3 stream synth timer)
        list(APPEND _pg_mp3 ${_pg_fw}/vendor/libmad/${_u}.c)
    endforeach()
    target_sources(core_pigeon_obj PRIVATE ${_pg_mp3})
    set_source_files_properties(${_pg_mp3} PROPERTIES
        COMPILE_OPTIONS "-w;-fwrapv"
        COMPILE_DEFINITIONS "FPM_64BIT;NDEBUG;HAVE_LIMITS_H;SIZEOF_INT=4;SIZEOF_LONG=__SIZEOF_LONG__;SIZEOF_LONG_LONG=8;malloc=ko_mad_malloc;calloc=ko_mad_calloc;free=ko_mad_free")
endif()
# the golden check from a NOR with the test bank (fw_golden.cpp FW_GOLDEN_SYSEX: a sampler is silent without one), its
# script 2 s later (FW_GOLDEN_DELAY_MS: after the 3 s splash), the reference linked with the same MP3 decoder units
fw_golden(pigeon ${CMAKE_SOURCE_DIR}/cores/pigeon web/emu/felucca_web.c FELUCCA_VERSION="v1.5.1")
if(TARGET fw_golden_pigeon)
    target_sources(fw_ref_pigeon PRIVATE ${_pg_mp3})
    set_tests_properties(fw_golden_pigeon PROPERTIES ENVIRONMENT
        "FM1EMU_HOME=${CMAKE_BINARY_DIR}/conformance/home;FW_GOLDEN_SYSEX=${CMAKE_SOURCE_DIR}/tests/fixtures/pigeon/bank.syx;FW_GOLDEN_DELAY_MS=2000")
endif()
