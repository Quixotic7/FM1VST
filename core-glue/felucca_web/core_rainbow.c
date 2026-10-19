/* SPDX-License-Identifier: GPL-3.0-only */
/* The Rainbow Mode core (https://github.com/joshbartlettband-web/felucca-rainbow, the pinned, unmodified submodule
 * cores/felucca-rainbow): a Felucca 1.1.5 fork whose browser unit, web/emu/felucca_web.c (Felucca 1.6's design,
 * back-ported, plus kid.c and its exports), is driven by the family's glue (fw_core.h). Rainbow mode (kid.c, a toy
 * for a small child) is on from power-up and takes over the web unit's frame (kid_frame: its own input, LEDs and
 * screen; HOME + SAVE held 3 s leaves it for the full Felucca until the next power-up); the glue changes nothing of
 * that. What differs from Felucca 1.6.1's unit: no idle LED animation (1.1.5), no second core. The SMK-37 board model
 * (BOARD_SMK37) is a separate build, not this core. */
#define FM1CORE_FW_P rb
#define FM1CORE_FW_WEB_C "../../cores/felucca-rainbow/web/emu/felucca_web.c"
#define FM1CORE_FW_EDITOR_C "../../cores/felucca-rainbow/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/felucca-rainbow/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_900"                                             /* (felucca.c) */
#include "fw_core.h"
