/* SPDX-License-Identifier: GPL-3.0-only */
/* The PATCHWERK core: PATCHWERK 1.0.3 by Matt Kuebrich (https://github.com/MattKuebrich/patchwerk, the pinned,
 * unmodified submodule cores/patchwerk: GPL-3.0-only, with fm1-base and felucca-bsp (MPL-2.0) vendored in it under
 * base/, its patches/ already applied) built with the fm1-base glue (core-glue/fm1base/README.md), from FRASIL's glue
 * (core_frasil.c: the same fm1-base generation, felucca-bsp's chip/ota.c, the service's coroutine, lib/touch.c) and
 * MODELAR's (core_modelar.c: no mid LED plane, the two-argument mirror stand-ins). The include list follows fm1-base's
 * device unity build (base/device/unity.c) with the hardware left out: plat.h, the app's header, touch.h (APP_TOUCH),
 * gfx.c, fm1-base's defaults (fm1b_config.h), the platform's screen faces (sys_text.c: PATCHWERK's, APP_SYS_TEXT), the
 * store (storage.c on the NOR image, the app's APP_STORE_MAP: the eight user patches, the panel calibration, the
 * settings; the sample bank in the data area between them, plat_data_erase / plat_data_write), the updater's framing
 * (felucca-bsp's chip/ota.c: the M-UPGRADE identity, and the host reads of the sample bank's commands), the screen
 * reader (announce.c), the panel calibration (panel.c), the build with no radio (radio_none.c: fm1.toml has no ble),
 * the device's plat.h implementation (plat_fm1.c), the keys' velocity and pressure (lib/touch.c, its host part) and the
 * app (app.c). The synth (app/core: engine, modules, presets, the live link, the sample bank, the UI) is its own units
 * (core_patchwerk.cmake, fm1.toml "o2", "os", "oz").
 *
 * app.c is compiled with PLAT_HOST, as fm1-base's host simulator and browser build compile it (build.py: the unity and
 * the o2 units alike, so the two halves agree): its device-only parts are the hardware's (the second core's mailbox,
 * cpu1/entry.S; the battery's converter; the linker's budget symbols; the boot probe of the XIP image; its own memmove
 * and strlen for a toolchain with no C library), and the host's stand-ins say what an emulated unit has (no second
 * core, no battery reading, the engine's plan published at once: pw_plat_audio_running 0, the render and the UI on one
 * thread here). On the host the first screen is the rack (pw_ui_init_noboot), as the simulator shows it. The platform's
 * own screens in PATCHWERK's faces (APP_SYS_TEXT: app.c's app_sys_* are its device part) are given here, the same
 * statements over pw_ui.c's pw_sys_*.
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 1.0.3's: "1.0.3" and
 * FM-1_606010003 (fm1.toml id_prefix FM-1_606 + 01 00 03), from core_patchwerk.cmake.
 * Left out, as in every core: the splash (APP_SPLASH, APP_SPLASH_FRAME: plat_glue.h starts the UI at power-on), the
 * render's own time (APP_OVERLOAD's shedding: simulated time), the screen mirror (fm1.toml mirror = true: device/
 * mirror.c, the web page's view of a unit over USB; unity.c's stand-ins), USB audio both ways (AUDIO IN hears
 * nothing), the second core (APP_CPU1, DIAGNOSTICS > CPU: it never starts).
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47), the remote control (7D 4C 43, this
 * fm1-base's: no lease) and the app's own (app_sysex, 7D 4E 4C: the web page's live link, core/pw_link.c), then, every
 * pass, ota_service (the M-UPGRADE handshake) and usb_cmd: the app's commands in the update's framing (APP_USB_CMD,
 * cmd 0x60: the sample bank, core/pw_bank.c, whose uploads read the host through plat_host_read, ota.c's read
 * requests). Here the same, in that order, from the submodule's files; an upload waits as on the device: the service
 * runs on a coroutine (FM1CORE_PL_SX_CORO) that yields where the device feeds its watchdog. The log readout says what
 * an emulated unit has: no crash recorded, then "end". The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7
 * are not taken (plat_sx.h): an installer gets the identity, no read requests (it times out), and nothing is
 * written. */
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
#error "APP_VERSION: build.py --release's (core_patchwerk.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_patchwerk.cmake)"
#endif
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_PL_SX_CORO 1              /* (the sample bank's uploads wait for the host: the service's coroutine) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#include "touch.h"
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"
#include "storage.c"

/* felucca-bsp's chip/ota.c on the glue's transport (plat_sx.h), with unity.c's hooks on the core's HAL: the device's
 * ms, the wait (the watchdog fed: the coroutine yields), the update's staging area (OTA_AREA) on the NOR image. The
 * update's own steps (ota_session: its screen, its commit) are never reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/patchwerk/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_pw_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_pw_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_pw_ota_in(off, n))
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
void plat_present(void) {}                /* (unity.c without FM1B_MIRROR: no mirror marks the frame) */
int plat_usb_powered(void) { return 1; }  /* (unity.c: a USB host there; here the host is always there) */
#define PLAT_HOST 1                       /* lib/touch.c's host part (app_host_key / app_host_tick) and app.c's host
                                           * build (above) */
