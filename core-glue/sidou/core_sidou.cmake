# SPDX-License-Identifier: GPL-3.0-only
# SIDOU (https://github.com/dsablic/sidou, tag v1.9.0 = 63b0bea5; GPL-3.0-or-later, the image GPL-3.0-only), a C64 SID
# machine: bare-metal firmware on felucca-bsp (1.8 on; Felucca's HAL before), its own glue family
# (core-glue/sidou/README.md) over the unmodified submodule. Built as the firmware's CMake builds it: the portable
# instrument (app/, core/: their add_library lists, read here) as their own units, the device layer (platform/fm1:
# main.c, midi.c with felucca-bsp's bsp/chip/usb.c and ota.c (MPL-2.0), audio.c, store_fm1.c, input.c, ui.c) over
# this folder's hal/, lcd.c replaced by sidou_lcd.c. Every table and the demo song are committed (NO_GEN). Bluetooth
# MIDI (SIDOU_BLE, on in the firmware's own build) is left out: a host has no radio, so main.c, midi.c, ui.c and
# app/ build as the firmware does with SIDOU_BLE=OFF (the status line reads READY., no radio report). The identity
# is the release's (ci.yml: a v* tag builds SIDOU_PRODUCT FM-1_964).
set(_sd ${CMAKE_SOURCE_DIR}/cores/sidou)
set(_sd_fm1 ${_sd}/platform/fm1)
set(_sd_glue ${CMAKE_CURRENT_LIST_DIR})
set(_sd_bin ${CMAKE_BINARY_DIR}/sidou_overlay)
file(RELATIVE_PATH _sd_rel ${_sd_glue} ${_sd})

