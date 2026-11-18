# SPDX-License-Identifier: GPL-3.0-only
# TRAPSTATION OS 1.1.0.2 (https://github.com/sog-maker/trapstation-os, tag v1.1.0.2 = 52353fa): Schlensog Audio Lab's
# trap and phonk groovebox, a fork of BBL JAM BOX (blairysbeatlab) on Felucca. The felucca_web family's core (fw_core.h
# over the firmware's own web/emu/felucca_web.c, unmodified; cmake/FM1Cores.cmake fm1_add_core), as GHOULBOX's.
# Generated as tools/build.py generate() makes them (trapstationos_gen.py: the same step): trap_fm6_factory.h (the FM6
# bank, assets/fm6), ui_fonts.h (BLB Pixel 7 Chunky), ui_fonts_gh.h (DejaVu Sans Mono Bold: the release's build reads
# it from the system, /usr/share/fonts/truetype/dejavu/ or WSL's /mnt/c/Windows/Fonts/, and the repository carries
# neither the font nor its licence; FM1VST carries both in licences/trapstationos/, and trapstationos_gen.py points
# the unmodified gen_gh_fonts.py at that copy), ui_approved.h / ui_final.h (the screens' artwork, assets/ui), ui_icons.h
# (Fukiai), ui_keycaps.h (Inter Tight), ui_palettes.h, felucca_tables.h. FELUCCA_VERSION "v1.1.0.2": build.py
# --release 1.1.0.2 (the tag's); its identity FM-1_511 is core_trapstationos.c's FELUCCA_ID. TRAP_FM6_TEST=1 and
# TRAP_UX_TEST=1: build.py main()'s defaults, every release build's (the FM6 engine and the final UI are on).
set(_ts ${CMAKE_SOURCE_DIR}/cores/trapstation-os)
set(_ts_ver 1.1.0.2)
set(_ts_lic ${CMAKE_CURRENT_LIST_DIR}/licences/trapstationos)
include(${CMAKE_CURRENT_LIST_DIR}/fw_common.cmake)
fm1_add_core(trapstationos ${CMAKE_CURRENT_LIST_DIR} SOURCE ${_ts}
             UNIT core_trapstationos.c DESC core_trapstationos_desc.c FAMILY felucca_web LICENSE GPL-3.0-only
             CREDITS "TRAPSTATION OS by Schlensog Audio Lab, a modified version of BBL JAM BOX by Blair (blairysbeatlab), itself a modified version of Felucca by Leo Kuroshita (Hügelton Instruments), GPL-3.0-only. The engine interface and DSP helpers: Felucca-Engines (Hügelton Instruments), MIT. The FM6 core: msfa from Dexed (Google Inc., Pascal Gauthier) in Leo Kuroshita's integer C port, Apache-2.0. Chord follow after Skiff (illuminus5), the second core after X0X (charlesvestal), Melodee (keremimo) and Jangada (zednaked), GPL-3.0. Fonts: BLB Pixel 7 Chunky (blairysbeatlab) and Inter Tight (The Inter Project Authors), SIL OFL 1.1, the Fukiai icons (Hügelton Instruments), MIT, DejaVu Sans Mono Bold (Bitstream Vera licence, DejaVu changes public domain). The TRAPSTATION name, logo and artwork are (C) 2026 Schlensog Audio Lab, all rights reserved. M-VAVE and FM-1 are trademarks of their owners: TRAPSTATION OS is not affiliated with them."
             GEN trap_fm6_factory.h ui_fonts.h ui_fonts_gh.h ui_approved.h ui_final.h ui_icons.h ui_keycaps.h
                 ui_palettes.h felucca_tables.h
             GEN_PYTHON "__import__('runpy').run_path('${CMAKE_CURRENT_LIST_DIR}/trapstationos_gen.py')"
             VERSION ${_ts_ver} DEFINES FELUCCA_VERSION="v${_ts_ver}" TRAP_FM6_TEST=1 TRAP_UX_TEST=1)
# the .rfm1 (cmake/FM1Rfm1.cmake: -DFM1_RFM1=ON; docs/RFM1.md). The licence texts: what is compiled in (LICENSING.md's
# table): the GPL, LICENSING.md (with its note on the TRAPSTATION artwork), CREDITS.md, Felucca-Engines' MIT, msfa's
# Apache-2.0, Fukiai's MIT, the OFL of BLB Pixel 7 Chunky and Inter Tight, DejaVu's licence (FM1VST's copy). Not the
# JieLi SDK's Apache-2.0 (device packages only), nor the texts LICENSING.md keeps for parts not in this build (DaisySP,
# Rings, DotGothic16, segno, Tcl/Tk, PyInstaller, Python: the web pages and the desktop app).
fm1_add_rfm1(trapstationos AUTHORS "Schlensog Audio Lab" "Blair (blairysbeatlab)" "Leo Kuroshita (Hügelton Instruments)"
             LICENSE_FILES "GPL-3.0-only ({name})=LICENSE"
                           "LICENSING.md ({name})=LICENSING.md"
                           "CREDITS.md ({name})=CREDITS.md"
                           "MIT (Felucca-Engines in {name})=LICENSES/MIT-Felucca-Engines.txt"
                           "Apache-2.0 (msfa, the FM6 core in {name})=LICENSES/Apache-2.0-msfa.txt"
                           "MIT (the Fukiai icons in {name})=LICENSES/MIT-Fukiai.txt"
                           "OFL-1.1 (BLB Pixel 7 Chunky in {name})=LICENSES/OFL-BLBPixel7.txt"
                           "OFL-1.1 (Inter Tight in {name})=LICENSES/OFL-InterTight.txt"
                           "Bitstream Vera / DejaVu (DejaVu Sans Mono Bold in {name})=${_ts_lic}/DejaVu-LICENSE.txt")
