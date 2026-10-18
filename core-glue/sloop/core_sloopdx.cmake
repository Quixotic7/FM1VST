# SPDX-License-Identifier: GPL-3.0-only
# sloopDX 4.0 (https://github.com/zvenson/dxsloop): no 4.0 tag, the submodule is pinned at main eeec0a6 (the hub's
# catalog has no commit field: its 4.0 package was published with a439f1d, 2026-10-16 10:42 +0200, and eeec0a6 after
# it only adds a TODO.md line: the firmware tree is the same). The SLOOP-family glue (core-glue/sloop).
# GATED: it compiles assets/icons.png (gen_icons.py), the Hugelton icon atlas, all rights reserved (LICENSING.md: the
# Felucca section 7 permission, whose asset terms are not published yet). Un-gate: those terms, or replacement icons.
fm1_add_core(sloopdx ${CMAKE_CURRENT_LIST_DIR} SOURCE ${CMAKE_SOURCE_DIR}/cores/dxsloop UNIT core_sloopdx.c
             DESC core_sloopdx_desc.c FAMILY sloop LICENSE GPL-3.0-only
             GATED "the Hugelton icon atlas (assets/icons.png, all rights reserved, its GPL section 7 permission needs asset terms not yet published)"
             GEN felucca_font.h felucca_icons.h felucca_tables.h sloopdx_logo.h
             VERSION 4.0)
