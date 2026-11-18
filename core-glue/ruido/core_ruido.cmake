# SPDX-License-Identifier: GPL-3.0-only
# Ruido 0.7.1 by Miguel Hernández Domenech (https://github.com/miguelhd/ruido, tag v0.7.1 = 963e912; GPL-3.0-only), a
# four-oscillator Schmitt-trigger drone synthesizer with a scope HUD. Its firmware is portable C++17 (src/); on the
# FM-1 it runs over zvenson/dinghy's platform (fetched by its container build at f77bf03, the hardware layer), in its
# browser simulator over platforms/sim/wasm_bridge.cpp. Its own glue folder (core-glue/ruido): ruido_glue.cpp is the
# simulator bridge's unity build of src/ with the FM-1 bridge's portable headers (platforms/fm1: the panel, the NVS,
# the quantizer) used unchanged; core_ruido.c is the ABI and the NOR (Dinghy's blob record restated there). Dinghy is
# not built, so nothing is fetched. C++ as Ruido's Makefile compiles it: C++17, no exceptions, no RTTI, no math errno,
# no fused multiply-add. Nothing is generated.
# GATED: the UI's font tables (src/ui/FontSansAA.h, used by every HUD screen) are glyph bitmaps rasterised from
# Apple's Helvetica Neue (tools/bake_font.mm, lines 164-168: CoreText HelveticaNeue-Medium / -Bold), no licence.
set(_ru ${CMAKE_SOURCE_DIR}/cores/ruido)
set(_ru_ver 0.7.1)
if(EXISTS ${_ru}/src/core/Version.h)
    file(STRINGS ${_ru}/src/core/Version.h _ru_vl REGEX "define RUIDO_VERSION_STR")
    if(NOT _ru_vl MATCHES "\"${_ru_ver}\"")
        message(FATAL_ERROR "ruido: src/core/Version.h says ${_ru_vl}, the glue ${_ru_ver} (a new pin: check the glue)")
    endif()
endif()
if(NOT EXISTS ${_ru}/platforms/sim/wasm_bridge.cpp OR NOT EXISTS ${_ru}/platforms/fm1/fm1_storage_io.h)
    message(FATAL_ERROR "ruido: no platforms/sim/wasm_bridge.cpp or platforms/fm1/fm1_storage_io.h in ${_ru} (a new pin: check the glue)")
endif()
fm1_add_core(ruido ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_ru} UNIT core_ruido.c DESC core_ruido_desc.c
             EXTRA_SOURCES ruido_glue.cpp
             FAMILY ruido LICENSE GPL-3.0-only
             GATED "its UI font (src/ui/FontSansAA.h) is rasterised from Apple's Helvetica Neue (tools/bake_font.mm), no licence"
             CREDITS "Ruido by Miguel Hernández Domenech, GPL-3.0-only. Its FM-1 platform builds on Dinghy (zvenson), Felucca (Leo Kuroshita, Hügelton Instruments) and SLOOP (isod89), GPL-3.0-only (THIRD_PARTY_NOTICES.md). The HUD's font tables are rasterised from Helvetica Neue (not licensed)."
             NO_GEN
             VERSION ${_ru_ver}
             INCLUDES ${_ru}/src ${_ru}/platforms/fm1)
if(TARGET core_ruido_obj)
    # Ruido's Makefile flags (DSP_CXXFLAGS without its warnings: the native and, read by fm1_add_rfm1, the .rfm1's)
    set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/ruido_glue.cpp PROPERTIES
        COMPILE_OPTIONS "-std=c++17;-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-fno-math-errno;-ffp-contract=off;-w;-I${_ru}/src;-I${_ru}/platforms/fm1")
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: Ruido's LICENSE (GPL-3.0) and
# THIRD_PARTY_NOTICES.md
fm1_add_rfm1(ruido AUTHORS "Miguel Hernández Domenech"
             HOSTED_DESPITE_GATE "Helvetica Neue glyph bitmaps compiled in: built as the author ships it, hosted on RemoteFM1FW only, the owner's call of 2026-11-18"
             LICENSE_FILES "GPL-3.0-only ({name}, LICENSE)=LICENSE"
                           "{name} third-party notices (THIRD_PARTY_NOTICES.md)=THIRD_PARTY_NOTICES.md")
