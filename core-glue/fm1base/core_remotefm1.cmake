# SPDX-License-Identifier: GPL-3.0-only
# RemoteFM1 0.3.2 (https://github.com/Quixotic7/RemoteFM1, tag v0.3.2 = f4da149; the firmware MPL-2.0, with fm1-base
# and felucca-bsp MPL-2.0, saadet's FM6 with MSFA Apache-2.0, the Jersey 10 font OFL-1.1): the fm1-base glue
# (core-glue/fm1base/README.md) over the unmodified submodule and its own submodules (base = fm1-base @ 934e40d,
# base/bsp = felucca-bsp @ 2cd696a). Its standalone instrument as an .rfm1: the DX7 with Yamaha's ROM cartridges, the
# MIDI controller, the loops, the arp, the scales (core_remotefm1.c; the panel half has no host here).
#
#   core_remotefm1.c     the unity build (fm1-base, patched, and firmware/app.c with src/ and sa/), the glue
#   sa/sa_synth.c        the DX7 (saadet's FM6), fm1.toml's o2 unit, with PLAT_HOST as fm1-base's host and browser
#                        builds give the o2 units (its .pool section off)
#   remotefm1_port.c     read in place of src/rfm_dev.c (the overlay below)
#
# fm1-base and felucca-bsp as build.sh builds them: a copy of base/ in the build folder with firmware/patches/fm1-base
# and firmware/patches/felucca-bsp applied (git apply: exactly, or the configure stops), made again when the patches
# or the pins change; the unit reads fm1-base from there. The generate step is fm1-base's build.py generate() on the
# unpatched base/tools (the fonts, the screen reader's words, fm1.toml's gen lines: FM6's tables, sa_config.h, the
# pixel font) without gen_dx7_rom.py, which runs after it with --dir FM1_DX7_ROM_DIR.
#
# Yamaha's DX7 ROM voices: gen_dx7_rom.py packs ROM1A..ROM4B from the builder's own .syx files into build/gen/dx7_rom.h
# (in the submodule's build/, never committed). FM1_DX7_ROM_DIR (default $ENV{DX7_ROM_DIR}) names the folder; without
# the eight files the configure fails (and core_remotefm1.c does not compile against a header without them), so a
# build with only the INIT VOICE is never made. Neither the .syx files nor the header are ever committed here: only
# the compiled .rfm1 carries the voices, as ChoralRoot and Melodee compile in the CZ-1 and Prophet-5 sounds.
set(_rf ${CMAKE_SOURCE_DIR}/cores/RemoteFM1)
set(_rf_ver 0.3.2)
set(_rf_fw ${_rf}/firmware)
set(_rf_bin ${CMAKE_BINARY_DIR}/remotefm1)
set(_rf_base ${_rf_bin}/fm1-base)

# ---- the ROM cartridges' folder
set(FM1_DX7_ROM_DIR "$ENV{DX7_ROM_DIR}" CACHE PATH
    "RemoteFM1: the folder of Yamaha's DX7 ROM cartridges rom1a.syx .. rom4b.syx (default: the DX7_ROM_DIR environment variable)")
if(NOT FM1_DX7_ROM_DIR AND NOT "$ENV{DX7_ROM_DIR}" STREQUAL "")
    set(FM1_DX7_ROM_DIR "$ENV{DX7_ROM_DIR}" CACHE PATH "" FORCE)   # (a first configure without it, then with it)
endif()
set(_rf_rom_missing "")
foreach(_b 1a 1b 2a 2b 3a 3b 4a 4b)
    set(_found "")
    if(FM1_DX7_ROM_DIR AND IS_DIRECTORY "${FM1_DX7_ROM_DIR}")
        file(GLOB _all "${FM1_DX7_ROM_DIR}/*")
        foreach(_f IN LISTS _all)
            get_filename_component(_n "${_f}" NAME)
            string(TOLOWER "${_n}" _n)
            if(_n STREQUAL "rom${_b}.syx")
                set(_found "${_f}")
            endif()
        endforeach()
    endif()
    if(NOT _found)
        list(APPEND _rf_rom_missing rom${_b}.syx)
    endif()
endforeach()
if(_rf_rom_missing)
    string(REPLACE ";" " " _rf_rom_missing "${_rf_rom_missing}")
    string(CONCAT _why "RemoteFM1 core: Yamaha's DX7 ROM cartridges are compiled into it, and ${_rf_rom_missing} are not in "
             "FM1_DX7_ROM_DIR (\"${FM1_DX7_ROM_DIR}\"). Set the DX7_ROM_DIR environment variable or "
             "-DFM1_DX7_ROM_DIR=<folder> to the folder of rom1a.syx .. rom4b.syx (32-voice bulk dumps), or leave the "
             "core out (-DFM1_ONLY_CORES=...). A build with only the INIT VOICE is never made.")
    if(FM1_ONLY_CORES OR FM1_RFM1)
        message(FATAL_ERROR "${_why}")
    endif()
    # (a build of every core with no .rfm1, the release's: RemoteFM1 is left out, the others build)
    message(WARNING "${_why} Left out of this build.")
    return()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FM1_DX7_ROM_DIR})

