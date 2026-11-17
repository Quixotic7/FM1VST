# SPDX-License-Identifier: GPL-3.0-only
# HADAL-1 1.0.1 (https://github.com/sparky480v/hadal-1, tag v1.0.1 = 7909c72; Polystellarflow's generative dark ambient
# firmware: drones, choirs, bells and 135 sound effects across 15 worlds, with melting pictures; MPL-2.0, on fm1-base
# and felucca-bsp, MPL-2.0, vendored in the repository under base/, no submodules): the fm1-base glue
# (core-glue/fm1base/README.md) over the unmodified submodule, from VESPER's glue (core_vesper.cmake, the nearest
# fm1-base) without backup.c (fm1.toml has no backup) and with the second core simulated (fm1.toml cpu2 = true: each
# job runs at once, in order, as fm1-base's host simulator runs it). HADAL-1's engine and pictures are fm1.toml's "o2"
# units (app/dsp/hadal.c, app/dsp/vis.c), compiled apart with -DPLAT_HOST (their .pool section off), as build.py's host
# and browser builds compile them.
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (fm1-base's Barlow Semi Condensed) and the screen reader's
# words (app/words.tsv); one Python expression, run in app/ (GEN_ROOT). Not gated.
set(_hd ${CMAKE_SOURCE_DIR}/cores/hadal-1)
set(_hd_ver 1.0.1)
# build.py --release 1.0.1 (the release's hadal-1.0.1.fwsc): APP_VERSION "1.0.1", the identity fm1.toml id_prefix
# FM-1_96 + 01 00 01
set(_hd_id FM-1_96010001)
set(_hd_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, in its order
set(_hd_o2 dsp/hadal.c dsp/vis.c)
set(_hd_o2_rel "")
foreach(_f IN LISTS _hd_o2)
    list(APPEND _hd_o2_rel ../../cores/hadal-1/app/${_f})
endforeach()
fm1_add_core(hadal1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_hd} UNIT core_hadal1.c DESC core_hadal1_desc.c
             EXTRA_SOURCES ${_hd_o2_rel}
             FAMILY fm1base LICENSE MPL-2.0
             CREDITS "HADAL-1 by Polystellarflow (MPL-2.0). Built on fm1-base and felucca-bsp, from Felucca by Leo Kuroshita (Hugelton Instruments), and on the design of AMB-1 by Charles Vestal. fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_hd_gen}" PYTHON_NEEDS raqm
             VERSION ${_hd_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_hd_ver}" FELUCCA_ID="${_hd_id}"
             INCLUDES ${_hd}/app ${_hd}/app/dsp ${_hd}/base/include ${_hd}/base/device ${_hd}/base/lib)
# the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, fm1.toml's include
# list (dsp), each file's own directory, PLAT_HOST (EN_POOL / POOLD: the device's .pool section off)
if(TARGET core_hadal1_obj)
    foreach(_f IN LISTS _hd_o2)
        get_filename_component(_d ${_hd}/app/${_f} DIRECTORY)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/hadal-1/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-I${_d};-I${_hd}/app/dsp")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: HADAL-1's LICENSE (MPL-2.0),
# fm1-base's and felucca-bsp's MPL-2.0 with their notes, the Barlow OFL. HADAL-1 plays by itself from power-on (a new
# world every start): no RUN_ARGS
fm1_add_rfm1(hadal1 AUTHORS "Polystellarflow"
             LICENSE_FILES "MPL-2.0 (HADAL-1, LICENSE)=LICENSE"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "fm1-base credits (base/CREDITS.md)=base/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt")
