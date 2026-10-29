# SPDX-License-Identifier: GPL-3.0-only
# S1Vave 1.0 (https://github.com/AndrewGG88/S1Vave, commit 9730337 "S1Vave 1.0": no tag or release, main and master
# are that commit): Andrew GG's 8-voice virtual-analog fork of OberVave, on the Dinghy family glue (core-glue/dinghy/)
# over the unmodified submodule cores/s1vave, on its own host shim tests/host.c (OBV_HOST + OBV_WASM), as OberVave's.
# The headers in firmware/gen are committed (NO_GEN); the version is tools/build.py's APP_VER (its README's release
# build: --release 1.0). The .pool section attribute (gfx.c, obv_fx.h) Mach-O rejects becomes an alignment (FLAGS).
# Public (the owner's call, 2026-10-29). Its 64 factory programs are compiled-in C arrays (obv_patches.h): no preset
# file ships. CREDITS: its LICENSING.md's authors, the branding's CC BY 4.0, and the Terminus font's OFL (the text, from
# its tree: the release's licence finder only takes LICENSE* names).
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
fm1_add_core(s1vave ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/s1vave
             UNIT core_s1vave.c DESC core_s1vave_desc.c FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "S1Vave by Andrew GG (AndrewGG88): the sound engine, its DSP and its visual branding. A fork of OberVave by zvenson, on his Dinghy platform: the hardware layer is Felucca's by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89, and the SMK-37 board layer is by M. Geisler (genkigenki). GPL-3.0-only. The S1VAVE branding (its typography and the cyber orb, by Andrew GG) is under Creative Commons Attribution 4.0 (CC BY 4.0, https://creativecommons.org/licenses/by/4.0/). The full notices: the release's licences/cores/s1vave/ (LICENSING.md). The screen's font is Terminus Font (8x16), SIL Open Font License 1.1:"
             CREDITS_FILES assets/fonts/Terminus-LICENSE.txt
             NO_GEN VERSION 1.0
             INCLUDES ${CMAKE_SOURCE_DIR}/cores/s1vave/firmware/gen ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-Dsection(x)=aligned(4)")
dinghy_core(s1vave core_s1vave.c)
# the golden check: the same script on its own host sim (tests/host.c), the screens compared at 3000 ms
dinghy_golden_check(s1vave SCRIPT golden_s1vave.txt AT 3000 SOURCE ${CMAKE_SOURCE_DIR}/cores/s1vave/firmware/src/s1vave.c
                    DEFINES OBV_HOST APP_VER="1.0"
                    INCLUDES ${CMAKE_SOURCE_DIR}/cores/s1vave/firmware/src ${CMAKE_SOURCE_DIR}/cores/s1vave/firmware/gen
                    FLAGS "-Dsection(x)=aligned(4)")
