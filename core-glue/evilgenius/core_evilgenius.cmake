# SPDX-License-Identifier: GPL-3.0-only
# EVILGENIUS 0.1.0-beta.1 (https://github.com/Quixotic7/evilgenius, tag v0.1.0-beta.1 = 988a54e, a pre-release; MPL-2.0
# with the SID model BSD-2-Clause and SAM's C port unlicensed: its LICENSING.md), by Michael P Jones (Quixotic7): a
# six-track SID groovebox that talks. A derivative of FIZZ (the same core/ tree and the same FM1VST glue, kept in its
# own core/glue: core_quadrille.c, core_quadrille_desc.c), built here as RemoteFM1's tools/rfm1/build_fizz.sh builds
# FIZZ's .rfm1 (tools/rfm1/fizz/CMakeLists.txt there), from the unmodified submodule:
#
#   core_evilgenius.c       the unit: core_quadrille.c unchanged + the M-UPGRADE identity (felucca-bsp's chip/ota.c
#                           through common/sysex_ota.h, as fizz_unit.c), which the web manager finds the unit by
#   core_evilgenius_desc.c  core_quadrille_desc.c with the name EVILGENIUS and the public source_url
#   conformance.txt         FIZZ's, the same gestures (REC opens the keyboard; HOME + F3 for the flash round trip)
#
# The sources: tools/Makefile's CORE_SRC with GFX=plutovg (APP_SRC, UI_SRC, qd_gfx_plutovg.c, the sequencer,
# VOICE_SRC with SAM_SRC and say.c) and plutovg; every other .c in core/ is included by one of these. Its DEFS:
# SID_COMB_CONST and plutovg's. Nothing GPL is compiled: core/tests (the GPL test oracles in core/tests/ref) is
# neither built nor on the include path, and LICENSES/GPL-3.0.txt (theirs) is not carried. plutovg-ft-raster.c already
# has the Wasm overflow without setjmp (FIZZ's branch plutovg-wasm), so nothing is patched. NO_GEN: the fonts, the
# VOX words and the tables are committed headers.
#
# The identity (FELUCCA_ID): fm1-base's build.py --release, fm1.toml's id_prefix FM-1_45 + major, minor, patch as two
# digits each, checked against the repository's own release index, site/firmware/releases.json.
set(_eg ${CMAKE_SOURCE_DIR}/cores/evilgenius)
set(_eg_ver 0.1.0-beta.1)
set(_eg_id FM-1_45000100)
set(_eg_rel ../../cores/evilgenius)

if(NOT EXISTS ${_eg}/core/glue/core_quadrille.c OR NOT EXISTS ${_eg}/firmware/fm1-base/bsp/chip/ota.c)
    message(FATAL_ERROR "evilgenius core: ${_eg} is not checked out: git submodule update --init --recursive cores/evilgenius")
endif()
file(READ ${_eg}/site/firmware/releases.json _eg_rels)
string(JSON _eg_n LENGTH "${_eg_rels}" releases)
set(_eg_rel_id "")
math(EXPR _eg_last "${_eg_n} - 1")
foreach(_i RANGE ${_eg_last})
    string(JSON _v GET "${_eg_rels}" releases ${_i} version)
    if(_v STREQUAL _eg_ver)
        string(JSON _eg_rel_id GET "${_eg_rels}" releases ${_i} identity)
    endif()
endforeach()
if(NOT _eg_rel_id STREQUAL _eg_id)
    message(FATAL_ERROR "evilgenius core: ${_eg_ver}'s identity in site/firmware/releases.json is \"${_eg_rel_id}\", not "
                        "${_eg_id}: update core_evilgenius.cmake")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_eg}/site/firmware/releases.json)

set(_eg_pvg_sources plutovg-blend.c plutovg-canvas.c plutovg-font.c plutovg-ft-math.c plutovg-ft-raster.c
                    plutovg-ft-stroker.c plutovg-matrix.c plutovg-paint.c plutovg-path.c plutovg-rasterize.c
                    plutovg-surface.c)
