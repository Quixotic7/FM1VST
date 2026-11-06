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
 * recordings there (assets/breaks: Amen, Think); no right to redistribute them is documented (audio-rights.json), so
 * the core carries neither: a fresh flash has both banks empty, as an erased unit's, and the BREAK machine is silent
 * until a bank is uploaded (the web editor's 80 KiB slots, six_upload.c, over the managers' SysEx) or the user's own
 * flash image holds one.
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
#define FM1CORE_FW_BEFORE_BOOT() web_seed(FM1CORE_RHYTHMER_SEED)
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
