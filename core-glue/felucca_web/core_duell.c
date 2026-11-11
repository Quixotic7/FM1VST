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
 *   - its version string is the web unit's own (DUELL_VERSION_STR from duell_version.h);
 *   - KEY SENSE is built in (DUELL_VELO 1, as the release's tools/build.py builds the device since 0.2.30): the panel's
 *     key_vel / key_lean reach the firmware's own sense.c as the converter readings it reads on the unit (below).
 *
 * KEY SENSE (sense.h, sense_cal.h; after Velo's method). On the unit the scan tick (hal/fm1_input.h) reads each of the
 * 20 lower keys' carbon pill with the converter, undriven (open ~1023, seated 190 .. 360: the landing depth) or with the
 * row driven (the pill's resistance: lower = harder), and hands every reading to sense_update; sense.c turns them into
 * the landing's velocity (sense_gate / sense_vel) and the held keys' aftertouch (sense_at_value). The web unit has no
 * scan tick (fm1_input_tick is empty), so the glue is the tick: after each device ms, one reading of every analog key
 * (sense_want_drive says driven or not, as the tick asks), from a model of a key played at the panel's velocity:
 *   - a press: seated at the landing depth that sense.c's first estimate maps to the velocity (sk_depth_vel inverted);
 *     the driven landing reading is the one that gives the velocity through the key's calibration as it will be
 *     after this landing (sk_cal, then sk_map, tried on a copy for every reading 250 .. 760: sense.c learns each key's
 *     range from its landings, so the reading is chosen against the learned range, and the velocity is the one asked
 *     for whatever was played before). No velocity given (key_vel 0): 100, the firmware's own default.
 *   - held: the driven readings go below sense.c's reference by key_lean (0 .. 100 % -> aftertouch 0 .. 127 through
 *     SC_AT_FLOOR / SC_AT_SPAN), following the reference as it creeps: key_lean is the pressure held, so the
 *     aftertouch stays at it (sense.c smooths the readings, so a change glides in over a few ms).
 *   - let go: open readings.
 * The firmware's settings decide what is used: MENU > CONTROL > KEY VELOCITY (OFF by default: every key plays 100, as
 * a build without KEY SENSE) and AFTERTOUCH (ON by default: key_lean is channel pressure, the matrix source AT). The
 * top 7 keys (notes 20 .. 26) have no converter on the unit: SC_TOP_VEL (100), as there. The MASTER pot and the
 * battery, which a KEY SENSE build reads in the tick's slots (fm1_an), read as the web unit's fm1_adc_read: a new
 * reading every time (fm1core_du_an). */
#include <stdint.h>
#define DUELL_VELO 1
#include "sense.c"                        /* (the firmware's KEY SENSE core, ahead of the web unit as its own
                                           * tests/velo_hook_test.c has it ahead of hostsim.c, which leaves it out) */
typedef struct {
    int16_t pot, batt;
    uint32_t pot_n, batt_n;
} fm1core_du_an_t;
static fm1core_du_an_t *fm1core_du_an(void);
#define fm1_an (*fm1core_du_an())         /* (master.c, main.c: the tick's latest pot and battery readings) */
static void fm1core_du_keys(void);
#define FM1CORE_FW_P du
#define FM1CORE_FW_WEB_C "../../cores/DUELL/duell/web/emu/felucca_web.c"
#define FM1CORE_FW_ANIM_IDLE 1
#define FM1CORE_FW_UI_HOME() 0                /* (DUELL's HOME is a page: ui.page's title says it) */
#define FM1CORE_FW_AFTER_MS()                                                                                \
    do {                                                                                                     \
        fm1core_du_keys();                                                                                   \
        if (web_reboot_req) {                                                                                \
            web_reboot_req = 0;                                                                              \
            printf("duell: ERASE DATA done: the device restarts\n");                                         \
            fm1core_exit(0);                                                                                 \
        }                                                                                                    \
    } while (0)
#define FM1CORE_FW_LED(c, r, lv) fm1core_du_led((c), (r), &(lv))
static void fm1core_du_led(uint32_t c, uint32_t r, uint32_t *lv);
#define FM1CORE_FW_EDITOR_C "../../cores/DUELL/duell/firmware/src/editor.c"   /* the web editor's SysEx */
#define FM1CORE_FW_OTA_C "../../cores/DUELL/duell/firmware/src/ota.c"         /* the M-UPGRADE identity */
#define FELUCCA_ID "FM-1_901"       /* felucca-base/tools/build.py PRODUCT, every DUELL package (the Hub's duell 1.1) */
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

