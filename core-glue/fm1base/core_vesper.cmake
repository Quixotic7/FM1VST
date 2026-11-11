# SPDX-License-Identifier: GPL-3.0-only
# VESPER 2.0.2 (https://github.com/fatcatstudios-hub/vesper-fm1, tag vesper-2.0.2 = faf036f; Fat Cat Studios' ambient
# worship synth: GPL-3.0-only with M-VAVE's FM1 SDK linking exception, on fm1-base and felucca-bsp, MPL-2.0, its own
# submodules base = fm1-base @ d8ff8f4, base/bsp = felucca-bsp @ 305c372): the fm1-base glue
# (core-glue/fm1base/README.md) over the unmodified submodule, from Mutable Dreams' glue (core_mutabledreams.c) without
# backup.c (fm1.toml has no backup). VESPER's code (app/vesper/: the screens, the scenes, the LUSH engine, the open FM1
# engine it plays through, the third-party DSP) is fm1.toml's "o2" units, compiled apart with its o2_cflags,
# o2_include (vesper/prefix.h: VESPER's heap) and include list, -DPLAT_HOST, as build.py's host and browser builds
# compile them. The proprietary FM1 SDK library (libfm1_engine.a) is not in the repository and nothing links it: the
# engine is FM1 Open's source (FM1_OPEN_WEB). The device-only parts stay out, as on the host: the IR pass
# (mul64_outline.py, a CPU erratum), vesper/libc (device_include), the .pool section (PLAT_HOST).
# The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's directory (APP,
# CONF from app/fm1.toml, GEN = app/build/gen): the fonts (fm1-base's Barlow Semi Condensed) and the screen reader's
# words; one Python expression, run in app/ (GEN_ROOT).
# Compiled in, and so in the licence texts: VESPER's piano, marimba and glockenspiel (VSCO-2 CE, CC0, SAMPLE-CREDITS.md),
# one voice of M-VAVE's V15 factory DX7 bank repeated (app/vesper/app/fm_factory.inc, Apache-2.0, THIRD_PARTY.md),
# Open303, DaisySP, stmlib, emu2149, SEA (MIT), MSFA / Dexed (Apache-2.0). Not gated.
set(_vs ${CMAKE_SOURCE_DIR}/cores/vesper-fm1)
set(_vs_ver 2.0.2)
# build.py --release 2.0.2 with tools/identity.patch (fm1.toml id_digits = 3): APP_VERSION "2.0.2", the identity
# id_prefix FM-1_8837 + 2 0 1 (README: "Identity: FM-1_8837201")
set(_vs_id FM-1_8837201)
set(_vs_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, its globs expanded in its order (build.py o2_files)
set(_vs_o2 "")
foreach(_g vesper/port.c vesper/libc.c vesper/app/*.c vesper/web/engine/*.c vesper/web/synth/*.c
           vesper/third_party/synth/*.c)
    file(GLOB _fs RELATIVE ${_vs}/app ${_vs}/app/${_g})
    list(SORT _fs)
    list(APPEND _vs_o2 ${_fs})
endforeach()
set(_vs_o2_rel "")
foreach(_f IN LISTS _vs_o2)
    list(APPEND _vs_o2_rel ../../cores/vesper-fm1/app/${_f})
endforeach()
fm1_add_core(vesper ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_vs} UNIT core_vesper.c DESC core_vesper_desc.c
             EXTRA_SOURCES ${_vs_o2_rel}
             FAMILY fm1base LICENSE GPL-3.0-only
             GATED "compiles in a voice (PIANO 1) of M-VAVE's V15 factory DX7 bank, from M-VAVE's recovery image: its Apache-2.0 grant is VESPER's reading, unverified"
             CREDITS "VESPER by Fat Cat Studios (GPL-3.0-only with the FM1 SDK linking exception). Its engine started from M-VAVE's FM1 Open (FM1 modifications (c) 2026 M-VAVE). Open303 by Robin Schmidt, DaisySP by Electrosmith (with Plaits models by Emilie Gillet), stmlib by Emilie Gillet, emu2149 by Mitsutaka Okazaki and SEA by Dani Biró (MIT). MSFA by Google Inc. and Dexed's msfa by Pascal Gauthier (Apache-2.0). A voice of M-VAVE's V15 factory DX7 bank (Apache-2.0). Piano, marimba and glockenspiel samples: Versilian Studios VSCO-2 Community Edition (CC0). Built on fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita (Hügelton Instruments), MPL-2.0. Font: Barlow Semi Condensed (The Barlow Project Authors), SIL OFL 1.1."
             GEN felucca_font.h ann_words.h GEN_ROOT app GEN_PYTHON "${_vs_gen}" PYTHON_NEEDS raqm
             SUBMODULES_RECURSIVE
             VERSION ${_vs_ver}
             FLAGS -ffp-contract=off
             DEFINES APP_VERSION="${_vs_ver}" FELUCCA_ID="${_vs_id}"
             INCLUDES ${_vs}/app ${_vs}/base/include ${_vs}/base/device ${_vs}/base/lib)
# the o2 units' own flags (native and, read by fm1_add_rfm1, the .rfm1's): build.py o2_objects, fm1.toml's o2_cflags,
# o2_include and include list in its order (two engine.h: web/engine's first), each file's own directory
if(TARGET core_vesper_obj)
    set(_vs_inc "")
    foreach(_d vesper/web/engine vesper/web/synth vesper/third_party/synth vesper/sdk/include vesper/app/synth vesper/app)
        list(APPEND _vs_inc "-I${_vs}/app/${_d}")
    endforeach()
    foreach(_f IN LISTS _vs_o2)
        get_filename_component(_d ${_vs}/app/${_f} DIRECTORY)
        set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../../cores/vesper-fm1/app/${_f} PROPERTIES
            COMPILE_OPTIONS "-std=gnu99;-w;-ffp-contract=off;-DPLAT_HOST;-DFM1_HOST_TEST=1;-DFM1_OPEN_WEB=1;-DVESPER_FMB=1;-fwrapv;-include;${_vs}/app/vesper/prefix.h;-I${_d};${_vs_inc}")
    endforeach()
endif()

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: VESPER's LICENSE.md (GPL-3.0 with
# the linking exception), THIRD_PARTY.md (the MIT and Apache-2.0 notices of what is compiled in), SAMPLE-CREDITS.md
# (the CC0 samples), fm1-base's and felucca-bsp's MPL-2.0 with their notes, the Barlow OFL. RUN_ARGS: the scene
# plays from PLAY, and VESPER loads scene 1 four seconds after power-on (screen.c, "launched"; input before then skips
# the start-up screen): PLAY at 5 s, the chord at 6 s, played over the running scene
fm1_add_rfm1(vesper AUTHORS "Fat Cat Studios"
             HOSTED_DESPITE_GATE "M-VAVE's V15 factory voice PIANO 1 is compiled in: built as the author ships it, hosted on RemoteFM1FW only, the owner call of 2026-11-02"
             LICENSE_FILES "GPL-3.0-only with the FM1 SDK linking exception ({name}, LICENSE.md)=LICENSE.md"
                           "Third-party notices ({name}, THIRD_PARTY.md)=THIRD_PARTY.md"
                           "Sample credits ({name}, SAMPLE-CREDITS.md: VSCO-2 CE, CC0)=SAMPLE-CREDITS.md"
                           "MPL-2.0 (fm1-base, base/LICENSE)=base/LICENSE"
                           "fm1-base licensing (base/LICENSING.md)=base/LICENSING.md"
                           "MPL-2.0 (felucca-bsp, base/bsp/LICENSE)=base/bsp/LICENSE"
                           "felucca-bsp licensing (base/bsp/LICENSING.md)=base/bsp/LICENSING.md"
                           "felucca-bsp notice (base/bsp/NOTICE)=base/bsp/NOTICE"
                           "SIL OFL 1.1 (Barlow)=base/assets/fonts/Barlow-OFL.txt"
             RUN_ARGS --press PLAY@5000+100 --settle 6000)
