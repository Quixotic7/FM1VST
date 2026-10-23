# SPDX-License-Identifier: GPL-3.0-only
# LOOM-8 (https://github.com/charlesvestal/fm1-loom, tag v0.6.1; GPL-3.0-only, on fm1-base and felucca-bsp, MPL-2.0):
# the fm1-base glue (core-glue/fm1base/README.md) over the unmodified submodule and its own submodules (base =
# fm1-base, base/bsp = felucca-bsp). v0.6.1 (2026-10-22) is LOOM-8's first release without "-beta" (the ones before
# it, to v0.6-beta, were all betas); fm1-base and felucca-bsp stay where v0.6-beta had them, app/samples unchanged.
# The DSP and the sequencer (fm1.toml "o2") are their own units (loom_*.c). The generate step is fm1-base's build.py
# generate() with the globals its main() sets from the app's directory (APP, CONF from app/fm1.toml, GEN =
# app/build/gen): the fonts, the wavetables, the factory samples, the build's hash, the presets, the screen reader's
# words; one Python expression, run in app/ (GEN_ROOT).
# app/app.c includes app/plat_loom.c, the second core's hardware: a clang VFS overlay (-ivfsoverlay, written below
# into the build folder) has the compiler read loom_plat.c in its place, the host's version of the same names; every
# file of the submodule is compiled unmodified.
set(_loom ${CMAKE_SOURCE_DIR}/cores/fm1-loom)
set(_loom_ver 0.6.1)
# build.py --release: APP_VERSION the release in capitals; the package identity fm1.toml's id_prefix (FM-1_10) + the
# major, minor and patch as two digits each
string(TOUPPER "${_loom_ver}" _loom_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_loom_ver}")
set(_loom_id "FM-1_10")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _loom_id "0")
    endif()
    string(APPEND _loom_id "${_d}")
endforeach()
set(_loom_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
set(_loom_vfs ${CMAKE_BINARY_DIR}/core_vfs/loom.yaml)
file(WRITE ${_loom_vfs}.in "{ \"version\": 0, \"case-sensitive\": \"false\", \"roots\": [ { \"type\": \"file\", \"name\": \"${_loom}/app/plat_loom.c\", \"external-contents\": \"${CMAKE_CURRENT_LIST_DIR}/loom_plat.c\" } ] }\n")
configure_file(${_loom_vfs}.in ${_loom_vfs} COPYONLY)
# UN-GATED (the owner's call, 2026-10-21): the factory 909 hi-hat (app/samples/909_hat.wav, LICENSING.md: a render of
# 9W9, its ER-99 engine and its hat samples) ships unchanged. In the owner's words: "ship the original, it is from the
# emulated 909 on the schwung firmware for Ableton Move that Charles Vestal made". The credits name that origin. X0X
# (core-glue/plat/core_x0x.cmake) stays gated: its 909 cymbals are a separate question, not decided
fm1_add_core(loom ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_loom} UNIT core_loom.c DESC core_loom_desc.c
             EXTRA_SOURCES loom_synth.c loom_drumline.c loom_sample.c loom_fx.c loom_bench.c loom_pattern.c loom_seq.c
             FAMILY fm1base LICENSE GPL-3.0-only
             CREDITS "LOOM-8 by Charles Vestal. Built on fm1-base and felucca-bsp (MPL-2.0), from Felucca by Leo Kuroshita (Hügelton Instruments). Factory breaks: free loops by u/FCTRYBEATS, used with their permission. 909 drums, the hi-hat included: renders of 9W9 (GPL-3.0), the emulated 909 on the schwung firmware for Ableton Move that Charles Vestal made. Recorded instruments: Versilian Studios Chamber Orchestra 2 Community Edition (CC0 1.0). Inspired by teenage engineering's OP-XY, not affiliated with teenage engineering."
             CREDITS_FILES LICENSING.md base/assets/fonts/Barlow-OFL.txt base/assets/fonts/Terminus-LICENSE.txt
             GEN ann_words.h felucca_font.h loom_build.h loom_font.h presets.h samples.h wavetables.h
             GEN_ROOT app GEN_PYTHON "${_loom_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_loom_ver}
             FLAGS -ffp-contract=off "-ivfsoverlay${_loom_vfs}"
             DEFINES APP_VERSION="${_loom_app_ver}" FELUCCA_ID="${_loom_id}"
             INCLUDES ${_loom}/app ${_loom}/app/dsp ${_loom}/app/seq ${_loom}/base/include ${_loom}/base/device
                      ${_loom}/base/lib)
if(TARGET core_loom_obj)
    # (the overlay's file: the unit's dependency, which the compiler's depfile also names)
    set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/core_loom.c APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_LIST_DIR}/loom_plat.c ${_loom_vfs})
endif()
# the golden check: fm1-base's host simulator (build.py --host: base/host/plat_host.c with the app, PLAT_HOST, its o2
# units, fm1.toml's backup) and the core, one script
include(${CMAKE_CURRENT_LIST_DIR}/../plat/plat_golden.cmake)
plat_golden(loom GEN_TARGET gen_loom
            SOURCES ${_loom}/base/host/plat_host.c ${_loom}/app/dsp/synth.c ${_loom}/app/dsp/drumline.c
                    ${_loom}/app/dsp/sample.c ${_loom}/app/dsp/fx.c ${_loom}/app/dsp/bench.c ${_loom}/app/seq/pattern.c
                    ${_loom}/app/seq/seq.c
            DEFINES PLAT_HOST FM1B_BACKUP=1 APP_VERSION="${_loom_app_ver}"
            INCLUDES ${_loom}/base/device ${_loom}/base/include ${_loom}/base/lib ${_loom}/app ${_loom}/app/build/gen
            SCRIPT ${CMAKE_CURRENT_LIST_DIR}/golden_loom.txt)
