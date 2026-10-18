# SPDX-License-Identifier: GPL-3.0-only
# QUADRILLE (working name; the FMToneSeq repo), the step sequencer's Phase 1 prototype (its PLAN.md §13): a core
# built from the live FMToneSeq working tree, not a submodule, so a change there is one rebuild away. Its glue lives
# in FMToneSeq itself (core/glue: core_quadrille.c, core_quadrille_desc.c, conformance.txt); this file only points at
# it. QUADRILLE_DIR is the checkout (a cache path); without it the core is skipped. Gated: a prototype, not released.
set(QUADRILLE_DIR "/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/FMToneSeq" CACHE PATH
    "the FMToneSeq (QUADRILLE) checkout the quadrille core is built from")
if(NOT EXISTS ${QUADRILLE_DIR}/core/glue/core_quadrille.c)
    message(STATUS "quadrille core: skipped (no ${QUADRILLE_DIR}/core/glue/core_quadrille.c; set -DQUADRILLE_DIR=...)")
    return()
endif()
set(_qd ${QUADRILLE_DIR})
set(_qd_pvg ${_qd}/third_party/plutovg/source)
set(_qd_pvg_sources plutovg-blend.c plutovg-canvas.c plutovg-font.c plutovg-ft-math.c plutovg-ft-raster.c
                    plutovg-ft-stroker.c plutovg-matrix.c plutovg-paint.c plutovg-path.c plutovg-rasterize.c
                    plutovg-surface.c)
list(TRANSFORM _qd_pvg_sources PREPEND ../../third_party/plutovg/source/)   # (EXTRA_SOURCES: glue dir relative)
fm1_add_core(quadrille ${_qd}/core/glue SOURCE ${_qd}
             UNIT core_quadrille.c DESC core_quadrille_desc.c
             EXTRA_SOURCES ../app/app.c ../ui/ui_kit.c ../ui/ui_idle.c ../gfx/qd_gfx_plutovg.c ${_qd_pvg_sources}
             INCLUDES ${_qd}/core/app ${_qd}/core/ui ${_qd}/core/gfx ${_qd}/third_party/plutovg/include ${_qd_pvg}
             DEFINES PLUTOVG_BUILD PLUTOVG_BUILD_STATIC PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD
             FAMILY quadrille LICENSE GPL-3.0-only
             GATED "a prototype, not released (QUADRILLE Phase 1)"
             CREDITS "QUADRILLE (working name), a step sequencer for the FM-1: Phase 1 prototype.\nDrawn with plutovg (MIT). Portions of this software are copyright (c) 2006 The FreeType Project (www.freetype.org). All rights reserved.\nType: Inter Tight (SIL Open Font License 1.1)."
             CREDITS_FILES third_party/plutovg/LICENSE assets/fonts/OFL.txt
             NO_GEN
             VERSION 0.0.1-p1.0)
if(TARGET core_quadrille_obj)
    # plutovg is vendored unmodified: its own warnings are not ours
    set(_qd_pvg_abs ${_qd_pvg_sources})
    list(TRANSFORM _qd_pvg_abs PREPEND ${_qd}/core/glue/)
    set_source_files_properties(${_qd_pvg_abs} TARGET_DIRECTORY core_quadrille_obj PROPERTIES COMPILE_OPTIONS "-w")
endif()
