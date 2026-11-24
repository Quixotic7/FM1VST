# SPDX-License-Identifier: GPL-3.0-only
# Tekstuur 2.0 (https://github.com/barnierg76/tekstuur-fm1, tag v2.0 = a9fa86c; BARNIR's four-layer texture instrument:
# MPL-2.0, its sources CC0 from VSCO-2 CE and VCSL): the fm1-base glue (core-glue/fm1base/README.md) over the
# unmodified submodule, from SY1's glue (core_sy1.c: the same fm1-base device code) with ECHOFIELD's APP_USB_CMD
# service (core_echofield.c). Its app is eigen/ (app.h, app.c, fm1.toml, dsp/); the repository has no fm1-base of its
# own (eigen/bouw.sh builds in an fm1-base checkout), so fm1-base is a second submodule, cores/tekstuur-fm1-base =
# charlesvestal/fm1-base @ 68fdaf8 (2026-11-13, the last commit before v2.0), with its own bsp = felucca-bsp @ 18eadad.
# The manager checks out the core's own submodule only: the configure checks out this one too when it is empty.
# The engine (fm1.toml "o2": dsp/tk_engine.c) is its own unit, compiled from the submodule with PLAT_HOST as build.py's
# host and browser builds compile it. The generate step is fm1-base's build.py generate() with the globals its main()
# sets from the app's directory (APP, CONF from eigen/fm1.toml, GEN = eigen/build/gen): the fonts (fm1-base's Barlow
# Semi Condensed) and the screen reader's words (eigen/words.tsv); one Python expression, run in eigen/ (GEN_ROOT).
# The nine built-in sources (dsp/tk_samples.h, committed) are CC0 recordings (eigen/BRONNEN.md). Not gated.
set(_tk ${CMAKE_SOURCE_DIR}/cores/tekstuur-fm1)
set(_tk_ver 2.0)
set(_tkb ${CMAKE_SOURCE_DIR}/cores/tekstuur-fm1-base)
if(NOT EXISTS ${_tkb}/tools/build.py OR NOT EXISTS ${_tkb}/bsp/chip/ota.c)
    execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR} -c protocol.file.allow=always submodule update --init --recursive
                            cores/tekstuur-fm1-base RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT EXISTS ${_tkb}/tools/build.py OR NOT EXISTS ${_tkb}/bsp/chip/ota.c)
        message(FATAL_ERROR "tekstuur core: ${_tkb} is empty: git submodule update --init --recursive "
                            "cores/tekstuur-fm1-base (${_err})")
    endif()
endif()
# build.py --release 2.0: APP_VERSION the release in capitals; the package identity fm1.toml's id_prefix (FM-1_27) +
# the major, minor and patch as two digits each: FM-1_27020000
string(TOUPPER "${_tk_ver}" _tk_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_tk_ver}")
set(_tk_id "FM-1_27")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _tk_id "0")
    endif()
    string(APPEND _tk_id "${_d}")
endforeach()
set(_tk_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../../tekstuur-fm1-base/tools') or __import__('build'))")
# fm1.toml o2
set(_tk_o2 dsp/tk_engine.c)
set(_tk_o2_rel "")
foreach(_f IN LISTS _tk_o2)
    list(APPEND _tk_o2_rel ../../cores/tekstuur-fm1/eigen/${_f})
endforeach()
fm1_add_core(tekstuur ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_tk} UNIT core_tekstuur.c DESC core_tekstuur_desc.c
             EXTRA_SOURCES ${_tk_o2_rel}
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "Tekstuur by BARNIR (MPL-2.0). Sources: VSCO-2 CE and VCSL by Versilian Studios (Sam Gossner), CC0 1.0. Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h GEN_ROOT eigen GEN_PYTHON "${_tk_gen}" PYTHON_NEEDS raqm
             VERSION ${_tk_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_tk_app_ver}" FELUCCA_ID="${_tk_id}"
             INCLUDES ${_tk}/eigen ${_tk}/eigen/dsp ${_tkb}/include ${_tkb}/device ${_tkb}/lib)
# the o2 unit's own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# and browser builds, the file's own directory
if(TARGET core_tekstuur_obj)
    foreach(_f IN LISTS _tk_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/tekstuur-fm1/eigen/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-I${_tk}/eigen/dsp;-I${_tkb}/include")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: Tekstuur's MPL-2.0, its sources'
# record (eigen/BRONNEN.md: CC0 1.0, file by file), fm1-base's and felucca-bsp's MPL-2.0 with their notes, the Barlow OFL
# rd1: the page's knobs as drawn, live labels and readouts (core_tekstuur.c, common/rd_cols.h)
fm1_add_rfm1(tekstuur WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press ENV@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_ATTACK_=_71 --expect-readout ctl_6_DELAY_=_21
             AUTHORS "BARNIR"
             LICENSE_FILES "MPL-2.0 ({name}, LICENSE)=LICENSE"
                           "CC0 1.0 (the sources in {name}, eigen/BRONNEN.md)=eigen/BRONNEN.md"
                           "MPL-2.0 (fm1-base, LICENSE)=${_tkb}/LICENSE"
                           "fm1-base licensing (LICENSING.md)=${_tkb}/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, bsp/LICENSE)=${_tkb}/bsp/LICENSE"
                           "felucca-bsp licensing (bsp/LICENSING.md)=${_tkb}/bsp/LICENSING.md"
                           "felucca-bsp notice (bsp/NOTICE)=${_tkb}/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=${_tkb}/assets/fonts/Barlow-OFL.txt")
