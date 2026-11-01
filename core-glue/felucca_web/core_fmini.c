/* SPDX-License-Identifier: GPL-3.0-only */
/* The FMini-1 core (https://github.com/ruppert/fmini-1, the pinned, unmodified submodule cores/fmini-1 with its
 * Felucca-Engines submodule, firmware/engines): a three-oscillator monosynth on Felucca 1.6.1 (FELUCCA_FMINI_ONLY:
 * fmini_app.c's screen and controls), whose browser unit, web/emu/felucca_web.c, is driven by the family's glue
 * (fw_core.h). What differs from Felucca 1.6.1's unit:
 *   - its 3 s splash (fa_splash): main.c's loop starts 3030 ms after power-on (web_boot_ms), so a key sounds after it
 *     (conformance_fmini.txt settle_ms);
 *   - the UI is fmini_app.c's (UI_INPUT / UI_LEDS / UI_DRAW), track 1 the instrument (fa_init);
 *   - the device's second core (FELUCCA_CPU2) is built off by the web unit, as Felucca's.
 * Version and identity as its releases build them (since 1.14 a plain ./build.sh, no --release: build.py's --release
 * takes one digit per part): FELUCCA_VERSION felucca.c's default "v1.6.1" (core_fmini.cmake), FELUCCA_ID build.py's
 * PRODUCT FM-1_900; its screen shows core.h's FMINI_VERSION. */
#define FM1CORE_FW_P fm
#define FM1CORE_FW_WEB_C "../../cores/fmini-1/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/fmini-1/firmware/src/editor.c"   /* the web editors' SysEx */
#define FM1CORE_FW_OTA_C "../../cores/fmini-1/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_900"                                             /* (build.py's PRODUCT, ./build.sh) */
#include "fw_core.h"
