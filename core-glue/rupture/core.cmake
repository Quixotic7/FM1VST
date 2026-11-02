# SPDX-License-Identifier: GPL-3.0-only
# RUPTURE 1.48 (https://github.com/SikkenWorks/RUPTURE, tag V1.48 = bea4bfe): Joppe Sikken's (SikkenWorks) mono bass
# synth, a native FM-1 application on Felucca's HAL (vendor/etch-v1.88-source/firmware/fm1-native, by way of
# SikkenWorks' ETCH). Its own hand-written glue (core-glue/rupture: rupture_hal.h, hal/fm1_flash.h,
# ota_trusted_loader.h, core_rupture.c) over the unmodified submodule cores/RUPTURE. RUPTURE has no emulator: the
# glue is Tonitor's kind (core-glue/tonitor), with the firmware's own reset entry and main loop on a coroutine.
# THE VERSION: the tags are V<major>.<minor> (capital V); RELEASE-MANIFEST.json's "version" is the same number
# ("1.48"), read from there. The package identity (FELUCCA_ID, bringup.c) is FM-1_968 for every release
# (RELEASE-MANIFEST.json firmware_identity: kept for the OTA chain), so it says nothing about the version.
# THE BUILD: build.py's units and settings, as the release builds them (README: build.py --storage --updater
# --usb-audio-48k --multi-project), but ETCH_NATIVE_UAC 0 (USB audio out: a core's audio is its output). Nothing is
# generated (the tables are committed). -Dsection(x)=aligned(4): the .pool / .noinit / .uac_ram section attributes,
# which Mach-O rejects (the linker script's regions: one RAM here).
set(_ru ${CMAKE_SOURCE_DIR}/cores/RUPTURE)
set(_ru_fw ${_ru}/vendor/etch-v1.88-source/firmware/fm1-native)
set(_ru_bin ${CMAKE_BINARY_DIR}/core_vfs/rupture)
set(_ru_g ${CMAKE_CURRENT_LIST_DIR})

# ---- the overlays (a clang VFS overlay, as RemoteFM1's and LOOM-8's glues): the device's HAL headers read as
# rupture_hal.h (a one-line stub each, so its #pragma once holds) and hal/fm1_flash.h; ota_trusted_loader.h (the
# JieLi update loader's bytes, Apache-2.0) as an all-zero array of its size (no update runs in a core:
# core_rupture.c); bringup.c as the submodule's file with its one pi32v2 instruction (csync, a barrier, in
# ru_trs_service) a compiler barrier (checked: the configure fails if the line is gone)
if(EXISTS ${_ru_fw}/bringup.c)
    file(READ ${_ru_fw}/bringup.c _t)
    set(_from "__asm__ volatile(\"csync\" ::: \"memory\");")
    set(_to "__asm__ volatile(\"\" ::: \"memory\"); /* (FM1VST: csync, the pi32v2 barrier) */")
    string(FIND "${_t}" "${_from}" _at)
    string(REGEX MATCHALL "csync" _all "${_t}")
    list(LENGTH _all _n)
    if(_at LESS 0 OR NOT _n EQUAL 1)
        message(FATAL_ERROR "RUPTURE core: ${_ru_fw}/bringup.c has no \"${_from}\" any more, or another csync: "
                            "the overlay (core-glue/rupture/core.cmake) needs a look")
    endif()
    string(REPLACE "${_from}" "${_to}" _t "${_t}")
    set(_t "/* Changed by FM1VST (core-glue/rupture/core.cmake, an overlay of this GPL-3.0-only file for its host builds):\n * ru_trs_service's csync (a pi32v2 instruction) is a compiler barrier. */\n${_t}")
    set(_old "")
    if(EXISTS ${_ru_bin}/bringup.c)
        file(READ ${_ru_bin}/bringup.c _old)
    endif()
    if(NOT _old STREQUAL _t)
        file(WRITE ${_ru_bin}/bringup.c "${_t}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_ru_fw}/bringup.c)
endif()
set(_ru_vfs ${_ru_bin}/rupture_vfs.yaml)
set(_v "{ \"version\": 0, \"case-sensitive\": \"false\", \"use-external-names\": false, \"roots\": [\n")
string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_ru_fw}/bringup.c\", \"external-contents\": \"${_ru_bin}/bringup.c\" },\n")
string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_ru_fw}/ota_trusted_loader.h\", \"external-contents\": \"${_ru_g}/ota_trusted_loader.h\" },\n")
string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_ru_fw}/hal/fm1_flash.h\", \"external-contents\": \"${_ru_g}/hal/fm1_flash.h\" }")
foreach(_h time sys irq guard input uart timer audio adc lcd_hw usb xip)
    set(_s "/* (core-glue/rupture/core.cmake: the device's hal/fm1_${_h}.h, read as the host HAL) */\n#include \"rupture_hal.h\"\n")
    set(_old "")
    if(EXISTS ${_ru_bin}/hal/fm1_${_h}.h)
        file(READ ${_ru_bin}/hal/fm1_${_h}.h _old)
    endif()
    if(NOT _old STREQUAL _s)
        file(WRITE ${_ru_bin}/hal/fm1_${_h}.h "${_s}")
    endif()
    string(APPEND _v ",\n  { \"type\": \"file\", \"name\": \"${_ru_fw}/hal/fm1_${_h}.h\", \"external-contents\": \"${_ru_bin}/hal/fm1_${_h}.h\" }")
