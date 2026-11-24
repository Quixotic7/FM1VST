/* SPDX-License-Identifier: GPL-3.0-only */
/* The DABKE core: DABKE 0.18 (beta) by STUDIOMORPHIK (https://github.com/wissamsader/dabke, the pinned, unmodified
 * submodule cores/dabke, MPL-2.0, with fm1-base (its base/ submodule: wissamsader/fm1-base@a9423ea, fm1-base f66ac4a +
 * APP_UPDATE_SAVE) and felucca-bsp, MPL-2.0) built as one unit with the fm1-base glue (core-glue/fm1base/README.md).
 * Its base/ is Chimera Vocal's fm1-base file for file but for APP_UPDATE_SAVE (main_fm1.c's update session, never
 * reached here): this unit follows core_chimeravocal.c without the second core (fm1.toml has no cpu2) and without
 * btdev.c (no APP_BTDEV), with the app's log lines (APP_LOG) as core_sy1.c. The include list follows fm1-base's device
 * unity build (base/device/unity.c) with the hardware left out: plat.h, the app's header, lib/touch.h (APP_TOUCH),
 * gfx.c, fm1-base's defaults (fm1b_config.h), the platform's screens (sys_text.c), the store (storage.c on the NOR
 * image, the app's APP_STORE_MAP: the settings, the panel, the kept sounds, the sequencer's part), the updater's framing
 * (felucca-bsp's chip/ota.c: the M-UPGRADE identity), the screen reader (announce.c), fm1-base's panel calibration
 * (base/device/panel.c, by its path: the app's own app/panel.c is its home screen, which app.c includes with scene.c),
 * the build with no radio (radio_none.c, fm1.toml ble = false), the device's plat.h implementation (plat_fm1.c),
 * lib/touch.c's host part and the app (app.c). The engine is fm1.toml's "o2" unit (app/dabke.c), its own unit.
 *
 * The USB MIDI host (fm1.toml usb_host = true: the Akai MIDImix on the FM-1's USB port) is not built: FM1B_USB_HOST 0,
 * fm1-base's stubs (plat_usb_host does nothing, the state stays a device); the host's MIDI reaches the app as the USB
 * device's.
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 0.18's: "0.18" and FM-1_65001800
 * (fm1.toml id_prefix FM-1_65 + 00 18 00); core_dabke.cmake derives both. Left out, as in every core: the splash.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives each SysEx
 * frame to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47: "no crash recorded", the app's lines,
 * then "end"), the remote control (7D 4C 43) and the app's own (app_sysex: none, it takes nothing), then runs
 * ota_service (the M-UPGRADE handshake; usb_cmd has nothing in this build). The update key F0 22 24 35 7F F7 and
 * UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h).
 *
 * Saving: DABKE saves lazily (app_frame: the settings and the sequencer's part a few seconds after a change, in a quiet
 * moment); flash_sync saves them now (below). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>                       /* before exit.h: its prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "nor.h"
#include "fifo.h"
#include "ticks.h"
#include "panel.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

#ifndef APP_VERSION
#error "APP_VERSION: build.py --release's (core_dabke.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_dabke.cmake)"
#endif
#define FM1B_BACKUP 0                     /* (fm1.toml has no backup) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_FB_MIRROR_DIM 1           /* (this fm1-base's mirror_leds: the glow's LEDs too; mirror_mid) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#ifdef APP_TOUCH
#include "touch.h"
#endif
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"
#include "storage.c"

/* felucca-bsp's chip/ota.c on the glue's transport (plat_sx.h), with unity.c's hooks on the core's HAL: the device's
 * ms, the wait (the watchdog fed), the update's staging area (OTA_AREA) on the NOR image. The update's own steps
 * (ota_session: its screen, its commit) are never reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/dabke/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_dab_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_dab_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_dab_ota_in(off, n))
        return -8;
    return fm1nor_prog_span(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    (void)step;
    (void)code;
}
static void ota_commit(const uint8_t *parm) { (void)parm; }

/* felucca-bsp's usb.c and guard, as plat_fm1.c reads them: the USB link never lost (a host is always there); no
 * guarded PC range to extend */
static volatile uint8_t midi_usb_lost;
static void fm1_guard_pc_extend(uint32_t end) { (void)end; }
#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading): not sent */
{
    return fm1core_pl_sx_send(p, n);
}
#include "../../cores/dabke/base/device/panel.c"   /* fm1-base's panel calibration (app/panel.c is DABKE's home
                                                       * screen: app.c includes it) */
#include "radio_none.c"
/* plat_fm1.c's SysEx stream (plat_sysex_begin / put / end, plat_sysex_room; app.c's replies through plat_sysex_send)
 * writes USB-MIDI packets into usb.c's SysEx ring: here the managers' out ring (plat_sx.h fm1core_pl_sxo, the same
 * CIN 4..7 packets), read by midi_out_take; the stream holds it (sx_busy) from begin to end */
