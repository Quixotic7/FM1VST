/* SPDX-License-Identifier: GPL-3.0-only */
/* The Modular Koi core (https://github.com/bunglesociety/modular-koi, the pinned, unmodified submodule
 * cores/modular-koi with its Felucca-Engines submodule): Bungle Society's fork of Felucca 1.6.1 that turns the FM-1
 * into a steel tongue drum, the pond and its koi on the screen. Its browser unit, web/emu/felucca_web.c, is driven by
 * the family's glue (fw_core.h), built with the Koi board's flags (core_modularkoi.cmake: MODULAR_ONE, MODULAR_KOI,
 * FELUCCA_SLICE=0, as tools/build.py BOARD=koi), the second core off, the idle LED animation in. The ADPCM engine is
 * built off (core.h FELUCCA_ADPCM 0). No Tier 2 parameter map: the panel only, as the other forks.
 * Version and identity as BOARD=koi tools/build.py --release 0.1 builds the release: "KOI 0.1", FELUCCA_ID
 * "FM-1_901" (FM-1_9XY). */
#define FM1CORE_FW_P dt
#define FM1CORE_FW_WEB_C "../../cores/modular-koi/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/modular-koi/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/modular-koi/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_901"                                                     /* (build.py --release 0.1) */
/* The Koi controls, the tape's book and the pond (felucca.c includes them under MODULAR_KOI, before main.c; the
 * author's web/emu/felucca_web.c does not, its browser unit being Felucca's): main.c's calls declared here, the files
 * themselves included after the editor (core_modularkoi_ui.h, FM1CORE_FW_EDITOR_AFTER: its flash stand-ins). */
static void m1_init(void);
static void m1_boot(void);
static void m1_input(void);
static void m1_service(void);
static void m1_leds(void);
static void m1_draw(void);
#define FM1CORE_FW_EDITOR_AFTER "core_modularkoi_ui.h"
#define FM1CORE_FW_WEB_MS koi_web_ms                     /* main.c's MODULAR_ONE loop (core_modularkoi_ui.h) */
#include "fw_core.h"