endforeach()
string(APPEND _v " ] }\n")
file(WRITE ${_ru_vfs}.in "${_v}")
configure_file(${_ru_vfs}.in ${_ru_vfs} COPYONLY)

# ---- the core: bringup.c (with the glue) the unit, build.py's other units its extra sources
set(_ru_units audio_core native_cxs_q15 native_performance native_seq_ui native_pattern_bank native_project_codec
              native_wave_fx native_fx rupture_eq2 native_factory wave_editor wave_store native_digest
              native_panel_midi native_performance_midi)
file(RELATIVE_PATH _ru_rel ${_ru_g} ${_ru_fw})
set(_ru_extra "")
foreach(_u IN LISTS _ru_units)
    list(APPEND _ru_extra ${_ru_rel}/${_u}.c)
endforeach()
fm1_add_core(rupture ${_ru_g} SOURCE ${_ru} UNIT core_rupture.c DESC core_rupture_desc.c
             EXTRA_SOURCES ${_ru_extra}
             FAMILY rupture LICENSE GPL-3.0-only NO_GEN
             CREDITS "RUPTURE by Joppe Sikken (SikkenWorks): the instrument, its sound design and Cycle Exchange Synthesis, the monochrome FM-1 interface (github.com/SikkenWorks/RUPTURE). Built on Felucca by Leo Kuroshita (@kurogedelic, Hügelton Instruments): its hardware layer, LCD, USB and TRS MIDI, interrupt, update and recovery code, which RUPTURE keeps and adapts (GPL-3.0-only), by way of SikkenWorks' ETCH. The FM1 is M-VAVE's: RUPTURE is unofficial third-party firmware, not an M-VAVE release."
             CREDITS_FILES licenses/FELUCCA-LICENSING.txt
             VERSION_FILE RELEASE-MANIFEST.json VERSION_REGEX "\"version\": *\"([0-9]+\\.[0-9]+)\""
             FLAGS -ffp-contract=off "-Dsection(x)=aligned(4)" "-ivfsoverlay${_ru_vfs}"
             DEFINES ETCH_FLASH_READ_ONLY=0 ETCH_NATIVE_STORAGE=1 ETCH_NATIVE_OTA=1 ETCH_TEST_RESTORE_ONLY=0
                     ETCH_NATIVE_UAC=0 ETCH_NATIVE_UAC_48K=0 ETCH_P04_EXPANDED=0 ETCH_P05_MULTI=1 RUPTURE_BUILD=1
                     ETCH_STORE_SPARE_ENABLED=0 ETCH_STORE_OWNED_ENABLED=1
             INCLUDES ${_ru_fw} ${_ru_g})
if(TARGET core_rupture_obj)
    set_property(SOURCE ${_ru_g}/core_rupture.c APPEND PROPERTY OBJECT_DEPENDS ${_ru_bin}/bringup.c ${_ru_vfs}
                 ${_ru_g}/rupture_hal.h ${_ru_g}/hal/fm1_flash.h ${_ru_g}/ota_trusted_loader.h)
    # build.py's other units: the firmware's own code (warnings off), the same section attributes
    foreach(_u IN LISTS _ru_units)
        set_source_files_properties(${_ru_g}/${_ru_rel}/${_u}.c PROPERTIES
            COMPILE_OPTIONS "-w;-ffp-contract=off;-Dsection(x)=aligned(4)")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled in, all
# GPL-3.0-only (RUPTURE's own code and art, Felucca's HAL and drivers, the 5x7 pixel font drawn for the firmware):
# the GPL and Felucca's licensing notes. Not the JieLi loader's Apache-2.0 (left out: ota_trusted_loader.h above).
fm1_add_rfm1(rupture AUTHORS "Joppe Sikken (SikkenWorks)" "Leo Kuroshita (Hügelton Instruments)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "Felucca licensing (in {name})=licenses/FELUCCA-LICENSING.txt")