static uint8_t sx_busy;
#define SXQ FM1SX_OUT_PKTS
#define sx_out_q fm1core_pl_sxo.q
#define so_w fm1core_pl_sxo.w
#define so_r fm1core_pl_sxo.r
#include "plat_fm1.c"
#undef SXQ
#undef sx_out_q
#undef so_w
#undef so_r
#ifdef APP_TOUCH
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST
#endif
#include "app.c"                          /* (the device's paths: PLAT_HOST off; only app_expect is host-only) */
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7: "no crash
 * recorded", the app's own lines (APP_LOG: the keys' pressure), "end" last */
static void fm1core_dab_log_line(const char *t)
{
    uint8_t f[48];
    uint32_t n = 0;
    f[n++] = 0xF0;
    f[n++] = 0x7D;
    f[n++] = 0x4C;
    f[n++] = 0x47;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    fm1core_pl_sx_send(f, n);
}
static int fm1core_dab_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_dab_log_line("no crash recorded");
#ifdef APP_LOG
    app_log(fm1core_dab_log_line);
#endif
    fm1core_dab_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base's: no lease) */
static int fm1core_dab_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);   /* (DABKE has no SysEx of its own) */
}
/* main_fm1.c fm1_main, each pass: a frame one service took is done; a frame none took goes to ota_service (usb_cmd:
 * nothing in this build) */
static void fm1core_dab_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n)) {
        if (ann_frame(p, n, fm1_ms) || fm1core_dab_log_frame(p, n) || fm1core_fb_remote_frame(p, n) || app_sysex(p, n))
            ota_frame_done();
        else
            ota_service();
    }
}
#define FM1CORE_PL_SX_ACCEPT fm1core_dab_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_dab_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
/* the live knob labels and readouts (common/rd_cols.h): the knob boxes as app/app.c draws them (knob_value, cut to
 * its box as T() cuts it): a knob page (knob_boxes: KROW, "VOLUME" for the mixer rows' K1), the step view's strip
 * (panel.c seq_view: STRIP_ROW), ARP's panel (arp_page: the row the knobs turn, "ARP " dropped) and the maqam page
 * (maqam_page's four boxes, their "K1 " tags dropped). Not given: START FRESH, SAVE's slots, the home page without
 * step mode (no row drawn; K1..K4 the strip or the parts' volumes), ARP held before its panel shows. */
#include "rd_cols.h"
static void fm1core_dabke_fit(char *d, const char *s, int size, int32_t maxw)   /* T()'s text, not drawn */
{
    char u[48];
    int f = size <= 10 ? 0 : size <= 13 ? 1 : size <= 17 ? 2 : size <= 24 ? 3 : 4, n = 0, lo = 0;
    int32_t w;
    while (s[n] && n < 47) {
        int flat = s[n] == 'b' && n > 0 && s[n - 1] >= 'A' && s[n - 1] <= 'G' && (s[n + 1] < 'a' || s[n + 1] > 'z');
        u[n] = flat ? 'b' : (char)up_char((uint8_t)s[n]);
        n++;
    }
    u[n] = 0;
    w = text_w(FACE[f], u);
    while (maxw && w > maxw && f > lo)
        w = text_w(FACE[--f], u);
    if (FACE[f]->last < 'b')
        for (n = 0; u[n]; n++)
            u[n] = (char)up_char((uint8_t)u[n]);
    while (maxw && w > maxw && n > 1) {
        u[--n] = 0;
        w = text_w(FACE[f], u);
    }
    snprintf(d, RDC_LEN, "%s", u);
}
static void fm1core_dabke_box(uint32_t c, int id, int first_vol)   /* knob_boxes' one box */
{
    char b[24], l[RDC_LEN], v[RDC_LEN];
    const char *label = "";
    int pid;
    if (id == K_NONE) {
        rdc_col(c, "", "");
        return;
    }
    knob_value(id, b, &label);
    if (first_vol)
        label = "VOLUME";
    pid = strip_id(id) >= 0 ? strip_id(id) : id;
    fm1core_dabke_fit(l, label, 9, set.track && lk_ok(pid) && lk_has(picked(), pid) ? 44 : 51);
    fm1core_dabke_fit(v, b, 15, 53);
    rdc_col(c, l, v);
}
static void fm1core_dabke_rd(void)
{
    uint32_t c;
    rdc_begin();
    if (ui.fresh || (ui.arp_held && !arp_panel_up()))
        return;
    if (arp_panel_up()) {                     /* arp_page, the row framed */
        for (c = 0; c < 4u; c++) {
            char b[24], l[RDC_LEN], v[RDC_LEN];
            const char *label = "";
            knob_value(ARP_ROW[ui.arp_row & 1u][c], b, &label);
            if (label[0] == 'A' && label[1] == 'R' && label[2] == 'P' && label[3] == ' ')
                label += 4;
            fm1core_dabke_fit(l, label, 9, 51);
            fm1core_dabke_fit(v, b, 13, 51);
            rdc_col(c, l, v);
        }
        return;
    }
    if (ui.save)
        return;
    if (ui.page == PG_HOME) {
        if (!ui.seq || !set.track)            /* (no row; or the step view's strip while K1..K4 turn the parts) */
            return;
        for (c = 0; c < 4u; c++)
            fm1core_dabke_box(c, STRIP_ROW[c], 0);
        return;
    }
    if (ui.page == PG_SEL) {                  /* maqam_page's boxes */
        static const char *const LAB[4] = {"CENTS", "MAQAM", "ROOT", "DEPTH"};
        for (c = 0; c < 4u; c++) {
            char b[40], t[12], v[RDC_LEN];
            b[0] = 0;
            if (c == 0) {
                text_cat(b, NOTE_NAME[ui.cur]);
                text_cat(b, " ");
                int_text(set.ntune[ui.cur], t);
                text_cat(b, t);
            } else if (c == 1)
                maqam_name(b, 0);
            else if (c == 2)
                text_cat(b, NOTE_NAME[set.ch[C_ROOT] % 12]);
            else {
                int_text(set.qdepth, t);
                text_cat(b, t);
                text_cat(b, " CT");
            }
            fm1core_dabke_fit(v, b, 13, 50);
            rdc_col(c, LAB[c], v);
        }
        return;
    }
    for (c = 0; c < 4u; c++)                  /* knob_page */
        fm1core_dabke_box(c, KROW[ui.page - PG_FX][ui.row[ui.page - PG_FX]][c],
                          c == 0 && ui.page == PG_FX && ui.row[0] >= MIX_ROW);
}
#define FM1CORE_PL_UI_FRAME() (app_frame(), fm1core_dabke_rd())
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights_mid's steady mid level */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