/* ---- KEY SENSE: the scan tick's converter readings from the panel (the header above) ---- */
static fm1core_du_an_t *fm1core_du_an(void)
{
    static fm1core_du_an_t an;
    an.pot = (int16_t)fm1_adc_read(FM1_ADC_MASTER);
    an.batt = (int16_t)fm1_adc_read(FM1_ADC_BATT);
    an.pot_n++;
    an.batt_n++;
    return &an;
}

static struct {
    uint32_t down;                        /* the analog keys the model holds */
    uint16_t u[SC_NKEY], d[SC_NKEY];      /* a press's undriven (seated) and driven landing readings */
} fm1core_du_ks;

/* the undriven landing depth sense.c's first estimate maps to vel (sk_depth_vel inverted) */
static uint32_t fm1core_du_depth(uint32_t vel)
{
    const uint32_t span = SC_LAND_SOFT - SC_LAND_HARD, vs = SC_VEL_MAX - SC_VEL_MIN;
    vel = vel < SC_VEL_MIN ? SC_VEL_MIN : vel > SC_VEL_MAX ? SC_VEL_MAX : vel;
    return SC_LAND_SOFT - ((vel - SC_VEL_MIN) * span + vs / 2u) / vs;
}
/* the driven landing reading that key note's landing maps to vel: its calibration after this landing (sk_cal, or the
 * calibration walk's, which leaves the range alone until the key is done), then sk_map, on a copy. Of the readings
 * that give vel, the one nearest the middle of the priors: the learned range moves least, and a hard landing leaves
 * room below it for the aftertouch */
static uint32_t fm1core_du_driven(uint32_t note, uint32_t vel)
{
    const uint32_t moved = sn.moved6, mid = (SC_FLOOR_PRIOR + SC_CEIL_PRIOR) / 2u;
    uint32_t d, best = mid, err = ~0u, dist = ~0u;
    for (d = SC_D_MIN; d <= SC_D_MAX; d++) {
        sk_t k = sn.k[note];
        uint32_t v, e, m;
        if (!sn.walking)
            sk_cal(&k, d);
        v = sk_map(&k, d);
        e = v > vel ? v - vel : vel - v;
        m = d > mid ? d - mid : mid - d;
        if (e < err || (e == err && m < dist)) {
            err = e;
            dist = m;
            best = d;
        }
    }
    sn.moved6 = moved;                    /* (sk_cal's count of the calibration's moves: the copy's do not count) */
    return best;
}
/* after each device ms: the analog keys of one parity (each key every 2 ms, as the scan reads one every two 1.1 ms
 * frames), driven when sense.c asks for it */
static void fm1core_du_keys(void)
{
    fm1panel_t *const p = FM1CORE_PANEL;
    const uint32_t held = fm1_in.notes, t0 = fm1_ms * 1000u;
    uint32_t n;
    if (!sense_running())
        return;
    for (n = fm1_ms & 1u; n < SC_NKEY; n += 2u) {
        const uint32_t b = 1u << n;
        uint32_t adc = 1023u, driven = 0;
        if (held & b) {
            if (!(fm1core_du_ks.down & b)) {  /* pressed: the readings for the panel's velocity */
                const uint32_t vel = fm1p_vel(p, n, 100);
                fm1core_du_ks.down |= b;
                fm1core_du_ks.u[n] = (uint16_t)fm1core_du_depth(vel);
                fm1core_du_ks.d[n] = (uint16_t)fm1core_du_driven(n, vel);
            }
            driven = sense_want_drive(n);
            if (!driven) {
                adc = fm1core_du_ks.u[n];
            } else if (sn.k[n].fl & K_PEND) {
                adc = fm1core_du_ks.d[n];     /* the landing */
            } else {
                /* held: key_lean's aftertouch as counts below sense.c's reference (the landing reading, crept down
                 * while a pressure is held), and with no lean one count below it: the reference's creep must never
                 * pass the reading, as sense.c's v = d_ref - ds is unsigned and reads 127 for that one reading (on
                 * the unit a steady reading meets it too). Below SC_D_MIN (a long hold): a reading sense.c rejects,
                 * the pressure kept */
                const uint32_t at = (uint32_t)fm1p_lean(p, n) * 127u / 100u;
                const uint32_t ref = sn.k[n].d_ref ? sn.k[n].d_ref : fm1core_du_ks.d[n];
                const uint32_t off = at ? SC_AT_FLOOR + (at * (SC_AT_SPAN - SC_AT_FLOOR) + 126u) / 127u : 1u;
                adc = ref >= SC_D_MIN + off ? ref - off : SC_D_MIN - 1u;
            }
        } else {
            fm1core_du_ks.down &= ~b;
        }
        sense_update(n, adc, driven, t0 + n * 50u);   /* (the keys' slots spread over the ms, as the scan's columns) */
    }
}