#include "touch.c"
#include "app.c"
#undef PLAT_HOST
/* app.c's APP_SYS_TEXT part (its device build's): the platform's own screens in PATCHWERK's faces (pw_ui.c) */
int pw_sys_h(int face);
int pw_sys_w(int face, const char *s);
void pw_sys_text(int x, int y, int face, const char *s, void (*plot)(int, int, unsigned));
static uint16_t fm1core_pw_sys_col;
static void fm1core_pw_sys_plot(int x, int y, unsigned a)
{
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1)
        return;
    uint16_t *p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 15u) {
        *p = swap16(fm1core_pw_sys_col);
        return;
    }
    uint32_t bg = swap16(*p), al = (a * 32u + 7u) / 15u;
    uint32_t b = (bg | (bg << 16)) & 0x07E0F81Fu,
             f = (fm1core_pw_sys_col | ((uint32_t)fm1core_pw_sys_col << 16)) & 0x07E0F81Fu;
    uint32_t r = ((f * al + b * (32u - al)) >> 5) & 0x07E0F81Fu;
    *p = swap16(r | (r >> 16));
}
static int32_t app_sys_h(int face) { return pw_sys_h(face); }
static int32_t app_sys_w(int face, const char *s) { return pw_sys_w(face, s); }
static void app_sys_text(int32_t x, int32_t y, int face, const char *s, uint16_t c)
{
    fm1core_pw_sys_col = c;
    pw_sys_text(x, y, face, s, fm1core_pw_sys_plot);
}
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_pw_log_line(const char *t)
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
static int fm1core_pw_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_pw_log_line("no crash recorded");
    fm1core_pw_log_line("end");
    return 1;
}
/* main_fm1.c usb_cmd: a frame in the update's framing that ota_service did not take: the app's (APP_USB_CMD: the
 * sample bank; no backup, the mirror not built) */
static void fm1core_pw_usb_cmd(uint32_t d)
{
    if (d < 7u || ota_dec[2] == 0x11u)
        return;
    app_usb_cmd(ota_dec, d);
    ota_try_ms = 5000u;                   /* (the app's patience, plat_host_patience, back to the default) */
    ota_deadline = 0;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (PATCHWERK's fm1-base: no lease) */
static int fm1core_pw_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u) || fm1core_pl_sx_is(f, n, 0x4Eu, 0x4Cu);
}
/* main_fm1.c fm1_main, each pass: a frame the screen reader, the log readout, the remote control or the app took is
 * done; then ota_service (the M-UPGRADE handshake) and usb_cmd */
static void fm1core_pw_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || fm1core_pw_log_frame(p, n) || fm1core_fb_remote_frame(p, n) ||
                                    app_sysex(p, n)))
        ota_frame_done();
    fm1core_pw_usb_cmd(ota_service());
}
#define FM1CORE_PL_SX_ACCEPT fm1core_pw_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_pw_sx_service()
#define FM1CORE_PL_SX_WORK() (fm1core_pl_sxi.ready)

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_fb_sense_tick(void)  /* the keys' readings, while plat_sense is on (the converter is the scan's) */
{
    if (fm1_an.con)
        app_host_tick();
}
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
/* the keys' velocity and pressure (APP_TOUCH): the panel's key_vel / key_lean become the readings the scan would hand
 * the app, through lib/touch.c's own host model (as core_amb.c) */
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

static void fm1core_pl_flash_boot(void)  /* fm1_main: nothing (no backup.c: no restore record) */
{
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* the engine, the sample bank's directory, the live link */
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
/* the lazy save: PATCHWERK has none (SAVE writes the patch into its slot, the settings and the sample bank are written
 * as they change): nothing is pending */
static int fm1core_pl_sync(void) { return 1; }
static void fm1core_pl_info(char *buf, uint32_t n)   /* the page, the modules in the patch, the voices */
{
    snprintf(buf, n, "page %d modules %d voices %u", pw_ui_page(), pw_count(), (unsigned)pws.voices);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
