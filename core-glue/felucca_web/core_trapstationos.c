/* SPDX-License-Identifier: GPL-3.0-only */
/* The TRAPSTATION OS core (https://github.com/sog-maker/trapstation-os, the pinned, unmodified submodule
 * cores/trapstation-os): Schlensog Audio Lab's trap and phonk groovebox, a fork of BBL JAM BOX (blairysbeatlab), itself
 * a fork of Felucca. It ships Felucca's browser unit, web/emu/felucca_web.c (the firmware's felucca.c list with the
 * HAL replaced, its own device clock web_ms), so it is driven by the family's glue (fw_core.h) as GHOULBOX's and
 * Felucca 1.6.1's are; the scaffold's template (HAWK CHUA, a host build of its own) does not fit this tree.
 *   - the boot guard: main.c fm1_cstart's (a second failed boot in a row enters UBOOT), emulated by fw_core.h;
 *   - the idle LED animation: 1.2's (web_anim_idle), FM1CORE_FW_ANIM_IDLE;
 *   - no editor.c (TRAPSTATION's sound editor speaks its own SysEx through bk_usb.c / smp_usb.c, which the web unit
 *     builds out): the managers' SysEx is the M-UPGRADE identity only, ota.c with FELUCCA_ID "FM-1_511" (tools/build.py
 *     --release 1.1.0.2: PRODUCT "FM-1_5" X Y of the release, the identity of the published package);
 *   - flash_sync: TRAPSTATION's own LAST JAM and sound / kit slots (gb_store.c) instead of project.c's autosave
 *     (FM1CORE_FW_SYNC_MUSIC, below). */
#define FM1CORE_FW_P ts
#define FM1CORE_FW_WEB_C "../../cores/trapstation-os/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_OTA_C "../../cores/trapstation-os/firmware/src/ota.c"   /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_511"                                             /* (tools/build.py --release 1.1.0.2) */
static int fm1core_ts_sync_music(void);
#define FM1CORE_FW_SYNC_MUSIC() fm1core_ts_sync_music()
#include "fw_core.h"

/* flash_sync's music: TRAPSTATION has no project.c autosave; ui_input.c's loop writes the sound and kit slots
 * (gb_store_poll, one object a pass) and the LAST JAM (gb_store.c jam_poll: once nothing sounds, the screen has rested
 * JAM_REST_MS and a second has passed, a part a pass) when quiet. Here at once if quiet: the rest and the second
 * waived, every pending part written. 1: current; 0: something plays or sounds (autosave_quiet), or a write failed */
static int fm1core_ts_sync_music(void)
{
    uint32_t i;
    if (!autosave_quiet())
        return gb_dirty == 0;             /* (jam_poll waits for quiet too: deferred) */
    for (i = 0; i < 8u && gb_dirty; i++)
        gb_store_poll();
    for (i = 0; i < 4u; i++) {
        scrn.idle = fm1_ms - JAM_REST_MS;
        jam_q0 = (fm1_ms - 1000u) | 1u;
        if (!jam_pend)
            jam_t = 0;
        jam_poll();
        if (!jam_pend)
            break;
    }
    return !gb_dirty && !jam_pend;
}
