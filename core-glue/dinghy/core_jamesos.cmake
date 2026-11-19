# SPDX-License-Identifier: GPL-3.0-only
# JamesOS 1.0b by Aaron Grando (https://github.com/aarongrando/JamesOS, tag v1.0b = c630fe1, build 3083; the
# public repository has no release tag): the Dinghy family glue (core-glue/dinghy/) over the unmodified submodule
# cores/JamesOS, the FM-1 product only (firmware/product.h's default; the tree also builds JamesOS-K for the
# SMK-37 Pro), in its host mode (DINGHY_HOST) with the device's SAVE screen and project slots
# (DINGHY_DEVICE_SAVE_TEST, as its tests/test_project_device_ui.py builds the app). Two host files the firmware
# includes by relative paths are programs or test fixtures: platform.c's "../../tests/host.c" and dinghy.c's
# "../../tests/project_device_host.c"; clang's -ivfsoverlay maps them to jamesos_host.c and jamesos_project_host.c
# (the overlay file is written here, in the build tree), as core_cultbox.cmake does.
#
# The units as tools/build.py builds the device app: dinghy.c (the unit, with the platform, the app and the glue),
# firmware/src/audio_fixed8_plan_unit.c, Braids' four upstream units and braids_perc_c_api.cc, plaits_c_api.cc,
# plaits_kit_c_api.cc and every upstream Plaits and stmlib unit (C++14, no exceptions, no RTTI, no thread-safe
# statics, -DTEST and Plaits' stmlib renamed plaits_stmlib, as build.py and tests/build_braids_host.sh compile them).
# The defines: build.py's CFLAGS (the 44.1 kHz profile, 256-frame blocks, the Plaits rate and cache switches, the MIDI
# clock) for every unit, C and C++ alike, minus the device's (FM1_DEVICE, BR_CPU1_AUDIO: the host build renders on
# one core; BR_AUDIO_FOREGROUND_FIXED8_PLAN 0 as the host tests have it), plus the 1.0b release's --define set
# (README "Build and test": RV2_LINE_STRUCT, BR_MODEL_MOD_EXPERIMENT, BR_ADAPTIVE_POLY; the FM1_PATTERN_* ones are
# the device's pattern store) and its polyphony (BR_CAP_POLY=1: device candidates have it on). FELUCCA_ID is the
# release's identity (docs/RELEASE-1.0b.md: FM-1_9673083); APP_VER is what a device candidate is built with
# (build.py TARGETS fm1 "version", which --release cannot change for a candidate: the screen reader's HELLO says
# "JAMESOS 1.0b-device-test FM-1_9673083"). The generated headers are committed (firmware/gen, the fonts and the bank
# in firmware/src, the drum kits in assets/drums/generated: NO_GEN). The .pool / .ram_audio section attributes Mach-O
# rejects become an alignment (-Dsection).
#
# Licences (LICENSE, docs/legal/THIRD-PARTY-NOTICES.md): the code GPL-3.0-only (with Dinghy's, Felucca's and SLOOP's
# platform); Plaits, Braids and stmlib MIT (Emilie Gillet; the six-op engine's DX7 patch banks are Plaits' own
# resources.cc); the sixteen Plaits model symbols adapted from the Mutable Instruments documentation, CC BY-SA 3.0;
# the drum kits synthesised by tools/synth_drum_kits.py, CC0 1.0; the HD font's glyph bitmaps derived from Chakra
# Petch SemiBold, SIL OFL 1.1 (docs/legal/HD-FONT-NOTICE.md). The 128 starting sounds (assets/presets/bank.json) and
# the example songs are the project's own.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_jo ${CMAKE_SOURCE_DIR}/cores/JamesOS)
set(_jo_ver 1.0b)
set(_jo_id FM-1_9673083)
set(_jo_vfs ${CMAKE_BINARY_DIR}/dinghy/jamesos_vfs.yaml)
file(WRITE ${_jo_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_jo}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/jamesos_host.c\" }, { \"type\": \"file\", \"name\": \"${_jo}/tests/project_device_host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/jamesos_project_host.c\" } ] }\n")
# build.py's CFLAGS defines (device-only ones left out, see above) and the release's --define set
set(_jo_defs BR_AUDIO_INTERNAL_BLOCK160=0 BR_AUDIO_INTERNAL_BLOCK240=0 BR_AUDIO_INTERNAL_BLOCK64=0
    BR_AUDIO_INTERNAL_BLOCK128=0 BR_AUDIO_INTERNAL_BLOCK256=1 FM1_NATIVE_OUTPUT_32K=0 FM1_NATIVE_OUTPUT_48K=0
    FM1_NATIVE48_EXPERIMENTAL_CLOCK=0 PLAITS_INTERNAL_SAMPLE_RATE=44100 FM1_PLAITS_SOURCE_RATE_32K=0
    FM1_PLAITS_SOURCE_RATE_44100=1 FM1_PLAITS_SOURCE_RATE_38400=0 FM1_PLAITS_SOURCE_RATE_48000=0
    FM1_PLAITS_SRC_LAGRANGE4=0 FM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0 FM1_PLAITS_PHASE_LUT=0
    FM1_PLAITS_RATE_TIME_CALIBRATION=1 FM1_PLAITS_HIHAT_RATE_TIME_CALIBRATION=1 BR_AUDIO_FOREGROUND_FIXED8_PLAN=0
    BR_AUDIO_FIXED8_SOLVER_RAM=0 BR_AUDIO_FAILURE_JOB_PROFILE_DIAG=0 BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG=0
    BR_AUDIO_FAILURE_JOB_FX_SUBSTAGE_PROFILE_DIAG=0 BR_AUDIO_FAILURE_JOB_MODEL_SUBSTAGE_PROFILE_DIAG=0
    BR_FX_RATE_SIGNED_441_RECIP=0 BR_FX_TAPE_WOW_DELTA_LUT=0 BR_PLAITS_HIHAT_FUSION=0
    FM1_HARMONIC_TARGET_AMPLITUDE_CACHE=1 FM1_HARMONIC_CACHE_TEST=0 FM1_HARMONIC_TRIPLE_RAM=0
    FM1_PLAITS_KIT_RENDER_OUT_RESTRICT=1 FM1_PLAITS_WAVES_STACK_SCRATCH=0 FM1_PLAITS_WAVES_BOOT_SCRATCH_BSS=1
    FM1_BRAIDS_PACK_WT_WAVES=0 BR_MIDI_CLOCK_EXPERIMENT=1 BR_ENABLE_PLAITS=1 BR_UI_HD=1
    BR_CAP_POLY=1 RV2_LINE_STRUCT=1 BR_MODEL_MOD_EXPERIMENT=1 BR_ADAPTIVE_POLY=1)
