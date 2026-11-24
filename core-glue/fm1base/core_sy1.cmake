# SPDX-License-Identifier: GPL-3.0-only
# SY1 1.0.0 (https://github.com/fenderberg/fm1-sy1, tag v1.0.0 = abe0d0b; fenderberg's Synth1-compatible two-oscillator
# polysynth, bitimbral since 0.4: MIT, on fm1-base and felucca-bsp, MPL-2.0, its own submodules base = fm1-base @
# c8e0599, base/bsp = felucca-bsp @ 18eadad): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified
# submodule, from LOOM-8's glue (core_loom.c: the same fm1-base, the second core simulated) without backup.c and the
# resource region, with lib/touch.c (APP_TOUCH) as ECHOFIELD's. The engine (fm1.toml "o2": app/dsp/s1_synth.c, s1_fx.c,
# s1_patch.c, s1_tables.c) is its own units, compiled from the submodule with PLAT_HOST as build.py's host and browser
# builds compile them (fm1.toml has no o2_cflags / o2_include). The generate step is fm1-base's build.py generate()
# with the globals its main() sets from the app's directory (APP, CONF from app/fm1.toml, GEN = app/build/gen): the
# fonts (fm1-base's Barlow Semi Condensed) and the screen reader's words; one Python expression, run in app/
# (GEN_ROOT). The 500 factory presets (app/presets/factory500.h, a selection of community Synth1 banks under each
# patchmaker's own terms, all free to redistribute, some not to be sold: THIRD_PARTY.md, app/presets/CREDITS.txt) are
# committed upstream and compiled in, with their credits text (the app's SysEx 06 reads it back). Not gated.
set(_sy1 ${CMAKE_SOURCE_DIR}/cores/fm1-sy1)
set(_sy1_ver 1.0.0)
# build.py --release 1.0.0 (the release workflow's version): APP_VERSION the release in capitals; the package identity
# fm1.toml's id_prefix (FM-1_51) + the major, minor and patch as two digits each: FM-1_51010000
string(TOUPPER "${_sy1_ver}" _sy1_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_sy1_ver}")
set(_sy1_id "FM-1_51")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _sy1_id "0")
    endif()
    string(APPEND _sy1_id "${_d}")
endforeach()
set(_sy1_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, in its order
set(_sy1_o2 dsp/s1_synth.c dsp/s1_fx.c dsp/s1_patch.c dsp/s1_tables.c)
set(_sy1_o2_rel "")
foreach(_f IN LISTS _sy1_o2)
    list(APPEND _sy1_o2_rel ../../cores/fm1-sy1/app/${_f})
endforeach()
fm1_add_core(sy1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_sy1} UNIT core_sy1.c DESC core_sy1_desc.c
             EXTRA_SOURCES ${_sy1_o2_rel}
             FAMILY fm1base LICENSE MIT
             CREDITS "SY1 by fenderberg (MIT). The parameter laws are oxo_synth2's measurements of Synth1 (MIT, Francois Chabloz). Not affiliated with Daichi Laboratory. Factory 500: Synth1 community presets under their patchmakers' own terms (app/presets/CREDITS.txt), compilation credited to techno-addicts. Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h GEN_ROOT app GEN_PYTHON "${_sy1_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_sy1_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_sy1_app_ver}" FELUCCA_ID="${_sy1_id}"
             INCLUDES ${_sy1}/app ${_sy1}/app/dsp ${_sy1}/base/include ${_sy1}/base/device ${_sy1}/base/lib)
# the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# and browser builds, each file's own directory
if(TARGET core_sy1_obj)
    foreach(_f IN LISTS _sy1_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/fm1-sy1/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-I${_sy1}/app/dsp;-I${_sy1}/base/include")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: SY1's MIT, THIRD_PARTY.md
# (oxo_synth2's MIT text, the community banks' status), the Factory 500's credits and terms (app/presets/CREDITS.txt,
# with each bank's original readme: Lunatique's asks for its complete readme), fm1-base's and felucca-bsp's MPL-2.0
# with their notes, the Barlow OFL
# rd1: its knob row as drawn, live labels and readouts (core_sy1.c, common/rd_cols.h)
fm1_add_rfm1(sy1 WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --press ENV@4100 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_ATTACK_=_0.64_ms --expect-readout ctl_6_R.TAU_=_9.2_ms
             AUTHORS "fenderberg"
             LICENSE_FILES "MIT ({name}, LICENSE)=LICENSE"
                           "Third-party material ({name}, THIRD_PARTY.md: oxo_synth2 MIT)=THIRD_PARTY.md"
                           "Factory 500 presets: credits and terms (app/presets/CREDITS.txt)=app/presets/CREDITS.txt"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt")
