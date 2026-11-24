/* SPDX-License-Identifier: GPL-3.0-only */
/* The h1 core: h1 (https://github.com/jakehandy/h1, the pinned, unmodified submodule cores/h1, tag v0.3.0 = 4cff936,
 * MIT, with its submodules base = fm1-base and base/bsp = felucca-bsp, MPL-2.0) built as one unit with the fm1-base
 * glue (core-glue/fm1base/README.md). h1's fm1-base is c8e0599, SY1's: this unit follows core_sy1.c (the second core
 * simulated) without the app's own SysEx (h1 has none) and without lib/touch.c (no APP_TOUCH). The include list
 * follows fm1-base's device unity build (base/device/unity.c) with the hardware left out: plat.h, the app's header,
 * gfx.c, fm1-base's defaults (fm1b_config.h), the platform's screens (sys_text.c), the store (storage.c on the NOR
 * image, the app's APP_STORE_MAP: OBJ_STATE, OBJ_PANEL), the updater's framing (felucca-bsp's chip/ota.c: the
 * M-UPGRADE identity), the screen reader (announce.c), the panel calibration (panel.c), the build with no radio
 * (radio_none.c, fm1.toml ble = false), the device's plat.h implementation (plat_fm1.c) and the app (app/app.c: the
 * panel in; the sound, the lights and the picture's changed rectangles out). The instruments, the sequencer and the
 * drawing are fm1.toml's "o2" units (core/*.c), their own units.
 *
 * app.c's three reaches past plat.h, on the device's paths (PLAT_HOST off), with the host's answers:
 *   - app/hw/panel.h hw_panel_done (FM1_LCD_SPI_CON's done bit) and device/lcd.c's lcd_busy: the plat HAL's lcd_blit
 *     writes the panel's RAM at once, so no transfer is ever running (lcd_busy 0, the done bit set);
 *   - main_fm1.c's bootguard.failed (a start after one that died in its first 30 s leaves the saved state alone): the
 *     plat glue's boot guard (fm1core_pl_guard.failed, set by fm1core_pl_boot_options), copied in before app_init.
 *
 * THE SECOND CORE (fm1.toml cpu2 = true: every other voice on it, h1_two_cores): as LOOM-8's glue (loom_plat.c) and
 * fm1-base's host simulator (host/plat_host.c) run it, a job runs at once, in order, on the host's one thread,
 * plat_core() 1 while it runs; it is never given up or barred. The render's sums are the same as on a two-core FM-1.
 * fm1-base's device half (cpu2.c) is not built; plat_fm1.c's plat_core is renamed away.
 *
 * h1's version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 0.3.0's: "0.3.0" and
 * FM-1_481000300 (fm1.toml id_prefix FM-1_481 + 00 03 00); core_h1.cmake derives both from its VERSION.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives each SysEx
 * frame to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47: "no crash recorded", then "end") and
 * the remote control (7D 4C 43), then runs ota_service (the M-UPGRADE handshake; usb_cmd has nothing in this build:
 * no backup). The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h). */
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
#error "APP_VERSION: build.py --release's (core_h1.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_h1.cmake)"
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
#include "../../cores/h1/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_h1_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_h1_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_h1_ota_in(off, n))
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
#define plat_core fm1core_h1_dev_core    /* (the device's, fm1_cnum: the simulated second core's below) */
#include "plat_fm1.c"
#undef plat_core
#undef SXQ
#undef sx_out_q
#undef so_w
#undef so_r

/* the second core, simulated (plat_host.c's, as loom_plat.c): a job runs at once, in order; never given up */
static int fm1core_h1_c2_on, fm1core_h1_c2_in_job;
int plat_cpu2_set(int on)
{
    fm1core_h1_c2_on = on != 0;
    return 0;
}
int plat_cpu2_on(void) { return fm1core_h1_c2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1core_h1_c2_on)
        return 0;
    fm1core_h1_c2_in_job = 1;
    fn(arg);
    fm1core_h1_c2_in_job = 0;
    return 1;
}
uint32_t plat_core(void) { return (uint32_t)fm1core_h1_c2_in_job; }
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }   /* (a job ran at once) */
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }
/* the stacks' room (device/stacks.c, fm1.toml crash_history = true; app.c's PERF readout): not measured on the host, as
 * main_fm1.c's own stand-in without it */
