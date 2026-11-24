# SPDX-License-Identifier: GPL-3.0-only
# Golf 1.0 (https://github.com/barnierg76/golf-fm1, tag v1.0 = 8396679; BARNIR's wavetable synthesizer with a step
# sequencer: two morphing wavetable oscillators over 130 built-in tables, a ladder filter, two LFOs, a modulation
# matrix, an arpeggiator, effects; MPL-2.0, the app in eigen/). The repository has no fm1-base of its own (eigen/bouw.sh
# copies the app into an $FM1_BASE checkout): the glue pins one beside it, cores/golf-fm1-base = fm1-base @ 68fdaf8
# (fm1-base's main at Golf 1.0's tag, 2026-11-14: c8e0599, SY1's and MORSE's, plus uac_mix.h's MASTER slide), with its
# submodule bsp = felucca-bsp @ 18eadad. Both unmodified. core_golf.c follows core_sy1.c (the same fm1-base, the second
# core simulated, lib/touch.c's host part for APP_TOUCH) with the app's USB commands (APP_USB_CMD: the user wavetable
# bank, tools/golf_tabellen.py) on the service's coroutine. The engine is fm1.toml's "o2" unit (eigen/dsp/gf_engine.c),
# its own unit with PLAT_HOST as build.py's host build compiles it. The generate step is fm1-base's build.py generate()
# with the globals its main() sets from the app's directory (APP = eigen, CONF from eigen/fm1.toml, GEN =
# eigen/build/gen): the fonts (fm1-base's Barlow Semi Condensed) and the screen reader's words; one Python expression,
# run in eigen/ (GEN_ROOT) with the pinned fm1-base's tools on the path. Not gated.
set(_gf ${CMAKE_SOURCE_DIR}/cores/golf-fm1)
set(_gf_base ${CMAKE_SOURCE_DIR}/cores/golf-fm1-base)
set(_gf_ver 1.0)
# (the manager checks out the core's own submodule only: the configure checks out this one, with its bsp, when empty)
if(NOT EXISTS ${_gf_base}/device/plat_fm1.c OR NOT EXISTS ${_gf_base}/bsp/chip/ota.c)
    execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR} -c protocol.file.allow=always submodule update --init --recursive
                            cores/golf-fm1-base RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT EXISTS ${_gf_base}/device/plat_fm1.c OR NOT EXISTS ${_gf_base}/bsp/chip/ota.c)
        message(FATAL_ERROR "golf core: cores/golf-fm1-base (fm1-base @ 68fdaf8, with its bsp) is not checked out: run "
                            "git submodule update --init --recursive cores/golf-fm1-base (${_err})")
    endif()
endif()
# build.py --release 1.0 (LEARNINGS.md: "--release 1.0 geeft FM-1_28010000"): APP_VERSION the release; the package
# identity fm1.toml's id_prefix (FM-1_28) + the major, minor and patch as two digits each: FM-1_28010000
string(TOUPPER "${_gf_ver}" _gf_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_gf_ver}")
set(_gf_id "FM-1_28")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _gf_id "0")
    endif()
    string(APPEND _gf_id "${_d}")
endforeach()
set(_gf_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../../golf-fm1-base/tools') or __import__('build'))")
# fm1.toml o2, in its order
set(_gf_o2 dsp/gf_engine.c)
set(_gf_o2_rel "")
foreach(_f IN LISTS _gf_o2)
    list(APPEND _gf_o2_rel ../../cores/golf-fm1/eigen/${_f})
endforeach()
fm1_add_core(golf ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_gf} UNIT core_golf.c DESC core_golf_desc.c
             EXTRA_SOURCES ${_gf_o2_rel}
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "Golf by BARNIR (MPL-2.0). Wavetables: Golf's own, and resyntheses of CC0, CC BY, MIT (Mutable Instruments Tides and Plaits by Emilie Gillet, the Spherical Wavetable Navigator by Dan Green) and GPL-3.0 (Surge XT) sources: eigen's TABELLEN.md. Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h GEN_ROOT eigen GEN_PYTHON "${_gf_gen}" PYTHON_NEEDS raqm
             VERSION ${_gf_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_gf_app_ver}" FELUCCA_ID="${_gf_id}"
             INCLUDES ${_gf}/eigen ${_gf}/eigen/dsp ${_gf_base}/include ${_gf_base}/device ${_gf_base}/lib)
# the o2 unit's own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# build, its own directory; gf_engine.c puts its voice code in the device's .app_ram section on any compiler but the
# Mac's / Linux's / Windows' (GF_HOT): the section becomes an alignment, as fm1_add_core's FLAGS note has it for .pool
if(TARGET core_golf_obj)
    foreach(_f IN LISTS _gf_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/golf-fm1/eigen/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-Dsection(x)=aligned(4);-I${_gf}/eigen/dsp;-I${_gf_base}/include")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: Golf's MPL-2.0, its wavetables'
# sources and credits (TABELLEN.md: CC0, CC BY with the credits CC BY asks for, GPL-3.0, MIT), the GPL-3.0 text and the
# MIT notices those tables need (Golf's repository names a licences/ folder for them that it does not have:
# golf_third_party.txt and FM1VST's own GPL-3.0 text), fm1-base's MPL-2.0 with its notes and credits, felucca-bsp's
# MPL-2.0 with its notes, the Barlow OFL
# rd1: the page's knobs as drawn, live labels and readouts (core_golf.c, common/rd_cols.h)
fm1_add_rfm1(golf WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press ENV@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_TYPE_=_LOW_12 --expect-readout ctl_6_DRIVE_=_21
             AUTHORS "BARNIR"
             LICENSE_FILES "MPL-2.0 ({name}, LICENSE)=LICENSE"
                           "Wavetables: sources and credits ({name}, TABELLEN.md)=TABELLEN.md"
                           "Wavetables: MIT notices and the GPL-3.0 tables (golf_third_party.txt)=${CMAKE_CURRENT_LIST_DIR}/golf_third_party.txt"
                           "GPL-3.0 (the Surge XT wavetables)=${CMAKE_SOURCE_DIR}/LICENSE"
                           "MPL-2.0 (fm1-base, LICENSE)=${_gf_base}/LICENSE"
                           "fm1-base licensing (LICENSING.md)=${_gf_base}/LICENSING.md"
                           "fm1-base credits (CREDITS.md)=${_gf_base}/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, bsp/LICENSE)=${_gf_base}/bsp/LICENSE"
                           "felucca-bsp licensing (bsp/LICENSING.md)=${_gf_base}/bsp/LICENSING.md"
                           "felucca-bsp notice (bsp/NOTICE)=${_gf_base}/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=${_gf_base}/assets/fonts/Barlow-OFL.txt")
