# SPDX-License-Identifier: GPL-3.0-only
# ECHOFIELD FM-1 0.2.10 (https://github.com/Philip-Bos/echofield-fm1, tag v0.2.10 = 27feb2d; Philip Bos' granular
# sampler: MIT, its factory sounds CC0-1.0, on fm1-base and felucca-bsp, MPL-2.0, its own submodules base = fm1-base
# @ 4039a8b, base/bsp = felucca-bsp @ b0b2428): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified
# submodule, from FRASIL's glue (core_frasil.c: backup.c, the service's coroutine, lib/touch.c), with app.c compiled
# as fm1-base's browser emulator compiles it (PLAT_HOST: the factory sounds in an empty bank, no waits for the audio
# interrupt). The engine (fm1.toml "o2": app/dsp/engine.c) is its own unit (echofield_dsp_engine.c).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (Barlow Semi Condensed: OFL), the screen reader's words and
# the fm1.toml gen line, app/tools/gen_factory_h.py: the 14 factory sounds (companion/factory/*.efs, CC0) as
# factory_data.h; one Python expression, run in app/ (GEN_ROOT). Not gated.
set(_ef ${CMAKE_SOURCE_DIR}/cores/echofield-fm1)
# the version: CHANGELOG.md's first "## X.Y.Z" heading (the release's own, 0.2.8 at v0.2.8); build.py --release's
# APP_VERSION the release in capitals; the package identity fm1.toml's id_prefix (FM-1_3246) + the major, minor and
# patch as two digits each (0.2.8: FM-1_3246000210)
set(_ef_vre "^## ([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
file(STRINGS ${_ef}/CHANGELOG.md _ef_vl REGEX "${_ef_vre}")
set(_ef_ver 0)
if(_ef_vl)
    list(GET _ef_vl 0 _ef_vl)
    string(REGEX MATCH "${_ef_vre}" _m "${_ef_vl}")
    set(_ef_ver "${CMAKE_MATCH_1}")
endif()
string(TOUPPER "${_ef_ver}" _ef_app_ver)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _m "${_ef_ver}")
set(_ef_id "FM-1_3246")
foreach(_g 1 2 4)
    set(_d "${CMAKE_MATCH_${_g}}")
    if(_d STREQUAL "")
        set(_d 0)
    endif()
    if(_d LESS 10)
        string(APPEND _ef_id "0")
    endif()
    string(APPEND _ef_id "${_d}")
endforeach()
set(_ef_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
fm1_add_core(echofield ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_ef} UNIT core_echofield.c DESC core_echofield_desc.c
             EXTRA_SOURCES echofield_dsp_engine.c
             FAMILY fm1base LICENSE MIT
             CREDITS "ECHOFIELD FM-1 by Philip Bos (MIT), its 14 factory sounds by Philip Bos (CC0-1.0). Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h factory_data.h GEN_ROOT app GEN_PYTHON "${_ef_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION_FILE CHANGELOG.md VERSION_REGEX "${_ef_vre}"
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_ef_app_ver}" FELUCCA_ID="${_ef_id}"
             INCLUDES ${_ef}/app ${_ef}/app/dsp ${_ef}/base/include ${_ef}/base/device ${_ef}/base/lib)

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: ECHOFIELD's MIT and its
# LICENSING.md (which file has which licence), the factory sounds' CC0 note, fm1-base's and felucca-bsp's MPL-2.0 with
# their notes, the Barlow OFL
fm1_add_rfm1(echofield AUTHORS "Philip Bos"
             LICENSE_FILES "MIT ({name}, LICENSE)=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "CC0-1.0 (the factory sounds in {name})=${CMAKE_CURRENT_LIST_DIR}/echofield_factory_cc0.txt"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt")
