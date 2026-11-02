# SPDX-License-Identifier: GPL-3.0-only
# RemoteFM1 (https://github.com/Quixotic7/RemoteFM1, tag v0.3.1; GPL-3.0-only): scaffolded by RFM1Manager from mutabledreams's glue (core-glue/fm1base); review it
# Mutable Dreams 1.4 (https://github.com/Quixotic7/RemoteFM1, tag v1.4 = 0321551; GPL-3.0-only as a combined
# distribution: the app MIT and GPL-3.0-only, Plaits / Rings / Elements / stmlib MIT (Emilie Gillet), fm1-base and
# felucca-bsp MPL-2.0, vendored in the repository, no submodules): the fm1-base glue (core-glue/fm1base/README.md)
# over the unmodified submodule. The C side (app/app.c with the platform) is the unit (core_remotefm1.c); the
# engine is C++ (fm1.toml "o2": app/dsp/dreams.cpp and the twelve Mutable Instruments units), compiled from the
# submodule's own files as its build.py and tools/build_local.py compile them: C++11, no exceptions, no RTTI, no
# thread-safe statics, -DTEST (stmlib's portable paths), app/compat first (its freestanding <cmath>, <algorithm>,
# <new>, math.h on fastmath.h, as the device and its WASI browser build use them), app/third_party, each file's own
# directory. The generate step is fm1-base's build.py generate() with the globals its main() sets from the app's
# directory (APP, CONF from app/fm1.toml, GEN = app/build/gen): the fonts (font_set "dreams": Barlow, Nunito) and the
# screen reader's words; one Python expression, run in app/ (GEN_ROOT). The presets (presets/*.json) are already
# compiled into app/dsp/preset_bank.inc and lofi_*.inc in the repository: nothing of presets/ is read or shipped.
set(_md ${CMAKE_SOURCE_DIR}/cores/RemoteFM1)
set(_md_ver 1.4)
# the release's identity and version (DEVICE-TEST-REPORT-1.4.txt: Mutable-Dreams-1.4.0.fwsc, identity FM-1_710010400:
# build.py --release 1.4.0, fm1.toml id_prefix FM-1_710 + 01 04 00)
set(_md_app_ver 1.4.0)
set(_md_id FM-1_710010400)
set(_md_gen "(lambda b: (setattr(b, 'APP', b.Path('.').resolve()), setattr(b, 'CONF', b.read_conf(b.APP / 'fm1.toml')), setattr(b, 'GEN', b.APP / 'build' / 'gen'), b.generate()))(__import__('sys').path.insert(0, '../base/tools') or __import__('build'))")
# fm1.toml o2, in its order
set(_md_o2 third_party/elements/dsp/exciter.cc third_party/elements/dsp/resonator.cc third_party/elements/resources.cc
    third_party/plaits/dsp/engine/additive_engine.cc dsp/dreams.cpp third_party/plaits/dsp/engine/fm_engine.cc
    third_party/plaits/dsp/engine/virtual_analog_engine.cc third_party/plaits/resources.cc
    third_party/rings/dsp/resonator.cc third_party/rings/resources.cc third_party/stmlib/dsp/units.cc
    third_party/stmlib/utils/random.cc)
set(_md_o2_rel "")
foreach(_f IN LISTS _md_o2)
    list(APPEND _md_o2_rel ../../cores/RemoteFM1/app/${_f})