int32_t plat_stack_left(int k)
{
    (void)k;
    return -1;
}

/* app.c's reaches past plat.h (the header above): the panel's transfer is never running (the HAL's lcd_blit is done
 * when it returns: device/lcd.c's lcd_busy 0, app/hw/panel.h's done bit set); main_fm1.c's boot guard is the plat
 * glue's, copied in by fm1core_pl_app_boot before app_init */
#define lcd_busy 0
#define FM1_LCD_SPI_CON 0x8000u
static struct {
    uint32_t failed;
} bootguard;
static void fm1core_h1_guard_in(void);
#include "app.c"                          /* (the device's paths: PLAT_HOST off) */
#undef lcd_busy
#undef FM1_LCD_SPI_CON
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7: "no crash
 * recorded", the app's own lines (APP_LOG: the render's load, the second core's state), "end" last */
static void fm1core_h1_log_line(const char *t)
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
static int fm1core_h1_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_h1_log_line("no crash recorded");
#ifdef APP_LOG
    app_log(fm1core_h1_log_line);
#endif
    fm1core_h1_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base's: no lease) */
static int fm1core_h1_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c fm1_main, each pass: a frame one service took is done; a frame none took goes to ota_service (usb_cmd:
 * nothing in this build) */
static void fm1core_h1_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n)) {
        if (ann_frame(p, n, fm1_ms) || fm1core_h1_log_frame(p, n) || fm1core_fb_remote_frame(p, n))
            ota_frame_done();
        else
            ota_service();
    }
}
#define FM1CORE_PL_SX_ACCEPT fm1core_h1_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_h1_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
/* the live knob labels and readouts (common/rd_cols.h): h1's own per-encoder view (core/h1.h h1_control: each
 * encoder's name and value text as the screen has them; the H1_ENC_* order is FM1P_E_*'s), copied after each UI frame */
#include "rd_cols.h"
static void fm1core_h1_rd(void)
{
    int e;
    rdc_begin();
    for (e = 0; e < H1_NENC; e++) {
        int lo, hi;
        const char *nm = "", *tx = "";
        (void)h1_control(e, &lo, &hi, &nm, &tx);   /* (it returns the value) */
        rdc_knob(e, nm, tx);
    }
}
#define FM1CORE_PL_UI_FRAME() (app_frame(), fm1core_h1_rd())
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights_mid's steady mid level */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_pl_flash_boot(void)  /* fm1_main: nothing (no backup.c: no restore record) */
{
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    fm1core_h1_guard_in();
    app_init();                           /* the instruments, the second core, the saved state (not after a bad start) */
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
/* the lazy save now: app_frame's save (the state changed and no voice sounding: saving erases flash) without its
 * wait; while a voice sounds: deferred */
static int fm1core_pl_sync(void)
{
    uint32_t size;
    const void *p;
    if (!h1_state_dirty())
        return 1;
    if (h1_voices_active())
        return 0;
    p = h1_state(&size);
    if (plat_store_save(OBJ_STATE, p, size) == 0)
        h1_state_saved();
    return !h1_state_dirty();
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "instrument %s algorithm %s page %d layers %d dirty %d", h1_instrument_name(h1_instrument()),
             h1_algo_name(h1_instrument(), h1_algorithm()), h1_page(), h1_layers(), h1_state_dirty());
}

#include "../plat/plat_glue.h"
static void fm1core_h1_guard_in(void) { bootguard.failed = fm1core_pl_guard.failed; }   /* (app.c's bootguard) */
#include "exit_end.h"
