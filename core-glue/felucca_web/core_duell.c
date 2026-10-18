/* SPDX-License-Identifier: GPL-3.0-only */
/* The DUELL core (https://github.com/oscillr/DUELL, the pinned, unmodified submodule cores/DUELL; an unofficial build
 * of the release tag, NOTICE.md): DUELL is Felucca main (felucca-base/) changed, and its browser unit is Felucca's
 * own felucca-base/web/emu/felucca_web.c (with DUELL's WAVE build, VOICE CAP and ERASE DATA restart in the frame),
 * driven by the family's glue (fw_core.h). What differs from Felucca 1.6.1's unit:
 *   - the keyboard's four LED sets (0.2.9): the keys out of the scale, the scale's, the root's at their levels
 *     (fm1_led_key_levels, /240 of lit), and the keys held or playing pulsing between a peak and a trough
 *     (fm1_led_pulse's period, ~1.13 s by default);
 *   - ERASE DATA restarts the device (fm1_reboot: web_reboot_req): the core halts with exit(0), the host powers it on
 *     again (a reload);
 *   - its version string is the web unit's own (DUELL_VERSION_STR from duell_version.h). */
#include <stdint.h>
#define FM1CORE_FW_P du
#define FM1CORE_FW_WEB_C "../../cores/DUELL/felucca-base/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_UI_HOME() 0                /* (DUELL's HOME is a page: ui.page's title says it) */
#define FM1CORE_FW_AFTER_MS()                                                                                \
    do {                                                                                                     \
        if (web_reboot_req) {                                                                                \
            web_reboot_req = 0;                                                                              \
            printf("duell: ERASE DATA done: the device restarts\n");                                         \
            fm1core_exit(0);                                                                                 \
        }                                                                                                    \
    } while (0)
#define FM1CORE_FW_LED(c, r, lv) fm1core_du_led((c), (r), &(lv))
static void fm1core_du_led(uint32_t c, uint32_t r, uint32_t *lv);
#include "fw_core.h"

/* the keyboard's sets: a level /240 of lit each (web_kl: out | scale << 8 | root << 16 | the played peak << 24,
 * web_klow the played trough), the played keys pulsing between peak and trough over web_pu_ms (0: ~1130 ms) */
static void fm1core_du_led(uint32_t c, uint32_t r, uint32_t *lv)
{
    const uint32_t bit = 1u << r;
    uint32_t v = 0;
    if (fm1_led_kout[c] & bit)
        v = web_kl & 0xFFu;
    if ((fm1_led_scl[c] & bit) && ((web_kl >> 8) & 0xFFu) > v)
        v = (web_kl >> 8) & 0xFFu;
    if ((fm1_led_root[c] & bit) && ((web_kl >> 16) & 0xFFu) > v)
        v = (web_kl >> 16) & 0xFFu;
    if (fm1_led_ktop[c] & bit) {
        const uint32_t pk = (web_kl >> 24) & 0xFFu, lo = web_klow < pk ? web_klow : pk;
        const uint32_t per = web_pu_ms ? web_pu_ms : 1130u, x = (fm1_ms % per) * 512u / per;
        const uint32_t tri = x & 256u ? 511u - x : x;   /* 0..255, the peak at mid-period */
        const uint32_t p = lo + (pk - lo) * tri / 255u;
        if (p > v)
            v = p;
    }
    v = v * 255u / FM1_LED_KL_FULL;
    if (v > *lv)
        *lv = v;
}
