/* SPDX-License-Identifier: GPL-3.0-only */
/* The MODELAR core: MODELAR 0.3 by Hirakos (https://github.com/hirakos999/MODELAR, the pinned, unmodified submodule
 * cores/MODELAR: MPL-2.0, with its own submodules base = fm1-base @ 865bc2f and base/bsp = felucca-bsp @ 33cac69,
 * MPL-2.0) built with the fm1-base glue (core-glue/fm1base/README.md), from VESPER's glue (core_vesper.c, branch
 * rfm1-vesper) without app_sysex (MODELAR has none). The include list follows fm1-base's device unity build
 * (base/device/unity.c) with the hardware left out: plat.h, the app's header, gfx.c, fm1-base's defaults
 * (fm1b_config.h), the platform's screen faces (sys_text.c), the store (storage.c on the NOR image, the app's
 * APP_STORE_MAP: the system settings at 0xFC000 and the panel calibration at 0xDC000; the 16 patch slots and the quick
 * backup are in the data area, store.c's plat_data_erase / plat_data_write from 0x97000), the updater's framing
 * (felucca-bsp's chip/ota.c: the M-UPGRADE identity), the screen reader (announce.c), the panel calibration (panel.c),
 * the build with no radio (radio_none.c, fm1.toml ble = false), the device's plat.h implementation (plat_fm1.c) and the
 * app (app.c: engine.c, store.c, ui.c). The sound (app/dsp/synth.c) is fm1.toml's "o2" unit, compiled apart
 * (core_modelar.cmake).
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 0.3's: "0.3" and FM-1_42000300
 * (fm1.toml id_prefix FM-1_42 + 00 03 00; README: "answers the identity request with FM-1_42000300").
 * Left out, as in every core: the splash (APP_SPLASH: ui.c app_splash, the logo; plat_glue.h starts the UI at
 * power-on), the render's own time (simulated time), USB audio in (fm1.toml usb_audio_in: the device's UAC).
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47) and the remote control (7D 4C 43, this
 * fm1-base's: no lease); no app_sysex (app.h has no APP_SYSEX); then, every pass, ota_service (the M-UPGRADE
 * handshake); usb_cmd has nothing (no backup, no mirror, no APP_USB_CMD). Here the same, in that order, from the
 * submodule's files. The log readout says what an emulated unit has: no crash recorded, then "end". The update key
 * F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): an installer gets the identity, no read
 * requests (it times out), and nothing is written. */
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
#error "APP_VERSION: build.py --release's (core_modelar.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_modelar.cmake)"
#endif
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"
#include "storage.c"

/* felucca-bsp's chip/ota.c on the glue's transport (plat_sx.h), with unity.c's hooks on the core's HAL: the device's
 * ms, the wait (the watchdog fed: the coroutine yields), the update's staging area (OTA_AREA; backup.c keeps its
 * journal record there) on the NOR image. The update's own steps (ota_session: its screen, its commit) are never
 * reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/MODELAR/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_md_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_md_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_md_ota_in(off, n))
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

#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading): not sent */
{
    return fm1core_pl_sx_send(p, n);
}
#include "panel.c"
#include "radio_none.c"
#include "plat_fm1.c"
#include "app.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_md_log_line(const char *t)
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
static int fm1core_md_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_md_log_line("no crash recorded");
    fm1core_md_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 */
static int fm1core_md_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c fm1_main, each pass: a frame one service took is done; a frame none took goes to ota_service (the
 * device runs ota_service every pass; with no frame it has nothing to do) */
static void fm1core_md_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n)) {
        if (ann_frame(p, n, fm1_ms) || fm1core_md_log_frame(p, n) || fm1core_fb_remote_frame(p, n))
            ota_frame_done();
        else
            ota_service();                /* (usb_cmd: nothing in this build) */
    }
}
#define FM1CORE_PL_SX_ACCEPT fm1core_md_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_md_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_pl_flash_boot(void)  /* fm1_main: nothing (no backup.c: no restore record) */
{
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* (ui.c: the synth, the settings, the slot last used; the volumes at 0) */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block (USB audio full: no USB audio) */
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
/* the lazy save: MODELAR has none (SAVE writes the patch into its slot, straight into the data area through store.c's
 * plat_data_write; the settings are a store object, written as they change): nothing is pending */
static int fm1core_pl_sync(void) { return 1; }
static void fm1core_pl_info(char *buf, uint32_t n)   /* the page, the routing, the four volumes, the clock, the slot */
{
    snprintf(buf, n, "page %s algo %d vol %d %d %d %d playing %d slot %d", PAGE_NAME[ui.page], (int)patch.algo,
             (int)vol[0], (int)vol[1], (int)vol[2], (int)vol[3], (int)eng.playing, (int)sys.slot + 1);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
