# SPDX-License-Identifier: GPL-3.0-only
# Viscera 4-Track 2.6.0 by De-Pattern (https://github.com/De-Pattern/viscera, tag v2.6.0 = bb13e0c; GPL-3.0-only with
# the FM1 SDK linking exception), an overdubbing four-track tape looper with a resonator to print through. On the FM-1
# it is an APP on M-VAVE's FM1 Open SDK (tools/make_fm1_tree.py overlays it on m-vave/fm1_open V14, which is not in
# the repository); its own part is a portable core (core/: C99, float, no C library) that its browser emulator runs
# with a small platform layer of its own (web/vsc_wasm.c). The glue (core_viscera.c) is that platform layer on the
# ABI, after the emulator's and the FM-1's (fm1/app/screen.c, fm1/app/viscera_fm1.c, fm1/app/audio_output.c), over
# the unmodified submodule: the four core files are their own units, as the emulator's build compiles them.
# Nothing is generated. Not gated: every compiled file is Viscera's own (GPL-3.0-only with the linking exception);
# the stock V15 firmware file in site/ (the install page's bridge) is not built in.
set(_vs ${CMAKE_SOURCE_DIR}/cores/viscera)
set(_vs_ver 2.6.0)
set(_vs_glue ${CMAKE_CURRENT_LIST_DIR})
file(RELATIVE_PATH _vs_rel ${_vs_glue} ${_vs})
if(EXISTS ${_vs}/VERSION)
    file(STRINGS ${_vs}/VERSION _vs_file_ver LIMIT_COUNT 1)
    if(NOT _vs_file_ver STREQUAL _vs_ver)
        message(FATAL_ERROR "viscera: VERSION says ${_vs_file_ver}, the glue ${_vs_ver} (a new pin: check the glue)")
    endif()
endif()
if(NOT EXISTS ${_vs}/core/vsc.h OR NOT EXISTS ${_vs}/web/vsc_wasm.c)
    message(FATAL_ERROR "viscera: no core/vsc.h or web/vsc_wasm.c in ${_vs} (a new pin: check the glue)")
endif()
set(_vs_units ${_vs_rel}/core/vsc_engine.c ${_vs_rel}/core/vsc_params.c ${_vs_rel}/core/vsc_ui.c ${_vs_rel}/core/vsc_midi.c)
fm1_add_core(viscera ${_vs_glue} SOURCE ${_vs} UNIT core_viscera.c DESC core_viscera_desc.c
             EXTRA_SOURCES ${_vs_units}
             FAMILY fm1open LICENSE GPL-3.0-only
             CREDITS "Viscera 4-Track by De-Pattern, GPL-3.0-only with the FM1 SDK linking exception (M-VAVE's FM1 Open APP licence, LICENSE.md)."
             NO_GEN
             VERSION ${_vs_ver}
             DEFINES VSC_VERSION="${_vs_ver}"
             INCLUDES ${_vs}/core)
if(TARGET core_viscera_obj)
    # the firmware's own units: warnings off (the emulator's build: clang -O2)
    list(TRANSFORM _vs_units PREPEND ${_vs_glue}/ OUTPUT_VARIABLE _vs_paths)
    set_source_files_properties(${_vs_paths} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). WANTS ain1: the host's audio input is what the tape
# records (the FM-1's Bluetooth and USB audio in; without it the tape has nothing to record: after the intro's music it
# is silent). The licence text: Viscera's LICENSE.md (GPL-3.0 with the SDK linking exception). RUN_ARGS: the run feeds
# Elasticat's test loop as the input; track 1 is armed at power-on and INPUT's MONITOR is AUTO (while a track is armed),
# so the chord's time hears the input through the desk (the chord's keys play RESON, which is OFF)
fm1_add_rfm1(viscera WANTS ain1 AUTHORS "De-Pattern"
             LICENSE_FILES "GPL-3.0-only with the FM1 SDK linking exception ({name})=LICENSE.md"
             RUN_ARGS --audio-in ${CMAKE_SOURCE_DIR}/core-glue/elasticat/test_folder/audio/testloop_bpm120.wav)