# ---- fm1-base and felucca-bsp patched, as build.sh: base/ copied, then each patch with git apply
file(GLOB _rf_p_base ${_rf_fw}/patches/fm1-base/*.patch)
file(GLOB _rf_p_bsp ${_rf_fw}/patches/felucca-bsp/*.patch)
list(SORT _rf_p_base)
list(SORT _rf_p_bsp)
execute_process(COMMAND git -C ${_rf}/base rev-parse HEAD OUTPUT_VARIABLE _pin_base OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
execute_process(COMMAND git -C ${_rf}/base/bsp rev-parse HEAD OUTPUT_VARIABLE _pin_bsp OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(NOT EXISTS ${_rf}/base/tools/build.py OR NOT EXISTS ${_rf}/base/bsp/chip/usb.c)
    message(FATAL_ERROR "RemoteFM1 core: ${_rf}/base is empty: git submodule update --init --recursive cores/RemoteFM1")
endif()
set(_stamp "base ${_pin_base} bsp ${_pin_bsp}\n")
foreach(_p IN LISTS _rf_p_base _rf_p_bsp)
    file(MD5 ${_p} _h)
    get_filename_component(_n ${_p} NAME)
    string(APPEND _stamp "${_n} ${_h}\n")
endforeach()
set(_old "")
if(EXISTS ${_rf_bin}/fm1-base.stamp)
    file(READ ${_rf_bin}/fm1-base.stamp _old)
endif()
if(NOT _old STREQUAL _stamp OR NOT EXISTS ${_rf_base}/include/plat.h)
    message(STATUS "remotefm1 core: fm1-base ${_pin_base} and felucca-bsp ${_pin_bsp} with RemoteFM1's patches, into ${_rf_base}")
    file(REMOVE_RECURSE ${_rf_base})
    file(REMOVE ${_rf_bin}/fm1-base.stamp)
    foreach(_d include device lib host ble tools)
        file(COPY ${_rf}/base/${_d} DESTINATION ${_rf_base})
    endforeach()
    foreach(_d chip boards)
        file(COPY ${_rf}/base/bsp/${_d} DESTINATION ${_rf_base}/bsp)
    endforeach()
    # (GIT_CEILING_DIRECTORIES: git apply outside any repository, as build.sh: the build folder may be inside one)
    foreach(_p IN LISTS _rf_p_base)
        execute_process(COMMAND ${CMAKE_COMMAND} -E env GIT_CEILING_DIRECTORIES=${_rf_bin} git apply ${_p}
                        WORKING_DIRECTORY ${_rf_base} RESULT_VARIABLE _rc ERROR_VARIABLE _err)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "RemoteFM1 core: ${_p} does not apply to fm1-base ${_pin_base}: ${_err}")
        endif()
    endforeach()
    foreach(_p IN LISTS _rf_p_bsp)
        execute_process(COMMAND ${CMAKE_COMMAND} -E env GIT_CEILING_DIRECTORIES=${_rf_bin} git apply ${_p}
                        WORKING_DIRECTORY ${_rf_base}/bsp RESULT_VARIABLE _rc ERROR_VARIABLE _err)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "RemoteFM1 core: ${_p} does not apply to felucca-bsp ${_pin_bsp}: ${_err}")
        endif()
    endforeach()
    file(WRITE ${_rf_bin}/fm1-base.stamp "${_stamp}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_rf_p_base} ${_rf_p_bsp})

# ---- the overlays (a clang VFS overlay, as LOOM-8's and Ripple's glues): src/rfm_dev.c is remotefm1_port.c, and
# src/rfm_main.c the submodule's file with rf_run's one turn a pass (checked: the configure fails if the lines moved)
file(READ ${_rf_fw}/src/rfm_main.c _t)
set(_from " * Copyright (C) 2026 RemoteFM1 contributors */\n")
set(_to " * Copyright (C) 2026 RemoteFM1 contributors\n * Changed by FM1VST (core-glue/fm1base/core_remotefm1.cmake, an overlay of this MPL-2.0 file for its host builds):\n * rf_run takes one turn a pass, as in the host simulator (simulated time: the tick counter does not move inside a\n * pass, so the 2 ms slice would never end). */\n")
set(_from2 "#ifdef PLAT_HOST\n        break;\n#endif\n    } while (rf_ticks() - t0 < RF_SLICE_US * PLAT_TICKS_PER_US);")
set(_to2 "        break;                                      /* (FM1VST: one turn a pass) */\n    } while (rf_ticks() - t0 < RF_SLICE_US * PLAT_TICKS_PER_US);")
foreach(_k "" 2)
    string(FIND "${_t}" "${_from${_k}}" _at)
    if(_at LESS 0)
        message(FATAL_ERROR "RemoteFM1 core: ${_rf_fw}/src/rfm_main.c has no \"${_from${_k}}\" any more: the overlay "
                            "(core_remotefm1.cmake) needs a look")
    endif()
    string(REPLACE "${_from${_k}}" "${_to${_k}}" _t "${_t}")
endforeach()
set(_old "")
if(EXISTS ${_rf_bin}/rfm_main.c)
    file(READ ${_rf_bin}/rfm_main.c _old)
endif()
if(NOT _old STREQUAL _t)
    file(WRITE ${_rf_bin}/rfm_main.c "${_t}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_rf_fw}/src/rfm_main.c)
set(_rf_vfs ${_rf_bin}/remotefm1_vfs.yaml)
set(_v "{ \"version\": 0, \"case-sensitive\": \"false\", \"use-external-names\": false, \"roots\": [\n")
string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_rf_fw}/src/rfm_main.c\", \"external-contents\": \"${_rf_bin}/rfm_main.c\" },\n")
string(APPEND _v "  { \"type\": \"file\", \"name\": \"${_rf_fw}/src/rfm_dev.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/remotefm1_port.c\" } ] }\n")
file(WRITE ${_rf_vfs}.in "${_v}")
configure_file(${_rf_vfs}.in ${_rf_vfs} COPYONLY)

