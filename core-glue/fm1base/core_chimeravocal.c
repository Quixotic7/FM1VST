/* SPDX-License-Identifier: GPL-3.0-only */
/* The Chimera Vocal core: FM-1 Chimera Vocal (https://github.com/shan-sherif/fm-1-chimera-vocal, the pinned, unmodified
 * submodule cores/fm-1-chimera-vocal, MPL-2.0 AND MIT, with fm1-base and felucca-bsp vendored under base/, MPL-2.0)
 * built as one unit with the fm1-base glue (core-glue/fm1base/README.md). Its base/ is SY1's and LOOM-8's fm1-base
 * file for file: this unit follows core_sy1.c (no backup.c, no resource region, lib/touch.c's host part for
 * APP_TOUCH), with btdev.c (APP_BTDEV: the Bluetooth MIDI devices' list, on radio_none.c: none found) as core_loom.c
 * and the app's own SysEx (app_sysex, 7D 56 4F: the Studio's words, tools/ and app/web/studio.html). The include list
 * follows fm1-base's device unity build (base/device/unity.c) with the hardware left out: plat.h, the app's header,
 * gfx.c, fm1-base's defaults (fm1b_config.h), the platform's screens (sys_text.c), the store (storage.c on the NOR
 * image, the app's APP_STORE_MAP: settings, panel, words, presets, creatures, beats, song), the updater's framing
 * (felucca-bsp's chip/ota.c: the M-UPGRADE identity), the screen reader (announce.c), the panel calibration
 * (panel.c), the build with no radio (radio_none.c, fm1.toml ble = false), the device's plat.h implementation
 * (plat_fm1.c), btdev.c, lib/touch.c and the app (app.c: the words, the singing, the beatbox, the creatures, the UI).
 * The engine is fm1.toml's "o2" units (engine/ch_*.c, CHIMERA's port), their own units.
 *
 * THE SECOND CORE (fm1.toml cpu2 = true: the airway, the waveguide, renders on it): as LOOM-8's glue (loom_plat.c)
 * and fm1-base's host simulator (host/plat_host.c) run it, a job runs at once, in order, on the host's one thread,
 * plat_core() 1 while it runs; it is never given up or barred. app_render's output is the same as on a two-core FM-1.
 * fm1-base's device half (cpu2.c) is not built; plat_fm1.c's plat_core is renamed away.
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 0.9.0's (the release workflow's):
 * "0.9.0" and FM-1_042000900 (fm1.toml id_prefix FM-1_042 + 00 08 00); core_chimeravocal.cmake derives both.
 * Left out, as in every core: the splash (APP_SPLASH: app.c app_splash, the head side on; plat_glue.h starts the UI
 * at power-on).
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives each SysEx
 * frame to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47: "no crash recorded", then "end"), the
 * remote control (7D 4C 43) and the app's own (app_sysex: F0 7D 56 4F cmd ..), then runs ota_service (the M-UPGRADE
 * handshake; usb_cmd has nothing in this build). The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are
 * not taken (plat_sx.h).
 *
 * Saving: SAVE writes (the settings, the presets, the creatures; the song, the beats, the words on their pages), the
 * keyboard layout and the Studio's words at once, as on the device; flash_sync has nothing pending. */
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
#error "APP_VERSION: build.py --release's (core_chimeravocal.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_chimeravocal.cmake)"
#endif
#define FM1B_BACKUP 0                     /* (fm1.toml has no backup) */
#define FM1B_CPU2 1                       /* fm1.toml cpu2 = true: the second core, simulated (below) */
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
#include "../../cores/fm-1-chimera-vocal/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_chv_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_chv_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_chv_ota_in(off, n))
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
#include "panel.c"
#include "radio_none.c"
/* plat_fm1.c's SysEx stream (plat_sysex_begin / put / end, plat_sysex_room; app.c's replies through plat_sysex_send)
 * writes USB-MIDI packets into usb.c's SysEx ring: here the managers' out ring (plat_sx.h fm1core_pl_sxo, the same
 * CIN 4..7 packets), read by midi_out_take; the stream holds it (sx_busy) from begin to end */
static uint8_t sx_busy;
#define SXQ FM1SX_OUT_PKTS
#define sx_out_q fm1core_pl_sxo.q
#define so_w fm1core_pl_sxo.w
#define so_r fm1core_pl_sxo.r
#define plat_core fm1core_chv_dev_core    /* (the device's, fm1_cnum: the simulated second core's below) */
#include "plat_fm1.c"
#undef plat_core
#undef SXQ
#undef sx_out_q
#undef so_w
#undef so_r
#include "btdev.c"                        /* (APP_BTDEV: the GLOBAL page's Bluetooth devices; no Bluetooth here, none found) */

