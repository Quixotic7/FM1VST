# SPDX-License-Identifier: GPL-3.0-only
# MODELAR 0.3 (https://github.com/hirakos999/MODELAR, tag v0.3 = 89475ca; Hirakos' semi-modular drone synth played like
# a Kaoss Pad: MPL-2.0, on fm1-base and felucca-bsp, MPL-2.0, its own submodules base = fm1-base @ 865bc2f,
# base/bsp = felucca-bsp @ 33cac69): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified submodule,
# from VESPER's glue (core_vesper.c) without app_sysex. The sound (app/dsp/synth.c) is fm1.toml's "o2" unit, compiled
# apart with -DPLAT_HOST (its .pool section off), as build.py's host and browser builds compile it.
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (fm1-base's Barlow Semi Condensed) and the screen reader's
# words; one Python expression, run in app/ (GEN_ROOT). Nothing but MODELAR's own code, fm1-base, felucca-bsp and the
# Barlow font is compiled in. Not gated.
set(_md ${CMAKE_SOURCE_DIR}/cores/MODELAR)
set(_md_ver 0.3)
# build.py --release 0.3: APP_VERSION "0.3", the identity id_prefix FM-1_42 + 00 03 00 (README: FM-1_42000300)
set(_md_id FM-1_42000300)
set(_md_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
fm1_add_core(modelar ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_md} UNIT core_modelar.c DESC core_modelar_desc.c
             EXTRA_SOURCES ../../cores/MODELAR/app/dsp/synth.c
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "MODELAR by Hirakos (MPL-2.0). Built on fm1-base by Charles Vestal and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_md_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_md_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_md_ver}" FELUCCA_ID="${_md_id}"
             INCLUDES ${_md}/app ${_md}/app/dsp ${_md}/base/include ${_md}/base/device ${_md}/base/lib)
# the o2 unit's own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, -O2 and PLAT_HOST as
# its host and browser builds (fm1.toml has no o2_cflags / o2_include)
if(TARGET core_modelar_obj)
    set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/MODELAR/app/dsp/synth.c PROPERTIES
        COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-I${_md}/app/dsp;-I${_md}/base/include")
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: MODELAR's LICENSE (MPL-2.0),
# fm1-base's and felucca-bsp's MPL-2.0 with their notes, the Barlow OFL. RUN_ARGS: MODELAR starts with its four volumes
# at 0 (ui.c app_init: "silent at every power-on"), so the run turns K1..K4 up through the remote control
# (F0 7D 4C 43 03 role delta F7, tests/fixtures/modelar/volumes.syx: each knob +40 detents three times), as a player
# does before the first sound
fm1_add_rfm1(modelar AUTHORS "Hirakos"
             LICENSE_FILES "MPL-2.0 ({name}, LICENSE)=LICENSE"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt"
             RUN_ARGS --sysex ${CMAKE_SOURCE_DIR}/tests/fixtures/modelar/volumes.syx)