# ---- the core. build.py --release 0.3.2: APP_VERSION "0.3.2", the identity fm1.toml's id_prefix FM-1_52 + 00 03 01
set(_rf_id FM-1_52000302)
set(_rf_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', {k: ([g for g in v if 'gen_dx7_rom' not in g] if k == 'gen' else v) for k, v in b.read_conf(b.APP / 'fm1.toml').items()}), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
file(RELATIVE_PATH _rf_rel ${CMAKE_CURRENT_LIST_DIR} ${_rf_fw})
fm1_add_core(remotefm1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_rf} UNIT core_remotefm1.c DESC core_remotefm1_desc.c
             EXTRA_SOURCES ${_rf_rel}/sa/sa_synth.c
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "RemoteFM1 by Quixotic7 (MPL-2.0): the FM-1 as a DX7 of its own, and as the panel of a firmware on a computer (here, an .rfm1 itself, its standalone instrument). Built on fm1-base by Charles Vestal, with Felucca code by Leo Kuroshita (Hügelton Instruments), and felucca-bsp by Leo Kuroshita, with Kerem Kilic's USB host and keremimo's clock sync (MPL-2.0). The DX7: FM6 from saadet by Kerem Kilic (Ellic Studio), with MSFA by Google Inc., Pascal Gauthier and Jean Pierre Cimalando (Apache-2.0). Fonts: Jersey 10 by Sarah Cadigan-Fried, and fm1-base's Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1. The built-in voices are Yamaha's DX7 ROM cartridges ROM1A..ROM4B. The rights stay with Yamaha Corporation."
             GEN felucca_font.h ann_words.h fm6_tables.h sa_config.h sa_font.h dx7_rom.h GEN_ROOT firmware
             GEN_PYTHON "${_rf_gen}"
             EXTRA_GEN "tools/gen_dx7_rom.py --out build/gen/dx7_rom.h --dir '${FM1_DX7_ROM_DIR}'"
             PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_rf_ver}
             FLAGS -ffp-contract=off "-ivfsoverlay${_rf_vfs}"
             DEFINES APP_VERSION="${_rf_ver}" FELUCCA_ID="${_rf_id}"
             INCLUDES ${_rf_fw} ${_rf_base}/include ${_rf_base}/device ${_rf_base}/lib)
if(TARGET core_remotefm1_obj)
    set(_g ${CMAKE_CURRENT_LIST_DIR})
    set_property(SOURCE ${_g}/core_remotefm1.c APPEND PROPERTY OBJECT_DEPENDS ${_rf_bin}/rfm_main.c ${_rf_vfs}
                 ${_g}/remotefm1_port.c ${_rf_bin}/fm1-base.stamp)
    # the o2 unit: build.py's host / browser flags (-DPLAT_HOST, its own directory first)
    set_source_files_properties(${_g}/${_rf_rel}/sa/sa_synth.c PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off;-DPLAT_HOST;-I${_rf_fw}/sa")
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: RemoteFM1's MPL-2.0 (fm1-base's and
# felucca-bsp's are the same text) and LICENSING.md, fm1-base's and felucca-bsp's notes, MSFA's Apache-2.0, the fonts'
# OFL (Jersey 10; Barlow, the font set fm1-base renders); fm1_add_rfm1 adds the glue's GPL-3.0 and the runtime's
fm1_add_rfm1(remotefm1 AUTHORS "Quixotic7"
             LICENSE_FILES "MPL-2.0 ({name}, fm1-base, felucca-bsp)=LICENSES/MPL-2.0.txt"
                           "LICENSING.md ({name})=LICENSING.md"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "Apache-2.0 (MSFA, in saadet's FM6)=firmware/third_party/saadet-fm6/LICENSES/Apache-2.0-msfa.txt"
                           "OFL-1.1 (the Jersey 10 font in {name})=firmware/third_party/jersey10/OFL.txt"
                           "OFL-1.1 (the Barlow font in {name})=base/assets/fonts/Barlow-OFL.txt")
