# SPDX-License-Identifier: GPL-3.0-only
# FRASIL 3.1 (https://github.com/styvesgoupil/frasil, tag v3.1 = 803e2fe; MPL-2.0): the fm1-base glue
# (core-glue/fm1base/README.md) over the unmodified submodule and its own submodules (base = fm1-base @ 92d6581,
# Styves Goupil's frasil branch, base/bsp = felucca-bsp @ d14ffd2), from Mutable Dreams' glue (the same fm1-base
# generation: chip/ota.c, backup.c). The engine (3.0: engine/, its DSP fm1.toml "o2": engine/dsp/*.c) is its own units (frasil_dsp_*.c).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (Barlow, Terminus: OFL), the screen reader's words and the
# fm1.toml gen lines, tools/gen_logo.py: the FRASIL logo (app/assets/frasil-logo.png -> frasil_logo.h, 4-bit, and
# frasil_saver.h, the screensaver's, 3.0), built
# unchanged; one Python expression, run in app/ (GEN_ROOT).
# GATED: LICENSING.md, "The FRASIL name and logo | (C) 2026 Styves Goupil: redistribute them unchanged, as part of
# FRASIL's source and packages" (the logo is compiled in). Hosted as .rfm1 despite the gate, as the author ships it,
# logo unchanged (the owner's call of 2026-11-01): not in the app's public build.
set(_fr ${CMAKE_SOURCE_DIR}/cores/frasil)
set(_fr_ver 3.1)
# build.py --release (./build.sh passes VERSION's): APP_VERSION the release in capitals; the package identity
# fm1.toml's id_prefix (FM-1_47) + the major, minor and patch as two digits each (3.0: FM-1_47030100, the hub's)
string(TOUPPER "${_fr_ver}" _fr_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_fr_ver}")
set(_fr_id "FM-1_47")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _fr_id "0")
    endif()
    string(APPEND _fr_id "${_d}")
endforeach()
set(_fr_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
fm1_add_core(frasil ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_fr} UNIT core_frasil.c DESC core_frasil_desc.c
             EXTRA_SOURCES frasil_dsp_frasil.c frasil_dsp_nuage.c frasil_dsp_glint.c frasil_dsp_bass.c
             FAMILY fm1base LICENSE MPL-2.0
             GATED "the FRASIL name and logo (compiled in) may be redistributed only unchanged, as part of FRASIL's source and packages (LICENSING.md)"
             CREDITS "FRASIL by Styves Goupil (MPL-2.0). The FRASIL name and logo (C) 2026 Styves Goupil, redistributed unchanged. Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Fonts: Barlow Semi Condensed (The Barlow Project Authors) and Terminus (Dimitar Toshkov Zhekov), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h frasil_logo.h frasil_saver.h GEN_ROOT app GEN_PYTHON "${_fr_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_fr_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_fr_app_ver}" FELUCCA_ID="${_fr_id}"
             INCLUDES ${_fr}/app ${_fr}/engine ${_fr}/engine/dsp ${_fr}/base/include ${_fr}/base/device ${_fr}/base/lib)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md; gated, built as a hosted file: HOSTED_DESPITE_GATE).
# The licence texts: its MPL-2.0 (fm1-base's and felucca-bsp's LICENSE are the same text), the three LICENSING.md
# notes (FRASIL's names the logo's terms), felucca-bsp's NOTICE, the fonts' OFL
fm1_add_rfm1(frasil AUTHORS "Styves Goupil"
             HOSTED_DESPITE_GATE "LICENSING.md lets the FRASIL name and logo be redistributed unchanged as part of FRASIL's source and packages: built as the author ships it, logo unchanged, hosted on RemoteFM1FW only, the owner's call of 2026-11-01"
             LICENSE_FILES "MPL-2.0 ({name}, fm1-base, felucca-bsp)=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "OFL-1.1 (the Barlow font in {name})=base/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=base/assets/fonts/Terminus-LICENSE.txt")
