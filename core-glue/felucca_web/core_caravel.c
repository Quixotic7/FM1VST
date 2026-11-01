/* SPDX-License-Identifier: GPL-3.0-only */
/* The Felucca core (https://github.com/bonszaimusic/caravel, the pinned, unmodified submodule cores/caravel with its
 * engines submodule, firmware/engines): Felucca's own browser unit, web/emu/felucca_web.c, driven by the family's
 * glue (fw_core.h), plus its Tier 2 parameter map and knob targets (core_caravel_params.c, in this unit: it reaches
 * the firmware's statics). The core id stays "felucca" (saved plugin states and presets map to it), as the map's
 * entry names do.
 * Felucca 1.6.1's second core (FELUCCA_CPU2, MENU > SYSTEM > 2ND CORE) is built off by the web unit: one core
 * whatever the menu says (fx.c mix_block's C1_ON() is 0). */
#define FM1CORE_FW_P fe
#define FM1CORE_FW_WEB_C "../../cores/caravel/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/caravel/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/caravel/firmware/bsp/chip/ota.c"    /* the M-UPGRADE identity (felucca-bsp's, 1.8) */
#define FELUCCA_ID "FM-1_900"                                             /* (felucca.c) */
#include "fw_core.h"

/* ---- the Tier 2 parameter map (in this unit: it reaches the firmware's statics) ---- */
#include "core_caravel_params.c"
