/* SPDX-License-Identifier: GPL-3.0-only */
/* The Golana core (https://github.com/Gadlele/Golana, the pinned, unmodified submodule cores/Golana): Gadlele's
 * single analog-style synth laid out like the microKORG (two oscillators, five filters incl. MS20, two EGs, two LFOs, a
 * patch bay, an arpeggiator and a 16-step sequencer, 32 programs: eng_golana.c, golana_ui.c, golana_prog.c ..), a fork
 * of Felucca by Leo Kuroshita (Hügelton Instruments). Its browser unit, web/emu/felucca_web.c (its "Try" page's), is
 * driven by the family's glue (fw_core.h). The idle LED animation is there (ANIM_IDLE).
 * Version and identity as tools/build.py --release 1.3 builds them (RELEASING.md; the release's Golana-1.3.fwsc):
 * FELUCCA_VERSION "v1.3" (core_golana.cmake, felucca.c's own default too), FELUCCA_ID FM-1_913 (release builds are
 * FM-1_9XY; felucca.c's FM-1_900 is the plain, non-release build's). */
#define FM1CORE_FW_P gn
#define FM1CORE_FW_WEB_C "../../cores/Golana/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_EDITOR_C "../../cores/Golana/firmware/src/editor.c"   /* the installer's INFO / backup SysEx */
#define FM1CORE_FW_OTA_C "../../cores/Golana/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_913"       /* build.py --release 1.3: FM-1_9 + X + Y */
#include "fw_core.h"