# the version, as the firmware's project() line has it
file(STRINGS ${_sd}/CMakeLists.txt _sd_proj REGEX "^project\\(sidou VERSION [0-9]+\\.[0-9]+\\.[0-9]+")
if(NOT _sd_proj MATCHES "VERSION ([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "sidou: no project(sidou VERSION x.y.z) in ${_sd}/CMakeLists.txt (a new pin: check the glue)")
endif()
set(_sd_major ${CMAKE_MATCH_1})
set(_sd_minor ${CMAKE_MATCH_2})
set(_sd_patch ${CMAKE_MATCH_3})
set(_sd_ver "${_sd_major}.${_sd_minor}.${_sd_patch}")

# the portable units: app/CMakeLists.txt's sidou_live and core/CMakeLists.txt's sidou_core
function(sidou_lib_sources cmakelists lib dir out)
    file(READ ${cmakelists} _t)
    if(NOT _t MATCHES "add_library\\(${lib} STATIC([^)]*)\\)")
        message(FATAL_ERROR "sidou: no add_library(${lib} STATIC ..) in ${cmakelists} (a new pin: check the glue)")
    endif()
    string(REGEX MATCHALL "[A-Za-z0-9_/.-]+\\.c" _srcs "${CMAKE_MATCH_1}")
    list(TRANSFORM _srcs PREPEND ${dir}/)
    set(${out} ${_srcs} PARENT_SCOPE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${cmakelists})
endfunction()
sidou_lib_sources(${_sd}/app/CMakeLists.txt sidou_live ${_sd_rel}/app _sd_app)
sidou_lib_sources(${_sd}/core/CMakeLists.txt sidou_core ${_sd_rel}/core _sd_core)

# ---- the overlays: the submodule's file, these lines changed (string(REPLACE), each checked), into the build folder,
# read in its place through a clang VFS overlay; with them every HAL header (platform/fm1/bsp/chip, bsp/boards/fm1, hal: fm1_*.h, also where
# usb.c and midi_uart.c include it by a relative path) is read as this folder's hal/
file(MAKE_DIRECTORY ${_sd_bin})
function(sidou_overlay src dst)           # ARGN: old new old new .. (each ";" written <SC>)
    file(READ ${src} _t)
    set(_args ${ARGN})
    list(LENGTH _args _n)
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE 0 ${_last} 2)
        math(EXPR _j "${_i} + 1")
        list(GET _args ${_i} _old)
        list(GET _args ${_j} _new)
        string(REPLACE "<SC>" ";" _old "${_old}")
        string(REPLACE "<SC>" ";" _new "${_new}")
        string(FIND "${_t}" "${_old}" _at)
        if(_at LESS 0)
            message(FATAL_ERROR "sidou: ${src} has no \"${_old}\" any more: the overlay (core_sidou.cmake) needs a look")
        endif()
        string(REPLACE "${_old}" "${_new}" _t "${_t}")
    endforeach()
    set(_old "")
    if(EXISTS ${dst})
        file(READ ${dst} _old)
    endif()
    if(NOT _old STREQUAL _t)
        file(WRITE ${dst} "${_t}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${src})
endfunction()
set(_sd_note "/* Changed by FM1VST (core-glue/sidou/core_sidou.cmake, an overlay of this file for its host build): ")
sidou_overlay(${_sd_fm1}/main.c ${_sd_bin}/main.c
    "// SPDX-License-Identifier: GPL-3.0-or-later\n#include <stdint.h>\n"
    "// SPDX-License-Identifier: GPL-3.0-or-later\n${_sd_note}the main loop's\n * wait for the next interrupt gives the clock back to the host, an update request is not run (a core has no update\n * loader) and fm1_cstart is never run (core_sidou.c runs its boot guard, then sidou_main). */\n#include <stdint.h>\n"
    "    if (midi_usb_live()) {\n        uint32_t t = timer_irqs<SC>\n        while (timer_irqs == t) {\n        }\n        return<SC>\n    }\n    __asm__ volatile(\"idle\")<SC>\n"
    "    fm1core_sd_wait()<SC> /* FM1VST: the main loop's coroutine gives the clock back */\n"
    "        if (update_poll()) {\n            upgrade()<SC>\n        }"
    "        if (update_poll()) {\n            fm1core_sd_upgrade_refused()<SC> /* FM1VST */\n        }"
    "void fm1_cstart(void) {\n"
    "__attribute__((unused)) static void fm1core_sd_cstart_unused(void) { /* FM1VST: never run */\n")
sidou_overlay(${_sd_fm1}/audio.c ${_sd_bin}/audio.c
    "// SPDX-License-Identifier: GPL-3.0-or-later\n#include \"audio.h\""
    "// SPDX-License-Identifier: GPL-3.0-or-later\n${_sd_note}no `.set` for the\n * emulator tests' symbol, and a producer's wait for the drain gives the clock back to the host. */\n#include \"audio.h\""
    "__asm__(\".set song_transport, player_isr\")<SC>\n"
    "/* FM1VST: (the emulator tests' song_transport symbol) */\n"
    "    if (midi_usb_live()) {\n        return<SC>\n    }\n    __asm__ volatile(\"idle\" ::: \"memory\")<SC>\n"
    "    fm1core_sd_wait()<SC> /* FM1VST: the main loop's coroutine gives the clock back */\n")
sidou_overlay(${_sd_fm1}/store_fm1.c ${_sd_bin}/store_fm1.c
    "// SPDX-License-Identifier: GPL-3.0-or-later\n#include \"store_fm1.h\""
    "// SPDX-License-Identifier: GPL-3.0-or-later\n${_sd_note}one thread (no\n * interrupt switch) and ALNK0's pending register a variable. */\n#include \"store_fm1.h\""
    "#define ALNK_CON2 (*(volatile uint8_t*)0x12E08u)"
    "#define ALNK_CON2 fm1core_sd_alnk_con2 /* FM1VST */"
    "    __asm__ volatile(\"cli\" ::: \"memory\")<SC>\n"
    "    /* FM1VST: one thread */\n"
    "    __asm__ volatile(\"csync\\n\\tsti\" ::: \"memory\")<SC>\n"
    "    /* FM1VST: one thread */\n")
sidou_overlay(${_sd_fm1}/input.c ${_sd_bin}/input.c
    "// SPDX-License-Identifier: GPL-3.0-or-later\n#include \"input.h\""
    "// SPDX-License-Identifier: GPL-3.0-or-later\n${_sd_note}the wait for the\n * next scan frame gives the clock back to the host. */\n#include \"input.h\""
    "    while (fm1_in.frames - from < FM1_DEB_PRESS + 1u) {\n        __asm__ volatile(\"idle\" ::: \"memory\")<SC>\n"
    "    while (fm1_in.frames - from < FM1_DEB_PRESS + 1u) {\n        fm1core_sd_wait()<SC> /* FM1VST: the next tick */\n")
file(GLOB _sd_hal RELATIVE ${_sd_glue}/hal ${_sd_glue}/hal/*.h)
set(_v "{ \"version\": 0, \"case-sensitive\": \"true\", \"use-external-names\": false, \"roots\": [\n")
foreach(_f main.c audio.c store_fm1.c input.c)
    string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_sd_fm1}/${_f}\", \"external-contents\": \"${_sd_bin}/${_f}\" },\n")
endforeach()
# felucca-bsp's layout (1.8 on): the chip's headers in bsp/chip, the board's in bsp/boards/fm1, and hal/ keeps the
# update loader's copies; a shim stands in for its header wherever the firmware has one
foreach(_h IN LISTS _sd_hal)
    set(_found 0)
    foreach(_d bsp/chip bsp/boards/fm1 hal)
        if(EXISTS ${_sd_fm1}/${_d}/${_h})
            set(_found 1)
            string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_sd_fm1}/${_d}/${_h}\", \"external-contents\": \"${_sd_glue}/hal/${_h}\" },\n")
        endif()
    endforeach()
    if(NOT _found)
        message(FATAL_ERROR "sidou: hal/${_h} shims a header the firmware no longer has (${_sd_fm1}/bsp/chip, bsp/boards/fm1, hal)")
    endif()
endforeach()
string(REGEX REPLACE ",\n$" " ] }\n" _v "${_v}")
set(_sd_vfs ${_sd_bin}/sidou_vfs.yaml)
file(WRITE ${_sd_vfs}.in "${_v}")
configure_file(${_sd_vfs}.in ${_sd_vfs} COPYONLY)

set(_sd_platform sidou_ui.c)
fm1_add_core(sidou ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_sd} UNIT core_sidou.c DESC core_sidou_desc.c
             EXTRA_SOURCES sidou_midi.c sidou_audio.c sidou_store.c sidou_input.c sidou_lcd.c ${_sd_platform}
                           ${_sd_app} ${_sd_core}
             FAMILY sidou LICENSE GPL-3.0-only
             CREDITS "SIDOU by SIDOU contributors (Denis Sablić, github.com/dsablic/sidou), GPL-3.0-or-later, the firmware image GPL-3.0-only with felucca-bsp's USB stack and update protocol (MPL-2.0, by Leo Kuroshita, Hügelton Instruments, Kerem Kilic and its contributors) and Felucca's TRS MIDI driver (Leo Kuroshita, Hügelton Instruments).\nThe SID's combined waveforms, filter curves, voice DACs, oscillator and envelope follow reSIDfp (Antti S. Lankila, Leandro Nini, Dag Lem, the VICE Project), GPL-2.0-or-later. The song player carries GoatTracker 2 forward (Lasse Öörni and its contributors), GPL-2.0-or-later. The PSID player follows FM-1 SID's (Joakim Eriksson), BSD-2-Clause. The light reverb is a C port of Mutable Instruments Rings (Emilie Gillet), MIT.\nThe tune SIDOU LOOP, the drum kit and the digi drums are SIDOU's own (GPL-3.0-or-later)."
             VERSION ${_sd_ver}
             SUBMODULES_RECURSIVE
             NO_GEN
             FLAGS -ffp-contract=off -include ${_sd_glue}/sidou_pre.h "-ivfsoverlay${_sd_vfs}"
             DEFINES SIDOU_ALL_COMBINED_PROFILES SIDOU_ALL_COMBINED_PROFILES_8580 SIDOU_VERSION="${_sd_ver}"
                     SIDOU_VERSION_MAJOR=${_sd_major} SIDOU_VERSION_MINOR=${_sd_minor} SIDOU_VERSION_PATCH=${_sd_patch}
                     SIDOU_PRODUCT="FM-1_964" SIDOU_MIDI_OUT_CHANNEL=1 SIDOU_MIDI_OUT_NRPN=0 STORE_SYSEX_GAP_MS=250u
             INCLUDES ${_sd_glue}/hal ${_sd_glue} ${_sd_fm1} ${_sd}/app ${_sd}/core)
if(TARGET core_sidou_obj)
    # the firmware's units (warnings off, as the firmware's flags are not these) and the glue's over them: the
    # sections header first (not in a unit that names something `section` itself: app/chip_view.c's function, whose
    # unit places nothing), every HAL header and the four overlaid files through the VFS overlay
    set(_sd_fw sidou_midi.c sidou_audio.c sidou_store.c sidou_input.c ${_sd_platform} ${_sd_app} ${_sd_core})
    foreach(_s IN LISTS _sd_fw)
        set(_o "-w;-ffp-contract=off;-ivfsoverlay${_sd_vfs}")
        file(READ ${_sd_glue}/${_s} _t)
        if(NOT _t MATCHES "[^,] section *\\(|[^A-Za-z0-9_ (]section *\\(")
            set(_o "${_o};-include;${_sd_glue}/sidou_pre.h")
        endif()
        set_source_files_properties(${_sd_glue}/${_s} PROPERTIES COMPILE_OPTIONS "${_o}"
            OBJECT_DEPENDS "${_sd_vfs};${_sd_bin}/main.c;${_sd_bin}/audio.c;${_sd_bin}/store_fm1.c;${_sd_bin}/input.c")
    endforeach()
    set_source_files_properties(${_sd_glue}/core_sidou.c PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off;-include;${_sd_glue}/sidou_pre.h;-ivfsoverlay${_sd_vfs}"
        OBJECT_DEPENDS "${_sd_vfs};${_sd_bin}/main.c;${_sd_bin}/audio.c;${_sd_bin}/store_fm1.c;${_sd_bin}/input.c")
    set_source_files_properties(${_sd_glue}/sidou_lcd.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what the firmware ships for what is
# compiled in (its LICENSES/: GPL-3.0 for the image and SIDOU's own, MPL-2.0 and felucca-bsp's NOTICE for its usb.c and
# ota.c, GPL-2.0-or-later for reSIDfp's and GoatTracker's parts, BSD-2-Clause for FM-1 SID's PSID design, MIT for Rings' reverb, GoatTracker's playroutine notice for the
# starter tunes' players) and its LICENSING.md
fm1_add_rfm1(sidou AUTHORS "SIDOU contributors (Denis Sablić)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSES/GPL-3.0-only.txt"
                           "GPL-3.0-or-later ({name})=LICENSES/GPL-3.0-or-later.txt"
                           "MPL-2.0 (felucca-bsp's USB stack and update protocol in {name})=LICENSES/MPL-2.0.txt"
                           "felucca-bsp NOTICE ({name})=platform/fm1/bsp/NOTICE"
                           "GPL-2.0-or-later (reSIDfp and GoatTracker 2 in {name})=LICENSES/GPL-2.0-or-later.txt"
                           "BSD-2-Clause (FM-1 SID's PSID player in {name})=LICENSES/BSD-2-Clause.txt"
                           "MIT (Mutable Instruments Rings' reverb in {name})=LICENSES/MIT.txt"
                           "LicenseRef-GoatTracker-Playroutine ({name})=LICENSES/LicenseRef-GoatTracker-Playroutine.txt"
                           "LICENSING.md ({name})=LICENSING.md")
