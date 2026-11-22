# SPDX-License-Identifier: GPL-3.0-only
# MORSE 0.8 (https://github.com/daviddwlee84/fm1-morse, tag v0.8 = 8467a08; Da-Wei Lee's pocket Morse code machine:
# a keyer and decoder on the keys, rhythm drills, flash cards, listening practice, Vail-format MIDI keying; GPL-3.0-or-
# later, its common/ (hostlink, ledfx) MIT, on fm1-base and felucca-bsp, MPL-2.0, its own submodules fm1-base =
# fm1-base @ c8e0599, fm1-base/bsp = felucca-bsp @ 18eadad): the fm1-base glue (core-glue/fm1base/README.md) over the
# unmodified submodule. The app is the repository's root (app.h, app.c and fm1.toml there, fm1-base in fm1-base/, not
# app/ and base/). Its fm1-base is SY1's (c8e0599): core_morse.c follows core_sy1.c without the second core (fm1.toml
# has no cpu2), with app_tick (APP_TICK, as core_amb.c) and the app's own SysEx. No o2 units (fm1.toml o2 = []).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from fm1.toml, GEN = build/gen): the fonts (fm1-base's Barlow Semi Condensed) and the screen reader's words
# (words.tsv); one Python expression, run in the repository's root. Not gated.
set(_mo ${CMAKE_SOURCE_DIR}/cores/fm1-morse)
set(_mo_ver 0.8)
# build.py --release 0.8: APP_VERSION the release; the package identity fm1.toml's id_prefix (FM-1_41) + the major,
# minor and patch as two digits each (fm1.toml: "release 0.3 FM-1_41000300"): FM-1_41000800
string(TOUPPER "${_mo_ver}" _mo_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_mo_ver}")
set(_mo_id "FM-1_41")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _mo_id "0")
    endif()
    string(APPEND _mo_id "${_d}")
endforeach()
set(_mo_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, 'fm1-base/tools') or __import__('build'))")
fm1_add_core(morse ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_mo} UNIT core_morse.c DESC core_morse_desc.c
             FAMILY fm1base LICENSE GPL-3.0-or-later
             CREDITS "MORSE by Da-Wei Lee (daviddwlee84), GPL-3.0-or-later, its common/ (hostlink, ledfx) MIT. Koch order from LCWO, MIDI keying in the Vail adapter's format. Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h GEN_PYTHON "${_mo_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_mo_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_mo_app_ver}" FELUCCA_ID="${_mo_id}"
             INCLUDES ${_mo} ${_mo}/common ${_mo}/fm1-base/include ${_mo}/fm1-base/device ${_mo}/fm1-base/lib)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: MORSE's GPL-3.0, fm1-base's
# MPL-2.0 with its notes and credits, felucca-bsp's MPL-2.0 with its notes, the Barlow OFL. RUN_ARGS: MORSE (0.7 on)
# starts on MENU (SETTINGS > START PAGE), PRACTICE chosen: PLAY opens it, where the keys are silent until a card asks;
# PLAY again starts the practice and the card's prompt sounds
fm1_add_rfm1(morse AUTHORS "Da-Wei Lee (daviddwlee84)"
             LICENSE_FILES "GPL-3.0 ({name}, LICENSE)=LICENSE"
                           "MPL-2.0 (fm1-base, fm1-base/LICENSE)=fm1-base/LICENSE"
                           "fm1-base licensing (fm1-base/LICENSING.md)=fm1-base/LICENSING.md"
                           "fm1-base credits (fm1-base/CREDITS.md)=fm1-base/CREDITS.md"
                           "MPL-2.0 (felucca-bsp, fm1-base/bsp/LICENSE)=fm1-base/bsp/LICENSE"
                           "felucca-bsp licensing (fm1-base/bsp/LICENSING.md)=fm1-base/bsp/LICENSING.md"
                           "felucca-bsp notice (fm1-base/bsp/NOTICE)=fm1-base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=fm1-base/assets/fonts/Barlow-OFL.txt"
             RUN_ARGS --press PLAY@1000 --press PLAY@1500)
