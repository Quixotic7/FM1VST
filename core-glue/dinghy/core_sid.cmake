# SPDX-License-Identifier: GPL-3.0-only
# FM-1 SID 0.3 (https://github.com/joakimeriksson/fm1-sid, tag v0.3 = c2e231f): the Dinghy family glue
# (core-glue/dinghy/) over the unmodified submodule cores/fm1-sid, its Dinghy build (firmware/build_dinghy.sh:
# firmware/fm1sid.c on its copy of Dinghy 0.3, dinghy/src/platform.c) in the host branch (DINGHY_HOST). platform.c
# includes "../../tests/host.c", a file FM-1 SID does not ship: clang's -ivfsoverlay maps that path to sid_host.c
# (the overlay file is written here, in the build tree), as core_fm106.cmake does.
#
# What its build gets from outside its tree (tools/fetch_ext.sh into ext/, not committed there):
#   reSID 0.16 (Dag Lem, GPL-2.0-or-later): fm1-dock's copy, the second submodule cores/fm1-dock (v0.7.1 = 3ec04e5;
#     the 0.3 release was built on fm1-dock e177e83, the same resid/ files), modules/sid/resid: the same tarball
#     (sha256 830fe4c0..) with fetch_ext.sh's one change (the wave tables const unsigned char), siddefs.h included
#   assets.h (firmware/gen_assets.py: the 8x12 font rendered from DejaVu Sans Mono with Pillow, and the own tune,
#     tools/housetune.py): committed here (sid_gen/assets.h), made by that script (DejaVu Sans Mono, Pillow 13); the
#     house tune is byte for byte web/house.sid. The font's licence: licences/sid/DejaVu-LICENSE.txt
#   the tune pack: GoatTracker 2's ten example songs in the tree (tunes/goattracker, GPL-2.0-or-later, Dojo first),
#     as the 0.3 release's dock/build_solo.sh packs them, made at configure time by sid_pack.py with the firmware's
#     own tools/sidpack.py into the build tree (sid_pack.h)
# m6502.h (ext/chips) is on build_dinghy.sh's include path but nothing includes it (player/cpu6510.h is its own).
# The units as build_dinghy.sh compiles them: fm1sid.c (the unit, with the platform and the glue), src/sid.c and
# src/sidsynth.c, player/psid.c, gt2.c, gt2edit.c, firmware/resid_glue.cc and reSID's 15 files (-fno-exceptions
# -fno-rtti -w). The own model's tables are made at boot (no SID_COMB_CONST: build_dinghy.sh's choice).
#
# Public: GPL-3.0 as a whole (fm1-sid's LICENSING.md). The tunes are GoatTracker's own example songs, carried by the
# firmware's repository and its release; no C64 tune from HVSC is compiled in.
include(${CMAKE_CURRENT_LIST_DIR}/dinghy.cmake)
set(_sid ${CMAKE_SOURCE_DIR}/cores/fm1-sid)
set(_sid_resid ${CMAKE_SOURCE_DIR}/cores/fm1-dock/modules/sid/resid)
if(NOT EXISTS ${_sid_resid}/sid.cc)
    message(FATAL_ERROR "sid core: ${_sid_resid} is empty (reSID 0.16): run git submodule update --init cores/fm1-dock")
endif()
set(_sid_vfs ${CMAKE_BINARY_DIR}/dinghy/sid_vfs.yaml)
file(WRITE ${_sid_vfs} "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_sid}/tests/host.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/sid_host.c\" } ] }\n")
# the tune pack (sid_host.c _pack), the release's songs in its order
set(_sid_tunes dojo unleash sanction consultant transylvanian hyperspace sixpack ghosttrackers everlasting cabrinigreen)
list(TRANSFORM _sid_tunes PREPEND ${_sid}/tunes/goattracker/)
list(TRANSFORM _sid_tunes APPEND .sng)
set(_sid_gen ${CMAKE_BINARY_DIR}/dinghy/sid_gen)
execute_process(COMMAND ${CR_PYTHON} -I ${CMAKE_CURRENT_LIST_DIR}/sid_pack.py ${_sid}/tools ${_sid_gen}/sid_pack.h
                        ${_sid_tunes}
                RESULT_VARIABLE _rc OUTPUT_QUIET)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "sid core: sid_pack.py failed (${CR_PYTHON})")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/sid_pack.py
             ${_sid}/tools/sidpack.py ${_sid_tunes})
# the other units: the firmware's (C, its own flags) and reSID's (C++)
set(_sid_c src/sid.c src/sidsynth.c player/psid.c player/gt2.c player/gt2edit.c)
set(_sid_cxx firmware/resid_glue.cc)
set(_sid_resid_units sid voice wave envelope filter extfilt pot wave6581_PST wave6581_PS_ wave6581_P_T wave6581__ST
    wave8580_PST wave8580_PS_ wave8580_P_T wave8580__ST)
