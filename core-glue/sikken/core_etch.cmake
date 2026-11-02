# SPDX-License-Identifier: GPL-3.0-only
# ETCH 1.88 (https://github.com/SikkenWorks/ETCH, tag V1.88 = 2eaaad8): Joppe Sikken's (SikkenWorks) drawn-wavetable
# synth, a native FM-1 application on Felucca's HAL (firmware/fm1-native), the app RUPTURE is built from. The
# SikkenWorks family's glue (core-glue/sikken: sikken_hal.h, hal/fm1_flash.h, ota_trusted_loader.h, sikken_glue.h,
# shared with RUPTURE; core_etch.c) over the unmodified submodule cores/ETCH.
# THE VERSION: the tags are V<major>.<minor> (capital V); RELEASE-MANIFEST.json's "release_version" is the same
# number ("1.88"), read from there. The package identity (FELUCCA_ID, bringup.c) is FM-1_1500 for every release
# (RELEASE-MANIFEST.json firmware_identity; the Project-Preset-Manager accepts FM-1_1500 identities).
# THE BUILD: build.py's units and settings, as tools/build_v188_release.py's app is built
# (build/fm1-native-storage-updater-uac48-p05: --storage --updater --usb-audio-48k --multi-project), but
# ETCH_NATIVE_UAC 0 (USB audio out: a core's audio is its output). Nothing is generated (the tables are committed).
# -Dsection(x)=aligned(4): the .pool / .noinit / .uac_ram section attributes, which Mach-O rejects. bringup.c is
# the submodule's as it is (no pi32v2 instruction in it, unlike RUPTURE's).
set(_et ${CMAKE_SOURCE_DIR}/cores/ETCH)
set(_et_fw ${_et}/firmware/fm1-native)
set(_et_g ${CMAKE_CURRENT_LIST_DIR})

# ---- the overlays (sikken.cmake's sikken_vfs: the device's HAL as sikken_hal.h, hal/fm1_flash.h,
# ota_trusted_loader.h all zero, so the JieLi loader stays out of the core)
include(${CMAKE_CURRENT_LIST_DIR}/sikken.cmake)
sikken_vfs(_et_vfs etch ${_et_fw})

# ---- the core: bringup.c (with the glue) the unit, build.py's other units its extra sources
set(_et_units audio_core native_performance native_seq_ui native_pattern_bank native_project_codec native_wave_fx
              native_fx native_factory wave_editor wave_store native_digest native_panel_midi
              native_performance_midi)
file(RELATIVE_PATH _et_rel ${_et_g} ${_et_fw})
set(_et_extra "")
foreach(_u IN LISTS _et_units)
    list(APPEND _et_extra ${_et_rel}/${_u}.c)
endforeach()
fm1_add_core(etch ${_et_g} SOURCE ${_et} UNIT core_etch.c DESC core_etch_desc.c
             EXTRA_SOURCES ${_et_extra}
             FAMILY sikken LICENSE GPL-3.0-only NO_GEN
             CREDITS "ETCH by SikkenWorks (Joppe Sikken): the concept, the waveform drawing, A/B shape morphing and Wave C instrument design, its synthesis, editing and performance work, the interface (github.com/SikkenWorks/ETCH). Built on Felucca by Leo Kuroshita (@kurogedelic, Hügelton Instruments): boot and startup, the hardware layer and drivers, interrupts, LCD, parts of audio and the USB MIDI and update transport, which ETCH modifies and adapts (GPL-3.0-only). Felucca's licensing inventory credits further upstream authors (in licenses). The FM1 is M-VAVE's: ETCH is unofficial third-party firmware, not affiliated with or endorsed by M-VAVE or JieLi."
             CREDITS_FILES LICENSES/FELUCCA-LICENSING.txt
             VERSION_FILE RELEASE-MANIFEST.json VERSION_REGEX "\"release_version\": *\"([0-9]+\\.[0-9]+)\""
             FLAGS -ffp-contract=off "-Dsection(x)=aligned(4)" "-ivfsoverlay${_et_vfs}"
             DEFINES ETCH_FLASH_READ_ONLY=0 ETCH_NATIVE_STORAGE=1 ETCH_NATIVE_OTA=1 ETCH_TEST_RESTORE_ONLY=0
                     ETCH_NATIVE_UAC=0 ETCH_NATIVE_UAC_48K=0 ETCH_P04_EXPANDED=0 ETCH_P05_MULTI=1
                     ETCH_STORE_SPARE_ENABLED=0 ETCH_STORE_OWNED_ENABLED=1
             INCLUDES ${_et_fw} ${_et_g})
sikken_units(etch ${_et_fw} ${_et_vfs} ${_et_units})

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled in, all
# GPL-3.0-only (ETCH's own code and art, Felucca's HAL and drivers, the firmware's pixel font): the GPL and Felucca's
# licensing notes. Not the JieLi loader's Apache-2.0 (left out: ota_trusted_loader.h). firmware/fm1-native/LICENSES'
# other notices (DaisySP, Rings, msfa, Fukiai, Inter Tight, DotGothic16) cover Felucca code and fonts that ETCH's
# units do not compile in (README: provenance credits). RUN_ARGS: SEL confirms the workflow ETCH asks for at every
# cold boot (SIMPLE), silent until then.
fm1_add_rfm1(etch AUTHORS "SikkenWorks (Joppe Sikken)" "Leo Kuroshita (Hügelton Instruments)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "Felucca licensing (in {name})=LICENSES/FELUCCA-LICENSING.txt"
             RUN_ARGS --press SEL@2000)