#ifdef APP_KEY_SENSE
static void fm1core_fb_sense_tick(void)  /* the keys' readings, while plat_sense is on (the converter is the scan's) */
{
    if (fm1_an.con)
        app_host_tick();
}
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
/* the keys' velocity (APP_TOUCH): the panel's key_vel / key_lean become the readings the scan would hand the app,
 * through lib/touch.c's own host model (as core_echofield.c) */
extern fm1panel_t fm1core_pl_panel;
static uint8_t fm1core_fb_vel[NKEYS], fm1core_fb_lean[NKEYS];
static void fm1core_fb_keys(uint32_t down, uint32_t up, uint32_t held)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        const uint8_t lean = fm1p_lean(&fm1core_pl_panel, k);
        if ((down >> k) & 1u) {
            fm1core_fb_vel[k] = fm1p_vel(&fm1core_pl_panel, k, 100);   /* (none given: 100, the emulator's) */
            fm1core_fb_lean[k] = lean;
            app_host_key(k, fm1core_fb_vel[k], lean);
        } else if ((up >> k) & 1u) {
            app_host_key(k, 0, 0);
        } else if (((held >> k) & 1u) && lean != fm1core_fb_lean[k]) {
            fm1core_fb_lean[k] = lean;
            app_host_key(k, fm1core_fb_vel[k], lean);   /* the lean changed: the same landing, pressed more */
        }
    }
}
#define FM1CORE_PL_KEY_HOOK(down, up, held) fm1core_fb_keys(down, up, held)
#endif

static void fm1core_pl_flash_boot(void)  /* fm1_main: nothing (no backup.c: no restore record) */
{
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* the engine, then the saved settings (or the starting sound) */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block (no USB audio) */
{
    app_render(lr, n, APP_GAIN(master_pos));
}
static void fm1core_pl_master(int32_t knob)   /* main_fm1.c: MASTER's position 0..4092 */
{
    fm1core_fb_master_adc = knob / 16;
    if (!fm1_an.con)
        fm1_an.master = (uint16_t)fm1core_fb_master_adc;
    master_pos = remote.master >= 0 ? (uint32_t)remote.master : (uint32_t)(knob / 4);
}
static void fm1core_pl_loop(void) { ble_poll(); }
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        app_say("UPDATE MODE IN", d);
    } else {
        app_say("UPDATE MODE", "CANCELLED");
    }
}
/* the lazy save now: app_frame's (the settings and the sequencer's part, a few seconds after a change, in a quiet
 * moment) without its waits; while a track records: deferred (its loop is kept when the recording ends) */
static int fm1core_pl_sync(void)
{
    if (!set_dirty && !sq_dirty)
        return 1;
    if (dabke_status()->rec_track >= 0)
        return 0;
    if (save_settings() != 0)
        save_settings();                  /* (a failed save tried once more, as app_update_save) */
    return !set_dirty && !sq_dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    const dabke_status_t *s = dabke_status();
    snprintf(buf, n, "page %d running %d rec %d dirty %u/%u", (int)ui.page, (int)s->running, (int)s->rec_track,
             (unsigned)set_dirty, (unsigned)sq_dirty);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
