# SPDX-License-Identifier: GPL-3.0-only
# A22 0.1.0 (https://github.com/abektes/a22, tag v0.1.0 = a8e6363; an FM workstation derived from h1 by Jake Handy:
# twelve synth and drum engines, eight FX, a four-track virtual tape, a mixer and the Riff/Shake sequencers: MIT, on
# fm1-base and felucca-bsp, MPL-2.0, its own submodules base = fm1-base @ c8e0599, base/bsp = felucca-bsp @ 18eadad):
# the fm1-base glue (core-glue/fm1base/README.md) over the unmodified submodule, from h1's glue (core_h1.cmake: the
# same fm1-base, the same app/ and core/ layout, the core's h1_* names kept). The whole instrument is the portable
# core in core/ (fm1.toml "o2": ../core/*.c, with o2_cflags -DH1_DEVICE=1), its own units, compiled from the submodule
# with PLAT_HOST as build.py's host and browser builds compile them; app/app.c only passes the panel in and the
# sound, the lights and the picture out. The generate step is fm1-base's build.py generate() with the globals its
# main() sets from the app's directory (APP, CONF from app/fm1.toml, GEN = app/build/gen): the platform's fonts
# (fm1-base's Barlow Semi Condensed, font_trim/font_pack) and the screen reader's words (app/words.tsv); one Python
# expression, run in app/ (GEN_ROOT). KIT's hits (core/plunk_data.c, h1's PLUNK pool) are CC0 VCSL recordings
# (docs/SAMPLES.md); WAVE's wavetables (core/wave_data.c) are made from harmonics by tools/gen_wavetables.py. Not gated.
set(_a22 ${CMAKE_SOURCE_DIR}/cores/a22)
set(_a22_ver 0.1.0)
# build.py --release 0.1.0 (the release workflow's version): APP_VERSION the release in capitals; the package identity
# fm1.toml's id_prefix (FM-1_4122) + the major, minor and patch as two digits each: FM-1_4122000100
string(TOUPPER "${_a22_ver}" _a22_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_a22_ver}")
set(_a22_id "FM-1_4122")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _a22_id "0")
    endif()
    string(APPEND _a22_id "${_d}")
endforeach()
set(_a22_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2 (../core/*.c), the glob in build.py's order (sorted)
set(_a22_o2 fx.c gfx.c h1.c inst_analog.c inst_chip.c inst_electric.c inst_fm4.c inst_haze.c inst_kit.c inst_mono.c
    inst_perc.c inst_shaper.c inst_stack.c inst_wave.c inst_wire.c plunk_data.c synth.c ui.c wave_data.c)
set(_a22_o2_rel "")
foreach(_f IN LISTS _a22_o2)
    list(APPEND _a22_o2_rel ../../cores/a22/core/${_f})
endforeach()
fm1_add_core(a22 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_a22} UNIT core_a22.c DESC core_a22_desc.c
             EXTRA_SOURCES ${_a22_o2_rel}
             FAMILY fm1base LICENSE MIT
             CREDITS "A22 by abektes (MIT), derived from h1 by Jake Handy (MIT). KIT's hits (PLUNK's pool): recordings from the Versilian Community Sample Library, CC0 (docs/SAMPLES.md). Built on fm1-base by Charles Vestal and felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments, MPL-2.0. Fonts: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1, and Terminus (Dimitar Zhekov), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h GEN_ROOT app GEN_PYTHON "${_a22_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_a22_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_a22_app_ver}" FELUCCA_ID="${_a22_id}"
             INCLUDES ${_a22}/app ${_a22}/base/include ${_a22}/base/device ${_a22}/base/lib)
# the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# and browser builds, fm1.toml's o2_cflags (-DH1_DEVICE=1), each file's own directory
if(TARGET core_a22_obj)
    foreach(_f IN LISTS _a22_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/a22/core/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-DH1_DEVICE=1;-I${_a22}/core;-I${_a22}/base/include")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: A22's MIT (h1's notice), the recordings' sources
# (docs/SAMPLES.md, CC0), fm1-base's and felucca-bsp's MPL-2.0 with their notes, the Barlow OFL and Terminus's licence
fm1_add_rfm1(a22 AUTHORS "A22 (abektes), from h1 by Jake Handy"
             LICENSE_FILES "MIT ({name}, LICENSE)=LICENSE"
                           "The recordings in {name} (docs/SAMPLES.md: CC0)=docs/SAMPLES.md"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt"
                           "Terminus font licence=base/assets/fonts/Terminus-LICENSE.txt")