/* the second core, simulated (plat_host.c's, as loom_plat.c): a job runs at once, in order; never given up */
static int fm1core_chv_c2_on, fm1core_chv_c2_in_job;
int plat_cpu2_set(int on)
{
    fm1core_chv_c2_on = on != 0;
    return 0;
}
int plat_cpu2_on(void) { return fm1core_chv_c2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1core_chv_c2_on)
        return 0;
    fm1core_chv_c2_in_job = 1;
    fn(arg);
    fm1core_chv_c2_in_job = 0;
    return 1;
}
uint32_t plat_core(void) { return (uint32_t)fm1core_chv_c2_in_job; }
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }   /* (a job ran at once) */
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }

#ifdef APP_TOUCH
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST
#endif
#include "app.c"                          /* (the device's paths: PLAT_HOST off; only app_expect is host-only) */
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7: "no crash
 * recorded", then "end" (Chimera has no APP_LOG) */
static void fm1core_chv_log_line(const char *t)
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
static int fm1core_chv_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_chv_log_line("no crash recorded");
    fm1core_chv_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base's: no lease) */
static int fm1core_chv_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u) || fm1core_pl_sx_is(f, n, 0x56u, 0x4Fu);   /* (the app's: 7D 56 4F) */
}
/* main_fm1.c fm1_main, each pass: a frame one service took is done; a frame none took goes to ota_service (usb_cmd:
 * nothing in this build) */
static void fm1core_chv_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n)) {
        if (ann_frame(p, n, fm1_ms) || fm1core_chv_log_frame(p, n) || fm1core_fb_remote_frame(p, n) || app_sysex(p, n))
            ota_frame_done();
        else
            ota_service();
    }
}
#define FM1CORE_PL_SX_ACCEPT fm1core_chv_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_chv_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
/* the live knob labels and readouts (common/rd_cols.h): the knob row as app/app.c draws it on each page of draw():
 * draw_home (PAGE[page][i], KNOB[id].name, "NASAL" for VELUM on VOWEL, knob_text), draw_seal (CRE_PAGE, CRE->p[id].name,
 * seal_text), draw_beat (TEMPO / SWING / STEP, K4 none) and draw_song (TEMPO / KEY / VIBRATO / SLOT), as drawn.
 * Not given: the word editor (ed.on: its legend, K1 moves the cursor, K2 types marks) and the debug page (page_debug). */
#include "rd_cols.h"
static void fm1core_chimeravocal_rd(void)
{
    char t[32];
    int i;
    rdc_begin();
    if (ed.on || page_debug)
        return;
    if (bt.on) {                              /* draw_beat's strip */
        int_text(beats.b[bt.slot].bpm, t);
        rdc_col(0, "TEMPO", t);
        int_text(beats.b[bt.slot].swing, t);
        text_cat(t, "%");
        rdc_col(1, "SWING", t);
        int_text(bt.cur + 1, t);
        rdc_col(2, "STEP", t);
        rdc_col(3, "", "");
        return;
    }
    if (sg.on) {                              /* draw_song's strip */
        static const char *const NM[4] = {"TEMPO", "KEY", "VIBRATO", "SLOT"};
        for (i = 0; i < 4; i++) {
            t[0] = 0;
            if (i == 0) {
                text_cat(t, "X");
                fmt1((float)sng.tempo / 100.0f, t + 1);
                if (sng.tempo % 10) {
                    char d[2] = {(char)('0' + sng.tempo % 10), 0};
                    text_cat(t, d);
                }
            } else if (i == 1) {
                if (sng.key > 0)
                    text_cat(t, "+");
                int_text(sng.key, t + (sng.key > 0));
            } else if (i == 2)
                int_text(sng.vibrato, t);
            else
                int_text(set.slot + 1, t);
            rdc_col((uint32_t)i, NM[i], t);
        }
        return;
    }
    if (body_cur == BODY_SEAL) {              /* draw_seal's settings */
        const ch_cdef_t *d = CRE;
        for (i = 0; i < 4; i++) {
            const int id = CRE_PAGE[cre.which][seal_page][i];
            if (id < 0) {
                rdc_col((uint32_t)i, "", "");
                continue;
            }
            seal_text(id, t);
            rdc_col((uint32_t)i, d->p[id].name, t);
        }
        return;
    }
    for (i = 0; i < 4; i++) {                 /* draw_home's readouts */
        const int id = PAGE[page][i];
        knob_text(id, t);
        rdc_col((uint32_t)i, page == PAGE_VOWEL && id == K_VELUM ? "NASAL" : KNOB[id].name, t);
    }
}
#define FM1CORE_PL_UI_FRAME() (app_frame(), fm1core_chimeravocal_rd())
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
    app_init();                           /* the engine, the second core, then the saved settings (or defaults) */
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
/* nothing pending: SAVE and the pages' own saves write at once, as on the device */
static int fm1core_pl_sync(void) { return 1; }
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "last key %d dirty %u debug %u", (int)ui.last_key, (unsigned)set_dirty, (unsigned)page_debug);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
