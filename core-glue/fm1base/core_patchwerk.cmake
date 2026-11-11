# SPDX-License-Identifier: GPL-3.0-only
# PATCHWERK 1.0.3 (https://github.com/MattKuebrich/patchwerk, tag v1.0.3 = 6f91629; Matt Kuebrich's modular synth for
# the FM-1: GPL-3.0-only, on fm1-base and felucca-bsp, MPL-2.0, vendored in the repository under base/ with its
# patches/ already applied: no submodules): the fm1-base glue (core-glue/fm1base/README.md) over the unmodified
# submodule. The C side (app/app.c with the platform) is the unit (core_patchwerk.c); the synth (app/core: engine,
# modules, presets, the live link, the sample bank, the UI) is fm1.toml's "o2", "os" and "oz" units, compiled apart
# with o2_cflags (-DPW_FM1B) and -DPLAT_HOST, as build.py's host and browser builds compile them (build.py
# o2_objects): PW_POOL's .pool section off. The second core (fm1.toml asm cpu1/entry.S, APP_CPU1) is not built: the
# host's stand-ins in app.c (PLAT_HOST) say it never starts, as on the host simulator.
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts and the screen reader's words; one Python expression, run in
# app/ (GEN_ROOT). PATCHWERK's own faces (core/pw_fonts.h, Barlow Semi Condensed) are in the repository.
set(_pw ${CMAKE_SOURCE_DIR}/cores/patchwerk)
set(_pw_ver 1.0.3)
# build.py --release 1.0.3: APP_VERSION "1.0.3", the identity id_prefix FM-1_606 + 01 00 03 (app/fm1.toml: releases
# FM-1_606XXYYZZ)
set(_pw_id FM-1_606010003)
set(_pw_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, os, oz, in that order
set(_pw_units core/pw_engine.c core/pw_modules.c core/pw_presets.c core/pw_link.c core/pw_bank.c core/pw_ui.c)
set(_pw_units_rel "")
foreach(_f IN LISTS _pw_units)
    list(APPEND _pw_units_rel ../../cores/patchwerk/app/${_f})
endforeach()
fm1_add_core(patchwerk ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_pw} UNIT core_patchwerk.c DESC core_patchwerk_desc.c
             EXTRA_SOURCES ${_pw_units_rel}
             FAMILY fm1base LICENSE GPL-3.0-only
             CREDITS "PATCHWERK by Matt Kuebrich (GPL-3.0-only). The CZ module is a port of Melodee's CZ engine (GPL-3.0, Kerem Kilic) and MAME's uPD933 model (BSD-3-Clause, Devin Acker). The FM-DX module is a port of ymfm's OPZ model and tables (BSD-3-Clause, Aaron Giles). SEA codec by Dani Biro (MIT). Built on fm1-base by Charles Vestal and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN ann_words.h felucca_font.h GEN_ROOT app GEN_PYTHON "${_pw_gen}" PYTHON_NEEDS raqm
             VERSION ${_pw_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_pw_ver}" FELUCCA_ID="${_pw_id}"
             INCLUDES ${_pw}/app ${_pw}/app/core ${_pw}/base/include ${_pw}/base/device ${_pw}/base/lib)
# the units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, PLAT_HOST as its host and
# browser builds, fm1.toml o2_cflags (-DPW_FM1B), the app's include list (core/)
if(TARGET core_patchwerk_obj)
    foreach(_f IN LISTS _pw_units)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/patchwerk/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-DPW_FM1B;-I${_pw}/app/core;-I${_pw}/base/include")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: PATCHWERK's GPL-3.0, ymfm's
# BSD-3-Clause (the FM-DX module's port), fm1-base's and felucca-bsp's MPL-2.0 with their notes (fm1-base's Apache-2.0
# parts are its Bluetooth files: not built), the Barlow OFL; the uPD933 (MAME, BSD-3-Clause) and SEA (MIT) notices as one text
fm1_add_rfm1(patchwerk AUTHORS "Matt Kuebrich"
             LICENSE_FILES "GPL-3.0-only ({name}, LICENSE)=LICENSE"
                           "BSD-3-Clause (ymfm, assets/dx/LICENSE-ymfm.txt)=assets/dx/LICENSE-ymfm.txt"
                           "{name}'s ported code: MAME uPD933 (BSD-3-Clause), SEA codec (MIT)=${CMAKE_CURRENT_LIST_DIR}/patchwerk_notices.txt"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt")
