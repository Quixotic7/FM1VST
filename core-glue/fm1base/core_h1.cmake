# SPDX-License-Identifier: GPL-3.0-only
# h1 0.3.0 (https://github.com/jakehandy/h1, tag v0.3.0 = 4cff936; Jake Handy's sixteen animated instruments with a
# loop recorder, a step sequencer, an arpeggiator and a metronome: MIT, on fm1-base and felucca-bsp, MPL-2.0, its own
# submodules base = fm1-base @ c8e0599, base/bsp = felucca-bsp @ 18eadad): the fm1-base glue
# (core-glue/fm1base/README.md) over the unmodified submodule, from SY1's glue (core_sy1.c: the same fm1-base, the
# second core simulated) without the app's own SysEx and without lib/touch.c (no APP_TOUCH). The whole instrument is
# the portable core in core/ (fm1.toml "o2": ../core/*.c, with o2_cflags -DH1_DEVICE=1), its own units, compiled from
# the submodule with PLAT_HOST as build.py's host and browser builds compile them; app/app.c only passes the panel in
# and the sound, the lights and the picture out. The generate step is fm1-base's build.py generate() with the globals
# its main() sets from the app's directory (APP, CONF from app/fm1.toml, GEN = app/build/gen): the platform's fonts
# (fm1-base's Barlow Semi Condensed) and the screen reader's words (app/words.tsv); one Python expression, run in app/
# (GEN_ROOT). BARD's and TWANG's recordings (core/smp_data.c, 56 cuts) and PLUNK's hits (core/plunk_data.c, 33 VCSL
# strokes) are CC0 (docs/SAMPLES.md). Not gated.
set(_h1 ${CMAKE_SOURCE_DIR}/cores/h1)
set(_h1_ver 0.3.0)
# build.py --release 0.3.0 (the release workflow's version): APP_VERSION the release in capitals; the package identity
# fm1.toml's id_prefix (FM-1_481) + the major, minor and patch as two digits each: FM-1_481000300
string(TOUPPER "${_h1_ver}" _h1_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_h1_ver}")
set(_h1_id "FM-1_481")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _h1_id "0")
    endif()
    string(APPEND _h1_id "${_d}")
endforeach()
set(_h1_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2 (../core/*.c), the glob in build.py's order (sorted)
set(_h1_o2 fx.c gfx.c h1.c inst_bard.c inst_bonk.c inst_cozy.c inst_haunt.c inst_ink.c inst_knot.c inst_koi.c
    inst_lava.c inst_orbit.c inst_pip.c inst_plunk.c inst_tide.c inst_tilt.c inst_turbo.c inst_twang.c inst_urchin.c
    plunk_data.c smp.c smp_data.c synth.c ui.c)
set(_h1_o2_rel "")
foreach(_f IN LISTS _h1_o2)
    list(APPEND _h1_o2_rel ../../cores/h1/core/${_f})
endforeach()
fm1_add_core(h1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_h1} UNIT core_h1.c DESC core_h1_desc.c
             EXTRA_SOURCES ${_h1_o2_rel}
             FAMILY fm1base LICENSE MIT
             CREDITS "h1 by Jake Handy (MIT). BARD's and TWANG's recordings and PLUNK's hits: Karoryfer Samples, Versilian VCSL, sfzinstruments and the Discord SFZ GM Bank, CC0 (docs/SAMPLES.md). Built on fm1-base by Charles Vestal and felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments, MPL-2.0. Fonts: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1, and Terminus (Dimitar Zhekov), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h GEN_ROOT app GEN_PYTHON "${_h1_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_h1_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_h1_app_ver}" FELUCCA_ID="${_h1_id}"
             INCLUDES ${_h1}/app ${_h1}/base/include ${_h1}/base/device ${_h1}/base/lib)
# the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# and browser builds, fm1.toml's o2_cflags (-DH1_DEVICE=1), each file's own directory
if(TARGET core_h1_obj)
    foreach(_f IN LISTS _h1_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/h1/core/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-DH1_DEVICE=1;-I${_h1}/core;-I${_h1}/base/include")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: h1's MIT, the recordings' sources
# (docs/SAMPLES.md, CC0), fm1-base's and felucca-bsp's MPL-2.0 with their notes, the Barlow OFL and Terminus's licence
fm1_add_rfm1(h1 AUTHORS "Jake Handy"
             LICENSE_FILES "MIT ({name}, LICENSE)=LICENSE"
                           "The recordings in {name} (docs/SAMPLES.md: CC0)=docs/SAMPLES.md"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt"
                           "Terminus font licence=base/assets/fonts/Terminus-LICENSE.txt")
