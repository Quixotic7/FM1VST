# SPDX-License-Identifier: GPL-3.0-only
# Mutable Dreams 1.5 (https://github.com/kidbee85-beep/mutable-dreams, tag v1.5 = b9897cf; GPL-3.0-only as a combined
# distribution: the app MIT and GPL-3.0-only, Plaits / Rings / Elements / stmlib MIT (Emilie Gillet), fm1-base and
# felucca-bsp MPL-2.0, vendored in the repository, no submodules): the fm1-base glue (core-glue/fm1base/README.md)
# over the unmodified submodule. The C side (app/app.c with the platform) is the unit (core_mutabledreams.c); the
# engine is C++ (fm1.toml "o2": app/dsp/dreams.cpp and the twelve Mutable Instruments units), compiled from the
# submodule's own files as its build.py and tools/build_local.py compile them: C++11, no exceptions, no RTTI, no
# thread-safe statics, -DTEST (stmlib's portable paths), app/compat first (its freestanding <cmath>, <algorithm>,
# <new>, math.h on fastmath.h, as the device and its WASI browser build use them), app/third_party, each file's own
# directory. The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's
# directory (APP, CONF from app/fm1.toml, GEN = app/build/gen): the fonts (font_set "dreams": Barlow, Nunito) and the
# screen reader's words; one Python expression, run in app/ (GEN_ROOT). The presets (presets/*.json) are already
# compiled into app/dsp/preset_bank.inc and lofi_*.inc in the repository: nothing of presets/ is read or shipped.
set(_md ${CMAKE_SOURCE_DIR}/cores/mutable-dreams)
set(_md_ver 1.5)
# the release's identity and version (docs/release-1.5/Release-Notes-1.5.md: identity FM-1_710010500:
# build.py --release 1.5.0, fm1.toml id_prefix FM-1_710 + 01 05 00)
set(_md_app_ver 1.5.0)
set(_md_id FM-1_710010500)
set(_md_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, in its order
set(_md_o2 third_party/elements/dsp/exciter.cc third_party/elements/dsp/resonator.cc third_party/elements/resources.cc
    third_party/plaits/dsp/engine/additive_engine.cc dsp/dreams.cpp third_party/plaits/dsp/engine/fm_engine.cc
    third_party/plaits/dsp/engine/virtual_analog_engine.cc third_party/plaits/resources.cc
    third_party/rings/dsp/resonator.cc third_party/rings/resources.cc third_party/stmlib/dsp/units.cc
    third_party/stmlib/utils/random.cc)
set(_md_o2_rel "")
foreach(_f IN LISTS _md_o2)
    list(APPEND _md_o2_rel ../../cores/mutable-dreams/app/${_f})
endforeach()
fm1_add_core(mutabledreams ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_md} UNIT core_mutabledreams.c DESC core_mutabledreams_desc.c
             EXTRA_SOURCES ${_md_o2_rel}
             FAMILY fm1base LICENSE GPL-3.0-only
             CREDITS "Mutable Dreams by Kid Bunka / Christoph Link (Mutable Dreams contributors). Sound engines from Mutable Instruments' Plaits, Rings and Elements and stmlib by Emilie Gillet (MIT). Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Fonts: Barlow (The Barlow Project Authors) and Nunito (The Nunito Project Authors), SIL OFL 1.1. Inspired by Mutable Instruments, not affiliated with Mutable Instruments."
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_md_gen}" PYTHON_NEEDS raqm
             VERSION ${_md_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_md_app_ver}" FELUCCA_ID="${_md_id}"
             INCLUDES ${_md}/app ${_md}/app/dsp ${_md}/base/include ${_md}/base/device ${_md}/base/lib)
# the C++ units' own flags (native and, read by fm1_add_rfm1, the .rfm1's: after its C flags, so -std=c++11 wins);
# PLAT_HOST as fm1-base's host and browser builds give the o2 units (build.py o2_objects): dreams.cpp's POOL, the
# device's .pool section, off (Mach-O has none; nothing else in the engine reads it)
if(TARGET core_mutabledreams_obj)
    foreach(_f IN LISTS _md_o2)
        get_filename_component(_d ${_md}/app/${_f} DIRECTORY)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/mutable-dreams/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=c++11;-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-ffp-contract=off;-w;-DTEST;-DPLAT_HOST;-ffunction-sections;-fdata-sections;-I${_md}/app/compat;-I${_md}/app/third_party;-I${_d}")
    endforeach()
endif()
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: the app's GPL-3.0 and MIT grants
# and its licensing notice, Mutable Instruments' MIT notice (the third_party files' own header, as one text), fm1-base's
# and felucca-bsp's MPL-2.0 with their licensing notes, the fonts' OFL
fm1_add_rfm1(mutabledreams AUTHORS "Kid Bunka / Christoph Link"
             LICENSE_FILES "GPL-3.0-only (Mutable Dreams, LICENSE)=LICENSE"
                           "Mutable Dreams licensing notice (LICENSING-GPL.txt)=LICENSING-GPL.txt"
                           "MIT (Mutable Dreams, LICENSE-MIT.txt)=LICENSE-MIT.txt"
                           "MIT (Mutable Instruments: Plaits, Rings, Elements, stmlib)=${CMAKE_CURRENT_LIST_DIR}/mutabledreams_mi_mit.txt"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt"
                           "SIL OFL 1.1 (Nunito)=base/assets/fonts/Nunito-OFL.txt")