endforeach()
fm1_add_core(remotefm1 ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_md} UNIT core_remotefm1.c DESC core_remotefm1_desc.c
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

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: the app's GPL-3.0 and MIT grants
# and its licensing notice, Mutable Instruments' MIT notice (the third_party files' own header, as one text), fm1-base's
# and felucca-bsp's MPL-2.0 with their licensing notes, the fonts' OFL
fm1_add_rfm1(remotefm1 AUTHORS "Michael P Jones"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "MPL-2.0 ({name})=LICENSES/MPL-2.0.txt"
                           "OFL-1.1 (the Terminus font in {name})=assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the Barlow font in {name})=base/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=base/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (firmware/third_party/jersey10/OFL.txt in {name})=firmware/third_party/jersey10/OFL.txt"
                           "OFL-1.1 (the 1.1-Barlow in {name})=web/installer/firmware/LICENSES/OFL-1.1-Barlow.txt"
                           "OFL-1.1 (the 1.1-Terminus in {name})=web/installer/firmware/LICENSES/OFL-1.1-Terminus.txt"
                           "OFL-1.1 (the chakrapetch font in {name})=web/site/fonts/OFL-chakrapetch.txt"
                           "OFL-1.1 (the dmmono font in {name})=web/site/fonts/OFL-dmmono.txt"
                           "OFL-1.1 (the dmsans font in {name})=web/site/fonts/OFL-dmsans.txt"
                           "OFL-1.1 (the unbounded font in {name})=web/site/fonts/OFL-unbounded.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/core-glue/felucca_web/licences/minitone/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/core-glue/felucca_web/licences/minitone/OFL-InterTight.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/ChoralRootFM1/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/ChoralRootFM1/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/ChoralRootFM1/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/ChoralRootFM1/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/ChoralRootFM1/web/site/OFL.txt in {name})=web/vendor/FM1VST/cores/ChoralRootFM1/web/site/OFL.txt"
                           "OFL-1.1 (the DepartureMono in {name})=web/vendor/FM1VST/cores/CoralFlower/LICENSES/OFL-DepartureMono.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/CoralFlower/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/CoralFlower/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/CoralFlower/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/CoralFlower/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (the DepartureMono font in {name})=web/vendor/FM1VST/cores/CoralFlower/web/fonts/OFL-DepartureMono.txt"
                           "OFL-1.1 (the InterTight font in {name})=web/vendor/FM1VST/cores/CoralFlower/web/fonts/OFL-InterTight.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/DUELL/felucca-base/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/DUELL/felucca-base/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/DUELL/felucca-base/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/DUELL/felucca-base/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/DUELL/felucca-base/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/DUELL/felucca-base/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/DUELL/felucca-base/web/emu/fonts/OFL.txt"
                           "Apache-2.0 (the AC79-SDK in {name})=web/vendor/FM1VST/cores/Fart-Machine-One/LICENSES/AC79-SDK-LICENSE.txt"
                           "OFL-1.1 (the Barlow font in {name})=web/vendor/FM1VST/cores/Fart-Machine-One/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/Fart-Machine-One/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/Felucca/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/Felucca/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/Felucca/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/Felucca/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/Felucca/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/Felucca/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/Felucca/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the ChakraPetch in {name})=web/vendor/FM1VST/cores/JIANT/LICENSES/OFL-ChakraPetch.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/JIANT/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the IBMPlexMono in {name})=web/vendor/FM1VST/cores/JIANT/LICENSES/OFL-IBMPlexMono.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/JIANT/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (the Michroma in {name})=web/vendor/FM1VST/cores/JIANT/LICENSES/OFL-Michroma.txt"
                           "OFL-1.1 (the ChakraPetch font in {name})=web/vendor/FM1VST/cores/JIANT/android/stems/app/src/main/assets/fonts/OFL-ChakraPetch.txt"
                           "OFL-1.1 (the IBMPlexMono font in {name})=web/vendor/FM1VST/cores/JIANT/android/stems/app/src/main/assets/fonts/OFL-IBMPlexMono.txt"
                           "OFL-1.1 (the InterTight font in {name})=web/vendor/FM1VST/cores/JIANT/android/stems/app/src/main/assets/fonts/OFL-InterTight.txt"
                           "OFL-1.1 (the ChakraPetch font in {name})=web/vendor/FM1VST/cores/JIANT/assets/fonts/OFL-ChakraPetch.txt"
                           "OFL-1.1 (the IBMPlexMono font in {name})=web/vendor/FM1VST/cores/JIANT/assets/fonts/OFL-IBMPlexMono.txt"
                           "OFL-1.1 (the Michroma font in {name})=web/vendor/FM1VST/cores/JIANT/assets/fonts/OFL-Michroma.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/JIANT/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/JIANT/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/JIANT/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/JIANT/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/JIANT/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/Mini-Tone/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/Mini-Tone/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/Mini-Tone/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/Mini-Tone/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/Mini-Tone/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/PurpleMonkeyFM1/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/PurpleMonkeyFM1/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/PurpleMonkeyFM1/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/PurpleMonkeyFM1/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/PurpleMonkeyFM1/web/pm_site/OFL.txt in {name})=web/vendor/FM1VST/cores/PurpleMonkeyFM1/web/pm_site/OFL.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/PurpleMonkeyFM1/web/site/OFL.txt in {name})=web/vendor/FM1VST/cores/PurpleMonkeyFM1/web/site/OFL.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/Speak37/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/Speak37/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/Speak37/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/Speak37/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/Speak37/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/Speak37/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/Speak37/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/dxsloop/assets/fonts/Terminus-LICENSE.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/dxsloop/docs/webapp/editor/FUKIAI-LICENSE.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/dxsloop/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/felucca-rainbow/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/felucca-rainbow/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/felucca-rainbow/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/felucca-rainbow/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/felucca-rainbow/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/felucca-rainbow/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/felucca-rainbow/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/fm-106/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/fm-106/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/fm-106/emu/fonts/OFL.txt"
                           "OFL-1.1 (the Barlow font in {name})=web/vendor/FM1VST/cores/fm1-amb/base/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/fm1-amb/base/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the Barlow font in {name})=web/vendor/FM1VST/cores/fm1-fimba/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/fm1-fimba/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the Barlow font in {name})=web/vendor/FM1VST/cores/fm1-fomni/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/fm1-fomni/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the Barlow font in {name})=web/vendor/FM1VST/cores/fm1-loom/base/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/fm1-loom/base/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the Barlow font in {name})=web/vendor/FM1VST/cores/fm1-x0x/assets/fonts/Barlow-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/fm1-x0x/assets/fonts/Terminus-LICENSE.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/fm1-x0x/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/fmini-1/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/fmini-1/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/fmini-1/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/fmini-1/assets/fonts/OFL.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/fmini-1/docs/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/fmini-1/docs/emu/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/fmini-1/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/fmini-1/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/fmini-1/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the InterTight font in {name})=web/vendor/FM1VST/cores/frodhes/assets/fonts/OFL-InterTight.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/frodhes/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/frodhes/docs/model/research/sources/06-felucca/Felucca/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/frodhes/docs/model/research/sources/06-felucca/Felucca/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/frodhes/docs/model/research/sources/06-felucca/Felucca/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/frodhes/docs/model/research/sources/06-felucca/Felucca/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/frodhes/docs/model/research/sources/06-felucca/Felucca/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/frodhes/docs/model/research/sources/06-felucca/Felucca/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/frodhes/docs/model/research/sources/06-felucca/Felucca/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (the PirataOne in {name})=web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/LICENSES/OFL-PirataOne.txt"
                           "OFL-1.1 (the VT323 in {name})=web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/LICENSES/OFL-VT323.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/ghoulbox-fm1-dungeon-synth/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the PressStart2P in {name})=web/vendor/FM1VST/cores/grimm/LICENSES/OFL-PressStart2P.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/grimm/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/melodee/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (the Rubik in {name})=web/vendor/FM1VST/cores/melodee/LICENSES/OFL-Rubik.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/melodee/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/melodee/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/melodee/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/nomanskyFM/firmware/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/obervave/assets/fonts/Terminus-LICENSE.txt"
                           "Apache-2.0 (the PermanentMarker font in {name})=web/vendor/FM1VST/cores/obervave/video/fonts/LICENSE-PermanentMarker.txt"
                           "OFL-1.1 (the VT323 font in {name})=web/vendor/FM1VST/cores/obervave/video/fonts/OFL-VT323.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/orbit/assets/fonts/Terminus-LICENSE.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/orbit/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (the LittleCharacter in {name})=web/vendor/FM1VST/cores/pigeon/LICENSES/LittleCharacter-OFL.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/pigeon/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/pigeon/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (the LittleCharacter font in {name})=web/vendor/FM1VST/cores/pigeon/assets/fonts/LittleCharacter-OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/pigeon/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (the LittleCharacter font in {name})=web/vendor/FM1VST/cores/pigeon/web/pigeon/fonts/LittleCharacter-OFL.txt"
                           "OFL-1.1 (the BarlowCondensed in {name})=web/vendor/FM1VST/cores/skiff/LICENSES/OFL-BarlowCondensed.txt"
                           "OFL-1.1 (the DotGothic16 in {name})=web/vendor/FM1VST/cores/skiff/LICENSES/OFL-DotGothic16.txt"
                           "OFL-1.1 (the InterTight in {name})=web/vendor/FM1VST/cores/skiff/LICENSES/OFL-InterTight.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/skiff/assets/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/skiff/assets/fonts/OFL.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/skiff/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/skiff/web/emu/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/skiff/web/emu/fonts/OFL.txt"
                           "OFL-1.1 (the InterTight font in {name})=web/vendor/FM1VST/cores/skiff/web/fonts/InterTight-OFL.txt"
                           "OFL-1.1 (web/vendor/FM1VST/cores/skiff/web/fonts/OFL.txt in {name})=web/vendor/FM1VST/cores/skiff/web/fonts/OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/sloop-fm1/assets/fonts/Terminus-LICENSE.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/sloop-fm1/docs/webapp/editor/FUKIAI-LICENSE.txt"
                           "MIT (the FUKIAI in {name})=web/vendor/FM1VST/cores/sloop-fm1/web/FUKIAI-LICENSE.txt"
                           "OFL-1.1 (the AtkinsonHyperlegible font in {name})=web/vendor/FM1VST/cores/tonitor/assets/fonts/AtkinsonHyperlegible-OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/cores/tonitor/assets/fonts/Terminus-LICENSE.txt"
                           "OFL-1.1 (web/vendor/FM1VST/plugin/fonts/OFL.txt in {name})=web/vendor/FM1VST/plugin/fonts/OFL.txt"
                           "OFL-1.1 (the Terminus font in {name})=web/vendor/FM1VST/remote/RemoteFM1/assets/fonts/Terminus-LICENSE.txt")
