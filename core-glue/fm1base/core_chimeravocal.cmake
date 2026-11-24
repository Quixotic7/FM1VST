# SPDX-License-Identifier: GPL-3.0-only
# FM-1 Chimera Vocal 0.9.0 (https://github.com/shan-sherif/fm-1-chimera-vocal, tag v0.9.0 = 7422270; Shan-Sherif's port
# of Jameson Hodge's CHIMERA vocal synthesizer, a physically modelled human voice: MPL-2.0 AND MIT, CHIMERA's port MIT,
# the CMUdict entries under CMU's BSD-style licence, fm1-base and felucca-bsp MPL-2.0, vendored in the repository
# under base/, no submodules): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified submodule, from
# SY1's glue (core_sy1.c: base/'s device, lib, include and felucca-bsp's chip are SY1's and LOOM-8's fm1-base, file
# for file; the second core simulated) with btdev.c (APP_BTDEV) as LOOM-8's and the app's own SysEx (7D 56 4F).
# The engine (fm1.toml "o2": engine/ch_*.c, CHIMERA's port, outside app/) is its own units, compiled from the
# submodule with fm1.toml's o2_cflags (-DCH_FM1: fm1-base's fastmath.h) and PLAT_HOST as build.py's host and browser
# builds compile them, each file's own directory first. The generate step is fm1-base's build.py generate() with the
# globals its main() sets from the app's directory (APP, CONF from app/fm1.toml, GEN = app/build/gen): the fonts
# (fm1-base's Barlow Semi Condensed) and the screen reader's words; one Python expression, run in app/ (GEN_ROOT).
# The dictionary (engine/ch_dict_data.h, app/dsp/words_default.h: CMUdict entries) is committed upstream and compiled
# in, with CMU's licence text; SCOWL's word list is the site's only (app/web), not built. Not gated.
set(_chv ${CMAKE_SOURCE_DIR}/cores/fm-1-chimera-vocal)
set(_chv_ver 0.9.0)
# build.py --release 0.9.0 (the release workflow's version, the tag's): APP_VERSION the release in capitals; the package
# identity fm1.toml's id_prefix (FM-1_042) + the major, minor and patch as two digits each: FM-1_042000900
string(TOUPPER "${_chv_ver}" _chv_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_chv_ver}")
set(_chv_id "FM-1_042")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _chv_id "0")
    endif()
    string(APPEND _chv_id "${_d}")
endforeach()
set(_chv_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, in its order (app/../engine/: the submodule's engine/)
set(_chv_o2 engine/ch_tract.c engine/ch_wave.c engine/ch_control.c engine/ch_speech.c engine/ch_live.c
    engine/ch_profile.c engine/ch_beat.c engine/ch_creature.c engine/ch_song.c)
set(_chv_o2_rel "")
foreach(_f IN LISTS _chv_o2)
    list(APPEND _chv_o2_rel ../../cores/fm-1-chimera-vocal/${_f})
endforeach()
fm1_add_core(chimeravocal ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_chv} UNIT core_chimeravocal.c DESC core_chimeravocal_desc.c
             EXTRA_SOURCES ${_chv_o2_rel}
             FAMILY fm1base LICENSE "MPL-2.0 AND MIT"
             CREDITS "FM-1 Chimera Vocal by Shan-Sherif, a port of CHIMERA by Jameson Hodge (MIT, github.com/jamesondh/chimera-vocal-synth), MPL-2.0 AND MIT, used with Jameson Hodge's permission for the name and the head. Pronunciations from the CMU Pronouncing Dictionary (Carnegie Mellon University, BSD-style licence). Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1. Unofficial: not affiliated with Jameson Hodge or M-VAVE."
             GEN ann_words.h felucca_font.h GEN_ROOT app GEN_PYTHON "${_chv_gen}" PYTHON_NEEDS raqm
             VERSION ${_chv_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_chv_app_ver}" FELUCCA_ID="${_chv_id}"
             INCLUDES ${_chv}/app ${_chv}/engine ${_chv}/app/dsp ${_chv}/base/include ${_chv}/base/device ${_chv}/base/lib)
# the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# and browser builds, fm1.toml's o2_cflags (-DCH_FM1), each file's own directory
if(TARGET core_chimeravocal_obj)
    foreach(_f IN LISTS _chv_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/fm-1-chimera-vocal/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-DCH_FM1;-I${_chv}/engine;-I${_chv}/app/dsp;-I${_chv}/base/include;-I${_chv}/base/lib")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: the repository's LICENSE (which
# licence covers which files), CHIMERA's MIT, the port's MIT, the MPL-2.0, CMUdict's licence, fm1-base's and
# felucca-bsp's MPL-2.0 with their notes, the Barlow OFL
# rd1: its knob row as drawn, live labels and readouts (core_chimeravocal.c, common/rd_cols.h)
fm1_add_rfm1(chimeravocal WANTS rd1
             READOUT_TEST --keys none --min-rms 0 --turn K1@4450:1 --turn K4@4750:1 --settle 4900 --seconds 0.15
                          --expect-readout ctl_3_VIBRATO_=_36 --expect-readout ctl_6_RING_=_2%
             AUTHORS "Shan-Sherif"
             LICENSE_FILES "{name} licensing (LICENSE)=LICENSE"
                           "MIT (CHIMERA, Jameson Hodge, LICENSES/MIT-CHIMERA.txt)=LICENSES/MIT-CHIMERA.txt"
                           "MIT ({name}, LICENSES/MIT.txt)=LICENSES/MIT.txt"
                           "MPL-2.0 ({name}, LICENSES/MPL-2.0.txt)=LICENSES/MPL-2.0.txt"
                           "CMU Pronouncing Dictionary (LICENSES/CMUdict.txt)=LICENSES/CMUdict.txt"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "fm1-base credits (base/CREDITS.md)=base/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt")
