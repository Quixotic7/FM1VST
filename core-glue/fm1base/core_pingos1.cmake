# SPDX-License-Identifier: GPL-3.0-only
# PING OS-1 1.0.1 (https://github.com/PINGAUDIO/PING-OS-1, tag v1.0.1 = f7d840a; PING AUDIO's four-track rhythm
# synthesis engine: 69 drum and synth models, 23 effects, a step sequencer; MPL-2.0, with Felucca's engines (MIT, the
# FM6 engine MPL-2.0 by grant, its msfa core Apache-2.0) and Mutable Instruments' Plaits / stmlib ports (MIT), on
# PING AUDIO's fork of fm1-base and felucca-bsp, MPL-2.0, submodules base and base/bsp): the fm1-base glue
# (core-glue/fm1base/README.md) over the unmodified submodule, from HADAL-1's glue (core_hadal1.cmake: the nearest
# fm1-base) with the app's USB commands (APP_USB_CMD: the sample bank, as core_golf.c) and the second core simulated
# (fm1.toml cpu2 = true). The engine (fm1.toml "o2": engine/ch_*.c, outside app/) is its own units, compiled from the
# submodule with fm1.toml's o2_cflags' -DCHISEL_FM1 (the device's sizes and sub-block) and PLAT_HOST, without the
# device's RAM code placement (-DCH_RAM_CODE, -mllvm ...).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (fm1-base's Barlow Semi Condensed), the screen reader's
# words (app/words.tsv) and the app's own generator (gen_wordmark.py: the splash's Jost wordmark, ping_font.h); one
# Python expression, run in app/ (GEN_ROOT), fm1-base's tools from ../base/tools. Not gated.
set(_pg ${CMAKE_SOURCE_DIR}/cores/PING-OS-1)
set(_pg_ver 1.0.1)
# build.py --release 1.0.1 (the release's ping-os-1-1.0.1.fwsc): APP_VERSION "1.0.1", the identity fm1.toml id_prefix
# FM-1_888 + 01 00 01
set(_pg_id FM-1_888010001)
set(_pg_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, in its order (app/../engine/: the submodule's engine/)
set(_pg_o2 engine/ch_core.c engine/ch_params.c engine/ch_mods.c engine/ch_material.c engine/ch_fx.c engine/ch_grain.c
    engine/ch_adpcm.c engine/ch_drum.c engine/ch_synth.c
    engine/ch_fe.c engine/ch_fe_tables.c engine/ch_fe_wheel.c engine/ch_fe_phase.c engine/ch_fe_trio.c
    engine/ch_fe_lofi.c engine/ch_fe_fm6.c engine/ch_fe_fm6core.c engine/ch_fe_fm6tab.c engine/ch_fe_fm6pat.c
    engine/ch_mi.c engine/ch_mi_tables.c)
set(_pg_o2_rel "")
foreach(_f IN LISTS _pg_o2)
    list(APPEND _pg_o2_rel ../../cores/PING-OS-1/${_f})
endforeach()
fm1_add_core(pingos1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_pg} UNIT core_pingos1.c DESC core_pingos1_desc.c
             EXTRA_SOURCES ${_pg_o2_rel}
             FAMILY fm1base LICENSE "MPL-2.0 AND MIT AND Apache-2.0"
             CREDITS "PING OS-1 by PING AUDIO (ping.audio), MPL-2.0. WHEEL, PHASE, TRIO, LOFI and FM6 from Felucca by Leo Kuroshita (Hügelton Instruments): MIT, the FM6 engine and patches MPL-2.0 by his grant, its core after msfa (Google, Pascal Gauthier, Apache-2.0). FOLD, FORMANT, CHORD, STRING and MODAL ported from Mutable Instruments' Plaits and stmlib by Emilie Gillet (MIT). Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Fonts: Barlow Semi Condensed (The Barlow Project Authors) and Jost (The Jost Project Authors), SIL OFL 1.1. See docs/THIRD_PARTY.md."
             GEN felucca_font.h ann_words.h ping_font.h GEN_ROOT app GEN_PYTHON "${_pg_gen}" PYTHON_NEEDS raqm
             VERSION ${_pg_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_pg_ver}" FELUCCA_ID="${_pg_id}"
             INCLUDES ${_pg}/app ${_pg}/engine ${_pg}/base/include ${_pg}/base/device ${_pg}/base/lib)
# the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# and browser builds, fm1.toml's include (../engine) and o2_cflags' -DCHISEL_FM1, each file's own directory
if(TARGET core_pingos1_obj)
    foreach(_f IN LISTS _pg_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/PING-OS-1/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-DCHISEL_FM1;-I${_pg}/engine;-I${_pg}/base/include;-I${_pg}/base/lib")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: PING OS-1's LICENSE (MPL-2.0) and
# its third-party notices (MIT, Apache-2.0 in full), fm1-base's and felucca-bsp's MPL-2.0 with their notes, the
# Barlow and Jost OFL. RUN_ARGS: a rhythm engine, silent until PLAY. The first press after the logo (4.3 s) is spent
# on the first power-on's card (HELP's hint: any touch ends it), the second starts the factory project (130 BPM)
fm1_add_rfm1(pingos1 AUTHORS "PING AUDIO"
             LICENSE_FILES "MPL-2.0 (PING OS-1, LICENSE)=LICENSE"
                           "Third-party code (docs/THIRD_PARTY.md: Felucca MIT / MPL-2.0 grant, msfa Apache-2.0, Mutable Instruments MIT)=docs/THIRD_PARTY.md"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "fm1-base credits (base/CREDITS.md)=base/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt"
                           "SIL OFL 1.1 (Jost)=app/assets/fonts/Jost-OFL.txt"
             RUN_ARGS --press PLAY@5000 --press PLAY@6000)
