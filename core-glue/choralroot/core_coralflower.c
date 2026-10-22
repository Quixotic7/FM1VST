/* SPDX-License-Identifier: GPL-3.0-only */
/* The CoralFlower core (0.5): the firmware unit of CoralFlower's emulator, tools/emu/emu_fw.c (ChoralRoot 0.22's
 * emulator with CoralFlower's instrument: emu_firmware.h's include list, cf_*.c, flora/), compiled here as is from
 * the unmodified, pinned submodule, behind the family's shared glue (crf_glue.h).
 *
 * Its emu_hal_t is the 0.22 line's (tools/emu/emu_hooks.h): four LED planes (led, led_mid, led_dim, led_faint), PA2,
 * the strip / pump hooks; no key velocity or lean, no knob_quick, no keys_classic. It differs from ChoralRoot
 * 0.25.1's and from PurpleMonkey's; each stays private to its own unit (core-api/fm1core.h includes none of them).
 *
 * exit(): emu_fw.c's boot guard (BOOT_UBOOT: exit(3)) and the update gesture's leaving (OCT- + OCT+ + HOME held 5 s:
 * cf_shim.c cr_update_poll through emu_firmware.h's CRB_STUB_GO / CRB_UBOOT_GO, exit(0)), each a plain statement
 * followed by ordinary code: common/exit.h records it and halts the core (see core_choralroot.c). Its atexit handler
 * is empty. The environment: common/env.h (EMU_MASTER, EMU_STUB read as unset). The version: the tag's 0.5
 * (coralflower.c still says "CoralFlower 0.1"; tools/emu/build.sh takes web/releases.json's newest, at the tag still
 * 0.4.2); the splash here reads "CoralFlower 0.5". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FM1CORE_NO_EMU_NAMES
#include "fm1core.h"
#include "version.h"
#include "exit.h"
#include "env.h"                         /* getenv -> NULL (common/env.h) */
#include "nor_stdio.h"
#include "crf_sx_pre.h"                   /* the managers' SysEx: CRB_SEND on the glue's ring (crf_glue.h) */

#define FELUCCA_VERSION "CoralFlower " FM1CORE_VERSION
#include "../../cores/CoralFlower/tools/emu/emu_fw.c"

#include "nor_stdio_end.h"
#include "exit_end.h"
#include "panel.h"                        /* (after cf_fb.c's `#define fb ((uint8_t *)cv_px)`: panel.h has no `fb`) */

/* make the image current: what a backup's LIST writes first (cf_backup.c crb_list), now: the settings record
 * (cf_settings.c; the frame's settings_poll writes it CF_SET_QUICK_MS after a change), the sequencer's patterns and
 * song (cf_sequi.c sq_save_write; its poll waits for a quiet moment), each part's unsaved tweak to its owner's store
 * (cfk_flush: HOME's, or since 0.5 its pattern's) and the patterns' own sounds (cfps_flush, 0.5). Then, as cfk_poll
 * would later, each "not tweaked" a store still owes (cfk_st[].drop: its owner's sound changed). Not in SAFE MODE,
 * not before the power-on, not after a restore (cfs_restore_lock) or a halt. 1: current; 0: a write failed (the
 * frame's polls try again a second later) */
static int fm1core_cf_sync(void)
{
    uint32_t part, st;
    if (fm1core_halt || cr_safe || !cu.booted || cfs_restore_lock)
        return 1;
    if (cfs_fnv(&cs, sizeof cs) != cfs_saved && cf_settings_write(0) <= 0)
        return 0;
    if (sq_dirty) {
        sq_save_write();
        if (sq_dirty)
            return 0;
    }
    cfk_flush();
    cfps_flush();
    for (st = 0; st < CFK_NST; st++)
        if (cfk_st[st].drop) {                    /* (cfk_poll's "not tweaked" record) */
            memset(&cfk_rec, 0, sizeof cfk_rec);
            cfk_rec.magic = CFK_MAGIC;
            cfk_rec.part = cfk_st[st].part;
            cfk_rec.np = (uint8_t)P_COUNT;
            cfk_rec.check = cfs_fnv(&cfk_rec, (uint32_t)__builtin_offsetof(cfk_rec_t, check));
            if (cfk_write(st, &cfk_rec, 0u) != 0)
                return 0;
        }
    for (part = 0; part < 3u; part++) {
        int s;
        cfk_build(part, &cfk_rec);
        if (cfk_rec.edited && (s = cfk_store_for(part)) >= 0 && cfk_rec.check != cfk_st[s].saved)
            return 0;                             /* (no store free: the unit keeps it nowhere either) */
    }
    return cfps_check() == cfps_saved;
}

#define FM1CORE_CRF_P cf
#define FM1CORE_CRF_VEL 0
#define FM1CORE_CRF_LEVELS 4
#define FM1CORE_CRF_PA2 1
#define FM1CORE_CRF_FLASH_SYNC fm1core_cf_sync
#define FM1CORE_CRF_SX 1                  /* cf_backup.c: the installer's backup and restore */
#define FM1CORE_CRF_OTA_C "../../cores/CoralFlower/firmware/src/ota.c"
#define FELUCCA_ID "FM-1_930"             /* (coralflower.c: the package identity) */
#include "crf_glue.h"
