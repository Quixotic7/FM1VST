# SPDX-License-Identifier: GPL-3.0-only
# DABKE 0.18 beta (https://github.com/wissamsader/dabke, tag v0.18-beta = b7ba61b; STUDIOMORPHIK's Arabic wedding
# keyboard: maqam scales with quarter tones, dabke and shaabi rhythms, synthesised reeds, strings and drums, eight
# tracks with loops and steps; MPL-2.0, all its sounds made by its own code, no samples; on fm1-base and felucca-bsp,
# MPL-2.0, its own submodule base = wissamsader/fm1-base @ a9423ea, base/bsp = felucca-bsp @ d18879b): the fm1-base
# glue (core-glue/fm1base/README.md) over the unmodified submodule. No stable release yet: the owner OKed the beta pin.
# Its fm1-base is Chimera Vocal's plus APP_UPDATE_SAVE: core_dabke.c follows core_chimeravocal.c without the second
# core (fm1.toml has no cpu2). The engine is fm1.toml's "o2" unit (app/dabke.c), compiled apart with -DPLAT_HOST (its
# .pool section off) as build.py's host and browser builds compile it.
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (fm1-base's Barlow Semi Condensed) and the screen reader's
# words (app/words.tsv); one Python expression, run in app/ (GEN_ROOT). Not gated.
set(_dab ${CMAKE_SOURCE_DIR}/cores/dabke)
set(_dab_ver 0.18)
# build.py --release 0.18: APP_VERSION the release in capitals; the package identity fm1.toml's id_prefix (FM-1_65)
# + the major, minor and patch as two digits each: FM-1_65001800
string(TOUPPER "${_dab_ver}" _dab_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_dab_ver}")
set(_dab_id "FM-1_65")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _dab_id "0")
    endif()
    string(APPEND _dab_id "${_d}")
endforeach()
set(_dab_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, in its order
set(_dab_o2 dabke.c)
set(_dab_o2_rel "")
foreach(_f IN LISTS _dab_o2)
    list(APPEND _dab_o2_rel ../../cores/dabke/app/${_f})
endforeach()
fm1_add_core(dabke ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_dab} UNIT core_dabke.c DESC core_dabke_desc.c
             EXTRA_SOURCES ${_dab_o2_rel}
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "DABKE by STUDIOMORPHIK (MPL-2.0). Built on fm1-base by Charles Vestal and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h GEN_ROOT app GEN_PYTHON "${_dab_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_dab_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_dab_app_ver}" FELUCCA_ID="${_dab_id}"
             INCLUDES ${_dab}/app ${_dab}/base/include ${_dab}/base/device ${_dab}/base/lib)
# the o2 unit's own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# and browser builds (PLAT_POOL: the device's .pool section off), its own directory
if(TARGET core_dabke_obj)
    foreach(_f IN LISTS _dab_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/dabke/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-I${_dab}/app;-I${_dab}/base/include;-I${_dab}/base/lib")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: DABKE's LICENSE (MPL-2.0), the Barlow
# OFL it ships, fm1-base's and felucca-bsp's MPL-2.0 with their notes. (JieLi's Apache-2.0 files are the FM-1 package's
# only: nothing of them is built here)
# rd1: its knob row as drawn, live labels and readouts (core_dabke.c, common/rd_cols.h)
fm1_add_rfm1(dabke WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press ENV@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_INSTRUMENT_=_ZURNA --expect-readout ctl_6_BUZZ_=_62
             AUTHORS "STUDIOMORPHIK (wissamsader)"
             LICENSE_FILES "MPL-2.0 ({name}, LICENSE)=LICENSE"
                           "SIL OFL 1.1 (Barlow, LICENSES/OFL-1.1-Barlow.txt)=LICENSES/OFL-1.1-Barlow.txt"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "fm1-base credits (base/CREDITS.md)=base/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE")