set(_sid_extra "")
foreach(_f IN LISTS _sid_c _sid_cxx)
    list(APPEND _sid_extra ../../cores/fm1-sid/${_f})
endforeach()
foreach(_f IN LISTS _sid_resid_units)
    list(APPEND _sid_extra ../../cores/fm1-dock/modules/sid/resid/${_f}.cc)
endforeach()
fm1_add_core(sid ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_sid}
             UNIT core_sid.c DESC core_sid_desc.c EXTRA_SOURCES ${_sid_extra}
             FAMILY dinghy LICENSE GPL-3.0-only
             CREDITS "FM-1 SID by Joakim Eriksson: the C64 tune player (PSID and RSID on its own 6510), the GoatTracker 2 player and editor, the SID synth with its step sequencer, the effects and fm1-sid's own SID model. GPL-3.0-only as a whole: the player, the 6510 and the own model BSD-2-Clause. reSID 0.16 by Dag Lem, GPL-2.0-or-later. GoatTracker 2's playroutine by Lasse Öörni (Cadaver) and contributors, GPL-2.0-or-later. The songs are GoatTracker 2's example songs (GPL-2.0-or-later): Dojo, Unleash The F****** Fury, On A Sanction From CIA, The Consultant and Transylvanian Whipping by Cadaver (Covert Bitops), My Own Hyperspace by Jammer, Sixpack Of Cola by No-XS (after beek), GhostTrackers by Hein, Everlasting Annoyance by Richard Bayliss (The New Dimension), I'll Be A Pimp In Cabrini Green by Randall. On Dinghy 0.3 by zvenson, over Felucca's platform by Leo Kuroshita (Hügelton Instruments), with SLOOP's changes by isod89, GPL-3.0-only. The screen's font is rendered from DejaVu Sans Mono (Bitstream Vera licence, DejaVu changes public domain). The full notices: the release's licences/cores/sid/ (LICENSING.md)."
             NO_GEN VERSION 0.3 DEFINES APP_VER="0.3"
             INCLUDES ${_sid}/firmware ${_sid}/dinghy/src ${_sid}/src ${_sid}/player ${CMAKE_CURRENT_LIST_DIR}/sid_gen
                      ${_sid_gen} ${CMAKE_CURRENT_LIST_DIR}
             FLAGS "-ivfsoverlay" "${_sid_vfs}")
if(TARGET core_sid_obj)
    # (the overlaid host.c is not in the compiler's dependency list under its real name)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_sid.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/sid_host.c ${_sid_vfs} ${_sid_gen}/sid_pack.h)
    # the firmware's C units: its own code, its warnings off; the C++ ones (native and, read by fm1_add_rfm1, the
    # .rfm1's: after its C flags, so -std=gnu++11 wins)
    foreach(_f IN LISTS _sid_c)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/fm1-sid/${_f} PROPERTIES
                                    COMPILE_OPTIONS "-w")
    endforeach()
    set(_sid_cxx_opts "-std=gnu++11;-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-w;-iquote${_sid_resid}")
    foreach(_f IN LISTS _sid_cxx)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/fm1-sid/${_f} PROPERTIES
                                    COMPILE_OPTIONS "${_sid_cxx_opts}")
    endforeach()
    foreach(_f IN LISTS _sid_resid_units)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/fm1-dock/modules/sid/resid/${_f}.cc
                                    PROPERTIES COMPILE_OPTIONS "${_sid_cxx_opts}")
    endforeach()
endif()
dinghy_core(sid core_sid.c)
# (no golden check: FM-1 SID has no host sim of its own; its computer builds run the device binary in fm1-emu)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md): the main loop on the coroutine, so Asyncify
# (tools/rfm1/rfm1_link.cmake). The licence texts: its GPL-3.0, its LICENSING.md, Dinghy's, reSID's GPL-2.0 (also
# GoatTracker's playroutine's and its songs' licence), the BSD-2-Clause of its player and model, DejaVu's
fm1_add_rfm1(sid AUTHORS "Joakim Eriksson"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE" "LICENSING.md ({name})=LICENSING.md"
                           "LICENSING.md (Dinghy in {name})=dinghy/LICENSING.md"
                           "GPL-2.0-or-later (reSID 0.16 and GoatTracker 2 in {name})=${_sid_resid}/COPYING"
                           "BSD-2-Clause ({name}: the player, the 6510, the own SID model)=${CMAKE_CURRENT_LIST_DIR}/licences/sid/BSD-2-Clause-fm1-sid.txt"
                           "Bitstream Vera / DejaVu (the font in {name})=${CMAKE_CURRENT_LIST_DIR}/licences/sid/DejaVu-LICENSE.txt")
