/* SPDX-License-Identifier: GPL-3.0-only */
/* The GHOULBOX core (https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth, the pinned, unmodified submodule
 * cores/ghoulbox-fm1-dungeon-synth): its own browser unit, web/emu/felucca_web.c (Felucca 1.6's, with GHOULBOX's
 * project capture exports), driven by the family's glue (fw_core.h). What differs from Felucca 1.6.1's unit:
 *   - its boot guard is GHOULBOX's own (bootguard_begin: a second failed boot starts the USB RESCUE, not UBOOT):
 *     not emulated (boot_options NULL);
 *   - web_lit_keys' chord overlay (the browser lights a chord's other keys) is the page's, not the device's: the
 *     panel shows fm1_led only, as the FM-1 does.
 * Randomness (DREAM / EVOL): libc.c's xorshift from a fixed seed (rng_state 0x1234567 at power-on), so a run is
 * deterministic in simulated time, as on the device after a reset. */
#define FM1CORE_FW_P gb
#define FM1CORE_FW_WEB_C "../../cores/ghoulbox-fm1-dungeon-synth/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_BOOTGUARD 0
#define FM1CORE_FW_EDITOR_C "../../cores/ghoulbox-fm1-dungeon-synth/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/ghoulbox-fm1-dungeon-synth/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_900"                                             /* (felucca.c) */
#include "fw_core.h"

/* the Tier 2 map in the .rfm1 (readouts, live knob labels): Felucca's, checked against this fork (fw_map_fork.h) */
#ifdef FM1CORE_RFM1
/* GHOULBOX's differences from Felucca 1.8.2's dispatch (its ui_input.c, from Felucca 1.4.1): SELECT is the crank
 * while any part plays the crank (crank_select), else the tempo (BPM LOCK: GLO held only); no SELECT modes, no EUCLID,
 * no HOME LEVELS (HOME's knobs are the selected part's) */
static int32_t fm1core_gb_select_target(void);
#define FE_SELECT_TARGET fm1core_gb_select_target()
#define FE_EUCLID_ON 0
#define FE_HOME_TRK(k) TSEL
#endif
#include "fw_map_fork.h"
#ifdef FM1CORE_RFM1
static int32_t fm1core_gb_select_target(void)
{
    const int glo = layer_held() && ui.ly == LAYER_GLO;
    uint32_t k;
    for (k = 0; k < NTRK && !crank_track(&trk[k]); k++)
        ;
    if ((!glo && k < NTRK) || (!glo && (ui_prefs & PREF_BPM_LOCK)))
        return -1;                                /* (the crank; BPM LOCK) */
    return fe_index_of(FEK_GP, G_BPM);
}
#endif