file(GLOB _eg_pvg_found RELATIVE ${_eg}/third_party/plutovg/source ${_eg}/third_party/plutovg/source/*.c)
list(SORT _eg_pvg_found)
if(NOT _eg_pvg_found STREQUAL _eg_pvg_sources)
    message(FATAL_ERROR "evilgenius core: plutovg's sources changed (${_eg_pvg_found}): update core_evilgenius.cmake")
endif()
list(TRANSFORM _eg_pvg_sources PREPEND ${_eg_rel}/third_party/plutovg/source/)
set(_eg_sam_sources ${_eg_rel}/core/voice/sam/sam.c ${_eg_rel}/core/voice/sam/render.c ${_eg_rel}/core/voice/sam/reciter.c)
set(_eg_sources app/app.c app/ctl.c app/ctl_step.c app/ctl_rec.c app/ctl_leds.c app/ctl_latch.c app/ctl_preview.c
                app/view.c
                ui/px.c ui/ui_kit.c ui/ui_idle.c ui/ui_keys.c ui/ui_picker.c ui/ui_tracks.c ui/ui_lane.c ui/ui_held.c
                ui/ui_turn.c ui/ui_layer.c ui/ui_octave.c
                gfx/qd_gfx_plutovg.c
                seq/model.c seq/pattern.c seq/seq.c seq/player.c seq/demo.c seq/chord.c seq/scale.c seq/sounds.c
                seq/clip.c
                voice/quad/quad_voice.c voice/sid/sid.c voice/pool.c voice/mix.c voice/fx/qd_fx.c voice/sam/say.c)
list(TRANSFORM _eg_sources PREPEND ${_eg_rel}/core/)

fm1_add_core(evilgenius ${CMAKE_CURRENT_LIST_DIR}
             SOURCE ${_eg}
             UNIT core_evilgenius.c DESC core_evilgenius_desc.c
             EXTRA_SOURCES ${_eg_sources} ${_eg_sam_sources} ${_eg_pvg_sources}
             INCLUDES ${_eg}/core/glue ${_eg}/core ${_eg}/core/app ${_eg}/core/ui ${_eg}/core/gfx ${_eg}/core/seq
                      ${_eg}/core/voice ${_eg}/core/voice/quad ${_eg}/core/voice/sid ${_eg}/core/voice/fx
                      ${_eg}/core/voice/sam ${_eg}/third_party/plutovg/include ${_eg}/third_party/plutovg/source
                      ${_eg}/firmware/fm1-base/bsp/chip
             DEFINES SID_COMB_CONST PLUTOVG_BUILD PLUTOVG_BUILD_STATIC PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD
                     FELUCCA_ID="${_eg_id}"
             FAMILY evilgenius LICENSE MPL-2.0
             GATED "compiles in SAM (core/voice/sam: the 1982 Software Automatic Mouth in Sebastian Macke's C port, no licence, SoftVoice's rights) and the VOX words made with it (core/voice/sid/eg_vox_data.h)"
             CREDITS "EVILGENIUS, a SID groovebox that talks, for the FM-1, by Michael P Jones (Quixotic7). MPL-2.0, the source: https://github.com/Quixotic7/evilgenius (LICENSING.md says what is under which licence). Built on FIZZ, the owner's FM TONE step sequencer (MPL-2.0). FM TONE, the effects and the output chain from ChoralRoot FM-1 v0.25.1, the owner's, relicensed MPL-2.0, the parts that came from Felucca rewritten.\nThe SID model: from FM-1 SID by Joakim Eriksson (github.com/joakimeriksson/fm1-sid), BSD-2-Clause, Copyright (c) 2026 Joakim Eriksson.\nThe speech: SAM, the Software Automatic Mouth (Don't Ask Software, 1982, now SoftVoice, Inc.), as Sebastian Macke's C port (github.com/s-macke/SAM), which carries no licence: here by the owner's decision, outside EVILGENIUS's MPL-2.0 (core/voice/sam/README.md).\nDrawn with plutovg v1.3.3 (Samuel Ugochukwu, MIT), which carries stb_truetype, stb_image and stb_image_write (Sean Barrett, MIT or Unlicense). Portions of this software are copyright (c) 2006 The FreeType Project (www.freetype.org). All rights reserved.\nType: Inter Tight (The Inter Project Authors, SIL Open Font License 1.1).\nThe M-UPGRADE identity (the web manager finds the unit by it): felucca-bsp's chip/ota.c (Leo Kuroshita, Hügelton Instruments, MPL-2.0), as on the device, through FM1VST's sysex_ota.h."
             CREDITS_FILES ./LICENSE LICENSES/BSD-2-Clause.txt third_party/plutovg/LICENSE
                           third_party/plutovg/source/FTL.TXT assets/fonts/OFL.txt
             NO_GEN
             SUBMODULES_RECURSIVE
             VERSION 0.1.0-beta.1)
# plutovg and SAM are vendored: their own warnings are not ours (tools/Makefile builds them -w too)
set(_eg_quiet ${_eg_pvg_sources} ${_eg_sam_sources})
list(TRANSFORM _eg_quiet PREPEND ${CMAKE_CURRENT_LIST_DIR}/)
set_source_files_properties(${_eg_quiet} TARGET_DIRECTORY core_evilgenius_obj PROPERTIES COMPILE_OPTIONS "-w")

# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). 16 MiB, as FIZZ's. The licence texts: EVILGENIUS's
# MPL-2.0 and LICENSING.md, the SID model's BSD-2-Clause, SAM's README (its no-licence status), plutovg's MIT, FreeType's
# FTL, Inter Tight's OFL (fm1_add_rfm1 adds the glue's GPL-3.0 and the runtime's); not LICENSES/GPL-3.0.txt, which
# is the test oracles' (core/tests/ref), nothing in this file. No RUN_ARGS: rfm1_run's chord sounds as it is (a key
# in the step view previews track 1's sound, DRUMS: ZAP, about a second)
fm1_add_rfm1(evilgenius
             HOSTED_DESPITE_GATE "SAM (no licence) is compiled in: built as the author ships it, hosted on RemoteFM1FW only, the owner call of 2026-11-14"
             AUTHORS "Michael P Jones (Quixotic7)"
             MEMORY_MIB 16
             FLAGS -ffile-prefix-map=${CMAKE_SOURCE_DIR}=FM1VST
             LICENSE_FILES "MPL-2.0 ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "BSD-2-Clause (the SID model, from FM-1 SID)=LICENSES/BSD-2-Clause.txt"
                           "SAM (no licence: core/voice/sam/README.md)=core/voice/sam/README.md"
                           "MIT (plutovg)=third_party/plutovg/LICENSE"
                           "FTL (FreeType, in plutovg)=third_party/plutovg/source/FTL.TXT"
                           "OFL-1.1 (Inter Tight)=assets/fonts/OFL.txt")
