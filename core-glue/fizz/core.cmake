# SPDX-License-Identifier: GPL-3.0-only
# FIZZ (the FMToneSeq repo, github.com/Quixotic7/FIZZ; QUADRILLE until P1.3k), the FM TONE step sequencer's Phase 1
# prototype (its PLAN.md §13): a core built from the live FMToneSeq working tree, not a submodule, so a change there is
# one rebuild away. Its glue lives in FMToneSeq itself (core/glue: core_quadrille.c, core_quadrille_desc.c,
# conformance.txt: the file names kept from QUADRILLE); this file only points at it. FIZZ_DIR is the checkout (a cache
# path); without it the core is skipped. Gated: a prototype, not released.
set(FIZZ_DIR "/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/FMToneSeq" CACHE PATH
    "the FMToneSeq (FIZZ) checkout the fizz core is built from")
if(NOT EXISTS ${FIZZ_DIR}/core/glue/core_quadrille.c)
    message(STATUS "fizz core: skipped (no ${FIZZ_DIR}/core/glue/core_quadrille.c; set -DFIZZ_DIR=...)")
    return()
endif()
set(_qd ${FIZZ_DIR})
set(_qd_pvg ${_qd}/third_party/plutovg/source)
set(_qd_pvg_sources plutovg-blend.c plutovg-canvas.c plutovg-font.c plutovg-ft-math.c plutovg-ft-raster.c
                    plutovg-ft-stroker.c plutovg-matrix.c plutovg-paint.c plutovg-path.c plutovg-rasterize.c
                    plutovg-surface.c)
list(TRANSFORM _qd_pvg_sources PREPEND ../../third_party/plutovg/source/)   # (EXTRA_SOURCES: glue dir relative)
# the sound (P1.1): FM TONE (core/voice/quad: ChoralRoot's eng_quad.c and what it reads, one unit, quad_voice.c), the
# voice pool, the mix, ChoralRoot's effects and output chain (core/voice/fx: one unit, qd_fx.c), the tracks (core/seq);
# the sequencer (P1.2: core/seq: the patterns and their editing, the engine, the player, the demo patterns; P1.3: the
# chords; P1.3b: the scales, the sound pool, copy / paste and undo)
set(_qd_voice_sources ../voice/quad/quad_voice.c ../voice/pool.c ../voice/mix.c ../voice/fx/qd_fx.c ../seq/model.c
                      ../seq/pattern.c ../seq/seq.c ../seq/player.c ../seq/demo.c ../seq/chord.c ../seq/scale.c
                      ../seq/sounds.c ../seq/clip.c)
# the app and its controller (P1.3, P1.3b, P1.3c, P1.3d: core/app/ctl*.c, view.c; P1.3d: ctl_latch, ctl_preview) and the screens (core/ui; P1.3c: ui_octave)
set(_qd_app_sources ../app/app.c ../app/ctl.c ../app/ctl_step.c ../app/ctl_rec.c ../app/ctl_leds.c ../app/ctl_latch.c
                    ../app/ctl_preview.c ../app/view.c)
set(_qd_ui_sources ../ui/ui_kit.c ../ui/ui_idle.c ../ui/ui_keys.c ../ui/ui_picker.c ../ui/ui_tracks.c ../ui/ui_lane.c
                   ../ui/ui_held.c ../ui/ui_turn.c ../ui/ui_layer.c ../ui/ui_octave.c)
fm1_add_core(fizz ${_qd}/core/glue SOURCE ${_qd}
             UNIT core_quadrille.c DESC core_quadrille_desc.c
             EXTRA_SOURCES ${_qd_app_sources} ${_qd_ui_sources}
                           ../gfx/qd_gfx_plutovg.c ${_qd_voice_sources} ${_qd_pvg_sources}
             INCLUDES ${_qd}/core/app ${_qd}/core/ui ${_qd}/core/gfx ${_qd}/core/seq ${_qd}/core/voice
                      ${_qd}/core/voice/quad ${_qd}/core/voice/fx ${_qd}/third_party/plutovg/include ${_qd_pvg}
             DEFINES PLUTOVG_BUILD PLUTOVG_BUILD_STATIC PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD
             FAMILY fizz LICENSE GPL-3.0-only
             GATED "a prototype, not released (FIZZ Phase 1)"
             CREDITS "FIZZ, an FM TONE step sequencer for the FM-1: Phase 1 prototype.\nFM TONE, the effects and the output chain: ChoralRoot FM-1 v0.25.1 (GPL-3.0-only, ChoralRoot FM-1 contributors), a fork of Felucca (Leo Kuroshita, Hugelton Instruments).\nDrawn with plutovg (MIT). Portions of this software are copyright (c) 2006 The FreeType Project (www.freetype.org). All rights reserved.\nType: Inter Tight (SIL Open Font License 1.1)."
             CREDITS_FILES third_party/plutovg/LICENSE assets/fonts/OFL.txt
             NO_GEN
             VERSION 0.5.1-p1.5d)
if(TARGET core_fizz_obj)
    # plutovg is vendored unmodified: its own warnings are not ours (ChoralRoot's copies keep theirs off themselves:
    # quad_voice.c and qd_fx.c push their diagnostics around them)
    set(_qd_pvg_abs ${_qd_pvg_sources})
    list(TRANSFORM _qd_pvg_abs PREPEND ${_qd}/core/glue/)
    set_source_files_properties(${_qd_pvg_abs} TARGET_DIRECTORY core_fizz_obj PROPERTIES COMPILE_OPTIONS "-w")
endif()