# the other units: the planner (C) and the DSP (C++), build.py's lists in its order
set(_jo_braids braids/upstream/braids/analog_oscillator.cc braids/upstream/braids/digital_oscillator.cc
    braids/upstream/braids/macro_oscillator.cc braids/upstream/braids/resources.cc braids/braids_perc_c_api.cc)
file(GLOB_RECURSE _jo_pl_up RELATIVE ${_jo}/firmware ${_jo}/firmware/plaits/upstream/plaits/*.cc)
file(GLOB_RECURSE _jo_st_up RELATIVE ${_jo}/firmware ${_jo}/firmware/plaits/upstream/stmlib/*.cc)
list(SORT _jo_pl_up)
list(SORT _jo_st_up)
set(_jo_plaits plaits/plaits_c_api.cc plaits/plaits_kit_c_api.cc ${_jo_pl_up} ${_jo_st_up})
set(_jo_extra ../../cores/JamesOS/firmware/src/audio_fixed8_plan_unit.c)
foreach(_f IN LISTS _jo_braids _jo_plaits)
    list(APPEND _jo_extra ../../cores/JamesOS/firmware/${_f})
endforeach()
list(APPEND _jo_extra jamesos_libcxx.cc)          # (std::sort's float instantiation: the .rfm1 links no libc++)
fm1_add_core(jamesos ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_jo}
             UNIT core_jamesos.c DESC core_jamesos_desc.c EXTRA_SOURCES ${_jo_extra}
             FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "JamesOS by Aaron Grando (a.k.a. James Os, a Gjelly project): three synth tracks on 24 Plaits engines, four drum lanes, 128 starting sounds, the sequencer and the pattern arranger, the effects and the HD interface. Built on Dinghy by zvenson, over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89. GPL-3.0-only. Plaits, Braids and stmlib by Emilie Gillet (Mutable Instruments), MIT. JamesOS is not made or endorsed by Mutable Instruments. The Plaits model symbols are adapted from the Mutable Instruments documentation, CC BY-SA 3.0. The drum kits are synthesised by the project, CC0 1.0. The HD font's glyphs derive from Chakra Petch (The Chakra Petch Project Authors), SIL Open Font License 1.1. The full notices: the release's docs/legal/ (THIRD-PARTY-NOTICES.md, HD-FONT-NOTICE.md)."
             NO_GEN VERSION ${_jo_ver}
             DEFINES ${_jo_defs} APP_VER="1.0b-device-test" FELUCCA_ID="${_jo_id}"
             INCLUDES ${_jo}/firmware/hal ${_jo}/firmware/src ${_jo}/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)" "-fwrapv" "-ffp-contract=off" "-ivfsoverlay" "${_jo_vfs}")
if(TARGET core_jamesos_obj)
    # (the overlaid host files are not in the compiler's dependency list under their real names)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_jamesos.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/jamesos_host.c ${CMAKE_CURRENT_LIST_DIR}/jamesos_project_host.c ${_jo_vfs})
    # the planner: the app's own flags, its warnings off; the C++ units (native and, read by fm1_add_rfm1, the
    # .rfm1's: after its C flags, so -std=c++14 wins)
    set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/JamesOS/firmware/src/audio_fixed8_plan_unit.c
                                PROPERTIES COMPILE_OPTIONS "-w;-fwrapv;-ffp-contract=off;-Dsection(x)=aligned(4)")
    set(_jo_cxx "-std=c++14;-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-fwrapv;-ffp-contract=off;-w;-Dsection(x)=aligned(4)")
    foreach(_f IN LISTS _jo_braids)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/JamesOS/firmware/${_f} PROPERTIES
            COMPILE_OPTIONS "${_jo_cxx};-I${_jo}/firmware/braids;-I${_jo}/firmware/braids/upstream")
    endforeach()
    foreach(_f IN LISTS _jo_plaits)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/JamesOS/firmware/${_f} PROPERTIES
            COMPILE_OPTIONS "${_jo_cxx};-DTEST;-Dstmlib=plaits_stmlib;-I${_jo}/firmware/plaits;-I${_jo}/firmware/plaits/upstream;-I${_jo}/firmware/braids/upstream")
    endforeach()
    set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/jamesos_libcxx.cc PROPERTIES COMPILE_OPTIONS "${_jo_cxx}")
endif()
dinghy_core(jamesos core_jamesos.c)
# (no golden check: JamesOS's host programs are its test runners, each with its own script and DSP objects)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL, the MIT texts of Plaits, Braids and stmlib, the notices
# (the CC BY-SA 3.0 symbols and the CC0 drum kits are described there), the HD font's notice and its OFL; Emscripten's
# and musl's are added by fm1_add_rfm1
fm1_add_rfm1(jamesos AUTHORS "Aaron Grando"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "Third-party notices ({name}, docs/legal/THIRD-PARTY-NOTICES.md)=docs/legal/THIRD-PARTY-NOTICES.md"
                           "MIT (Plaits in {name})=firmware/plaits/upstream/MIT-LICENSE.txt"
                           "MIT (stmlib in {name})=firmware/plaits/upstream/stmlib/LICENSE"
                           "MIT (Braids in {name})=firmware/braids/upstream/MIT-LICENSE.txt"
                           "HD font notice ({name}, docs/legal/HD-FONT-NOTICE.md)=docs/legal/HD-FONT-NOTICE.md"
                           "OFL-1.1 (Chakra Petch, the HD font in {name})=tools/fonts/chakrapetch/OFL.txt")
