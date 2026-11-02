# SPDX-License-Identifier: GPL-3.0-only
# Berlinan 0.5.1 (https://github.com/jiripisa/fm1-berlinan, tag v0.5.1 = afe2d93; Jiří Píša's generative Berlin School
# MIDI sequencer: GPL-2.0-or-later, on fm1-base and felucca-bsp, MPL-2.0, its own submodules base = fm1-base @ 15dff6d,
# base/bsp = felucca-bsp @ 6b087c2, AMB-1's): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified
# submodule, from AMB-1's glue. The engine (fm1.toml "o2": app/engine/*.c, app/engine/softfloat/*.c, Berkeley
# SoftFloat 3e, BSD-3-Clause) is its own units, straight from the submodule, with build.py's host flags
# (-O2 -ffp-contract=off -std=c99 -w -DPLAT_HOST, fm1.toml's include list, each file's own directory).
# app/app.c includes app/libc/libc.c when PLAT_HOST is off (the device's libc for the freestanding toolchain): a clang
# VFS overlay (-ivfsoverlay, written below; emcc takes it too) reads berlinan_nolibc.c (empty) in its place.
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (fm1-base's Barlow Semi Condensed) and the screen reader's
# words; one Python expression, run in app/ (GEN_ROOT). Not gated.
set(_bn ${CMAKE_SOURCE_DIR}/cores/fm1-berlinan)
set(_bn_ver 0.5.1)
# build.py --release 0.5.1: APP_VERSION "0.5.1", the identity fm1.toml's id_prefix FM-1_23 + 00 05 01 (README:
# "0.5.1 answers FM-1_23000501")
string(TOUPPER "${_bn_ver}" _bn_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_bn_ver}")
set(_bn_id "FM-1_23")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _bn_id "0")
    endif()
    string(APPEND _bn_id "${_d}")
endforeach()
set(_bn_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
set(_bn_vfs ${CMAKE_BINARY_DIR}/core_vfs/berlinan.yaml)
file(WRITE ${_bn_vfs}.in "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_bn}/app/libc/libc.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/berlinan_nolibc.c\" } ] }\n")
configure_file(${_bn_vfs}.in ${_bn_vfs} COPYONLY)
# fm1.toml o2, its globs expanded in its order (build.py o2_files)
set(_bn_o2 "")
foreach(_g engine/*.c engine/softfloat/*.c)
    file(GLOB _fs RELATIVE ${_bn}/app ${_bn}/app/${_g})
    list(SORT _fs)
    list(APPEND _bn_o2 ${_fs})
endforeach()
set(_bn_o2_rel "")
foreach(_f IN LISTS _bn_o2)
    list(APPEND _bn_o2_rel ../../cores/fm1-berlinan/app/${_f})
endforeach()
fm1_add_core(berlinan ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_bn} UNIT core_berlinan.c DESC core_berlinan_desc.c
             EXTRA_SOURCES ${_bn_o2_rel}
             FAMILY fm1base LICENSE GPL-2.0-or-later
             CREDITS "Berlinan for the FM-1 by Jiří Píša (GPL-2.0-or-later): its engine is a C translation of Berlinan for macOS by Jiří Píša, with Berkeley SoftFloat 3e by John R. Hauser (BSD-3-Clause, The Regents of the University of California) for its arithmetic. Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_bn_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_bn_ver}
             FLAGS -ffp-contract=off "-ivfsoverlay${_bn_vfs}"
             DEFINES APP_VERSION="${_bn_app_ver}" FELUCCA_ID="${_bn_id}"
             INCLUDES ${_bn}/app ${_bn}/app/engine ${_bn}/base/include ${_bn}/base/device ${_bn}/base/lib)
if(TARGET core_berlinan_obj)
    # (the overlay's file: the unit's dependency, which the compiler's depfile also names)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_berlinan.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/berlinan_nolibc.c ${_bn_vfs})
    # the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects' host flags,
    # fm1.toml's include list (engine), each file's own directory
    foreach(_f IN LISTS _bn_o2)
        get_filename_component(_d ${_bn}/app/${_f} DIRECTORY)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/fm1-berlinan/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=c99;-w;-ffp-contract=off;-DPLAT_HOST;-I${_d};-I${_bn}/app/engine")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: Berlinan's LICENSE
# (GPL-2.0-or-later), SoftFloat's (BSD-3-Clause), fm1-base's and felucca-bsp's MPL-2.0 with their notes, the Barlow OFL.
# Berlinan makes no sound (a MIDI sequencer: its notes go out over USB-MIDI), so the chord's RMS is 0: --min-rms 0.
# PLAY starts the Demo trio at 2 s; the MIDI out (rfm1_run --midi-out) shows it plays
fm1_add_rfm1(berlinan AUTHORS "Jiří Píša"
             LICENSE_FILES "GPL-2.0-or-later ({name}, LICENSE)=LICENSE"
                           "BSD-3-Clause (Berkeley SoftFloat 3e, app/engine/softfloat/LICENSE)=app/engine/softfloat/LICENSE"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt"
             RUN_ARGS --press PLAY@2000 --min-rms 0)
