/* SPDX-License-Identifier: GPL-3.0-only */
/* The Rhythmer core (https://github.com/JazzyRhythmer/Rhythmer, the pinned, unmodified submodule cores/Rhythmer): Jazzy
 * Rhythmer's six-track groovebox, a fork of JIANT FM (its jiant/ tree: JIANT's platform, sequencer, songs and UI, with
 * X0X's 303 and TB-3PO, Amenity's break engine, Garden-FM's SERUM, MONO2K's machines and Hortator's MD drums). Its tree
 * has no browser unit of its own (web/emu/felucca_web.c: its site runs the package in fm1-emu instead), so the
 * family's glue (fw_core.h) drives rhythmer_web.c here: JIANT's own unit from the JIANT commit Rhythmer forked from,
 * pointed at cores/Rhythmer/jiant, with main.c's Rhythmer additions (see that file). As JIANT's core:
 *   - the session's random macros (session_dice) take the web unit's seed, 1, always (FM1CORE_RHYTHMER_SEED): a run
 *     is deterministic (no host clock is read in a core);
 *   - the fifth LED plane, fm1_led_hi (a brighter steady level than mid);
 *   - MENU > RESTORE LAST is OFF by default (PREF_RESTORE_ON): no autosave until it is turned on;
 *   - six parts (NPART 6); the dual-core render is built off (FELUCCA_DUAL_CORE 0).
 * The break banks (A / B, 0xA0000 and 0xB4000 of the flash, six_slots.c): the device package programs two prepared
 * recordings there (assets/breaks: "Amen, Brother" by The Winstons and "Think (About It)" by Lyn Collins, PCM12) with
 * the RHYBRK1 marker at 0x93000 (tools/fm1pkg_make.py with_factory_banks). The core carries them as the author ships
 * them (the core is GATED, its .rfm1 hosted only: core_rhythmer.cmake): before the boot, a bank whose 80 KiB are all
 * erased (a fresh flash, as a freshly installed unit has them) gets the package's bytes (rhythmer_breaks.h, made by
 * rhythmer_breaks.py at build time from the submodule's slots), the marker likewise; a bank that holds anything else
 * (the user's own upload, six_upload.c over the managers' SysEx) is left as it is. So an upload stopped after its
 * erase gets the factory break back at the next power-on.
 * The six units (bass303, jungle, tb3po, six303_bridge) are their own units (rhythmer_<name>.c), as tools/build.py
 * builds them. Identity FM-1_748001 (build.sh --product); version string "Rhythmer dev", felucca.c's default, which
 * the release package (tag v0.1.0-beta.2, built by build.sh without --release) carries. */
#ifndef FM1CORE_RHYTHMER_SEED
#define FM1CORE_RHYTHMER_SEED 1u
#endif
#define FELUCCA_VERSION "Rhythmer dev"     /* firmware/src/felucca.c's (the package's splash, MENU > ABOUT, editor) */
#define JUNGLE_AUDIO_CODE                  /* felucca.c: the break engine's header without a section */
#define FM1CORE_FW_P rhy
#define FM1CORE_FW_WEB_C "rhythmer_web.c"
static void rhy_breaks_preload(void);
#define FM1CORE_FW_BEFORE_BOOT() (rhy_breaks_preload(), web_seed(FM1CORE_RHYTHMER_SEED))
#define FM1CORE_FW_RESTORE_ON() ((ui_prefs & PREF_RESTORE_ON) != 0)
#define FM1CORE_RHYTHMER_HI_LEVEL 160u     /* fm1_led_hi: between MID and lit, as JIANT's core */
#define FM1CORE_FW_LED(c, r, lv)                                                                             \
    do {                                                                                                     \
        if (((fm1_led_hi[c] >> (r)) & 1u) && (lv) < FM1CORE_RHYTHMER_HI_LEVEL)                               \
            (lv) = FM1CORE_RHYTHMER_HI_LEVEL;                                                                \
    } while (0)
#define FM1CORE_FW_EDITOR_C "../../cores/Rhythmer/jiant/firmware/src/editor.c"   /* the web editor's SysEx (and the
                                                                                     * break bank uploads) */
#define FM1CORE_FW_OTA_C "../../cores/Rhythmer/jiant/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_748001"        /* build.sh --product: the release package's identity */
#include "fw_core.h"

/* ------------------------------------------------- the factory breaks --- */
#include "rhythmer_breaks.h"                /* (generated: the package's two slots and its marker) */
static int rhy_erased(uint32_t at, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (nor[at + i] != 0xFFu)
            return 0;
    return 1;
}
static void rhy_breaks_preload(void)       /* after the flash image is opened, before web_power_on */
{
    static const uint8_t *const BANK[2] = {RHY_BRK0, RHY_BRK1};
    uint32_t k, n = 0;
    for (k = 0; k < 2u; k++) {
        const uint32_t at = RHY_BRK_AT + k * RHY_BRK_SLOT;
        if (rhy_erased(at, RHY_BRK_SLOT)) {
            memcpy(nor + at, BANK[k], RHY_BRK_SLOT);
            n++;
        }
    }
    if (n && rhy_erased(RHY_BRK_MARK_AT, sizeof RHY_BRK_MARK))
        memcpy(nor + RHY_BRK_MARK_AT, RHY_BRK_MARK, sizeof RHY_BRK_MARK);
}
