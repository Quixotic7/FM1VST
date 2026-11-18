/* SPDX-License-Identifier: GPL-3.0-only */
/* The Quantum GammaWave core (https://github.com/Guillel51/Quantum-GammaWave, tag v4.1.0 = 671ac35, the pinned,
 * unmodified submodule cores/Quantum-GammaWave): Guillel51's fork of Pocket GammaWave (Frederic Valente), itself a
 * JIANT FM / Felucca fork, whose browser unit, web/emu/felucca_web.c, is driven by the family's glue (fw_core.h).
 * After core_jiant.c, whose notes hold here but for:
 *   - no fifth LED plane (fm1_led_hi): the tree's HAL has JIANT's four (lit, dim, breath, mid);
 *   - the parts' voices come from one shared pool (3.5: V(t, i) = vpool[t->vidx[i]]), not trk[i].v.
 * Still as JIANT: the session's random macros (session_dice) take the web unit's default seed, 1 (no host clock in a
 * core); MENU > RESTORE LAST is OFF by default (PREF_RESTORE_ON). */
#ifndef FM1CORE_QUANTUMGAMMAWAVE_SEED
#define FM1CORE_QUANTUMGAMMAWAVE_SEED 1u
#endif
#define FM1CORE_FW_P qg
#define FM1CORE_FW_WEB_C "../../cores/Quantum-GammaWave/web/emu/felucca_web.c"
#define FM1CORE_FW_BEFORE_BOOT() web_seed(FM1CORE_QUANTUMGAMMAWAVE_SEED)
#define FM1CORE_FW_RESTORE_ON() ((ui_prefs & PREF_RESTORE_ON) != 0)
#define FM1CORE_FW_VOICE(t, v) V(&trk[t], v)                                    /* the shared voice pool (core.h) */
#define FM1CORE_FW_EDITOR_C "../../cores/Quantum-GammaWave/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/Quantum-GammaWave/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_965"       /* tools/build.py PRODUCT: Quantum GammaWave's own identity (QG-1) */
#include "fw_core.h"
/* ota.c (1.x-GW) declares two raw-NOR helpers that the device's ota_hw.c defines: no SPI here, so no JEDEC id (0, "no
 * answer") and no raw read (an error), as the sysex_ota.h stand-ins answer the other flash calls */
static uint32_t ota_jedec(void) { return 0u; }
static int ota_rawread(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
