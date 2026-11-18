# SPDX-License-Identifier: GPL-3.0-only
# LOAF BREAKER 0.9.68 (https://github.com/FLEXOR-R/loaf-breaker, tag loaf-v0.9.68 = f372ee0; FLEXOR's hardcore and
# breakcore break chopper, a fork of CHOPPAH by Laurens Profittlich: MPL-2.0, on fm1-base and felucca-bsp, MPL-2.0,
# its own submodules base = fm1-base @ 317cb86, base/bsp = felucca-bsp @ 5b0879b): the fm1-base glue
# (core-glue/fm1base/README.md) over the unmodified submodule, from Mighty Arp's glue (fm1-base b0976f2: 317cb86's
# device/ is the same file for file) with APP_TICK, APP_LOOP and the app's own SysEx (7D 43 48) as AMB-1's and SY1's.
# The engine (fm1.toml "o2": app/dsp/*.c, the glob expanded as build.py does: sorted) is its own units, with PLAT_HOST
# as build.py's host and browser builds compile them (no o2_cflags).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (font_trim) and the screen reader's words; one Python
# expression, run in app/ (GEN_ROOT). Then the break bank as ./build.sh makes it (its bank()): tools/prep_breaks.py over
# breaks/breaks.txt's lines marked *, into build/gen/bank_data.h (app.c's "bank_data.h", git-ignored upstream) instead
# of app/, so the submodule stays clean. The splash, the title and the face (app/splash_data.h ..) are committed.
#
# GATED, and the .rfm1 HOSTED_DESPITE_GATE (the owner's call, 2026-11-18): the bank built in has nine breaks. Six are
# FLEXOR's, sampled from commercial records (breaks/breaks.txt lines 24-26, 28, 29, 67: LOUDER, SATAN, PANTHER,
# FOCUZ, BANGPHS, NIGHT; breaks/README.md and breaks/flexor/README.md: "not royalty free", their rights with the
# artists and labels). The other three (lines 21-23: AMEN20, AMEN19, UPDOWN) are Dmitriy "Cyberworm" Vasilyev's Amen
# loops (rhythm-lab.com), offered royalty free (breaks/huge/info.txt, shipped below), themselves re-played or re-cut
# Amen breaks. The firmware has no build switch to leave the six out. breaks/classics/ (commercial records too) is
# not built in at this tag.
set(_lb ${CMAKE_SOURCE_DIR}/cores/loaf-breaker)
set(_lb_ver 0.9.68)
# the release's numbers (scripts/loaf_version.sh, .github/workflows/loaf.yml): V 0.9.68 is what people see
# (LOAF_VERSION, app/loaf_version.h upstream: the splash); the package's own PKG is A+2 . B . C = 2.9.68
# (build.sh --release 2.9.68: APP_VERSION "2.9.68", the identity fm1.toml's id_prefix FM-1_2623 + 02 09 68)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _m "${_lb_ver}")
math(EXPR _lb_pa "${CMAKE_MATCH_1} + 2")
set(_lb_pkg "${_lb_pa}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(_lb_id "FM-1_2623")
foreach(_d ${_lb_pa} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3})
    if(_d LESS 10)
        string(APPEND _lb_id "0")
    endif()
    string(APPEND _lb_id "${_d}")
endforeach()
set(_lb_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2 (dsp/*.c), its glob expanded in build.py's order
set(_lb_o2 dsp/adpcm.c dsp/brk.c dsp/brk_fx.c dsp/brk_math.c dsp/brk_synth.c dsp/kick.c dsp/ovl.c dsp/perc.c)
set(_lb_o2_rel "")
foreach(_f IN LISTS _lb_o2)
    list(APPEND _lb_o2_rel ../../cores/loaf-breaker/app/${_f})
endforeach()
fm1_add_core(loafbreaker ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_lb} UNIT core_loafbreaker.c DESC core_loafbreaker_desc.c
             EXTRA_SOURCES ${_lb_o2_rel}
             FAMILY fm1base LICENSE MPL-2.0
             GATED "compiles in six breaks sampled from commercial records (breaks/breaks.txt: LOUDER, SATAN, PANTHER, FOCUZ, BANGPHS, NIGHT, its README: not royalty free, rights with the artists and labels), no build switch to leave them out"
             CREDITS "LOAF BREAKER by FLEXOR, MPL-2.0, built on CHOPPAH by Laurens Profittlich, on fm1-base by Charles Vestal and felucca-bsp from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Breaks: six sampled by FLEXOR from records whose rights stay with their artists and labels, three Amen loops by Dmitriy Cyberworm Vasilyev (rhythm-lab.com, royalty free). Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h bank_data.h GEN_ROOT app GEN_PYTHON "${_lb_gen}" PYTHON_NEEDS raqm
             EXTRA_GEN "../tools/prep_breaks.py --list ../breaks/breaks.txt --break-slices 16 --transients --header build/gen/bank_data.h -o build/gen/bank.bin"
             SUBMODULES_RECURSIVE
             VERSION ${_lb_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_lb_pkg}" LOAF_VERSION="${_lb_ver}" FELUCCA_ID="${_lb_id}"
             INCLUDES ${_lb}/app ${_lb}/app/dsp ${_lb}/base/include ${_lb}/base/device ${_lb}/base/lib)
# the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host
# and browser builds, each file's own directory
if(TARGET core_loafbreaker_obj)
    foreach(_f IN LISTS _lb_o2)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/loaf-breaker/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-I${_lb}/app/dsp;-I${_lb}/app;-I${_lb}/base/include;-I${_lb}/base/lib")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: LOAF BREAKER's LICENSE (MPL-2.0),
# Cyberworm's terms for his Amen loops (breaks/huge/info.txt), fm1-base's and felucca-bsp's MPL-2.0 with their notes,
# the Barlow OFL. RUN_ARGS: a first start asks for the MODE (tutor.c TUT_ASK, BEGINNER marked), as on a fresh FM-1;
# PLAY at 3 s answers it (BEGINNER), and the chord then plays slices of the break
fm1_add_rfm1(loafbreaker AUTHORS "FLEXOR, Laurens Profittlich (CHOPPAH)"
             HOSTED_DESPITE_GATE "commercial record samples compiled in: built as the author ships it, hosted on RemoteFM1FW only, the owner's call of 2026-11-18"
             LICENSE_FILES "MPL-2.0 ({name}, LICENSE)=LICENSE"
                           "Cyberworm's Amen loops, royalty free (breaks/huge/info.txt)=breaks/huge/info.txt"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "fm1-base credits (base/CREDITS.md)=base/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt"
             RUN_ARGS --press PLAY@3000)
