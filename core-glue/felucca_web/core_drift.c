/* SPDX-License-Identifier: GPL-3.0-only */
/* The DRIFT core (https://github.com/FBNFRTG/drift-fm1, the pinned, unmodified submodule cores/drift-fm1): Fabian
 * Freitag's ambient instrument, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): one sequencer track with a
 * repeating generative Euclidean sequencer, eight voices, two oscillators, three user sample slots, a monochrome UI
 * (drift_ui.c, drift_osc.c, drift_seq.c ..). Its browser unit, web/emu/felucca_web.c (its own webplayer's), is driven
 * by the family's glue (fw_core.h). The idle LED animation is there (ANIM_IDLE).
 * Version and identity as tools/build.py --release 0.9.1 builds them (drift-fm1-0.9.1.fwsc): FELUCCA_VERSION
 * "DRIFT 0.9.1" (felucca.c's own default, as web/emu/build.sh passes it), FELUCCA_ID FM-1_983 (build.py PRODUCT:
 * DRIFT's one fixed identity across releases). The sample uploader (webplayer/samples) asks INFO for
 * "FELUCCA DRIFT 0.9.1" before it writes a slot. */
#define FM1CORE_FW_P dr
#define FM1CORE_FW_WEB_C "../../cores/drift-fm1/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/drift-fm1/firmware/src/editor.c"   /* the sample uploader's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/drift-fm1/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_983"       /* build.py PRODUCT: DRIFT's fixed identity (README: identity FM-1_983) */
#include "fw_core.h"
