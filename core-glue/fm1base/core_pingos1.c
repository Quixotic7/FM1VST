/* SPDX-License-Identifier: GPL-3.0-only */
/* The PING OS-1 core: PING OS-1 1.0.1 by PING AUDIO (https://github.com/PINGAUDIO/PING-OS-1, the pinned, unmodified
 * submodule cores/PING-OS-1: MPL-2.0, with Felucca's engines (MIT; FM6 MPL-2.0 by grant, its msfa core Apache-2.0) and
 * Mutable Instruments' Plaits / stmlib ports (MIT), on PING AUDIO's fork of fm1-base and felucca-bsp, the submodules
 * base and base/bsp, MPL-2.0) built with the fm1-base glue (core-glue/fm1base/README.md), from HADAL-1's glue
 * (core_hadal1.c: the nearest fm1-base) without backup.c (fm1.toml has no backup), with the app's USB commands
 * (APP_USB_CMD: the sample bank, app/samples.h, as core_golf.c). The include list follows fm1-base's device unity build
 * (base/device/unity.c) with the hardware left out: plat.h, the app's header, gfx.c, fm1-base's defaults
 * (fm1b_config.h), the platform's screen faces (sys_text.c), the store (storage.c on the NOR image, the app's
 * APP_STORE_MAP: the settings, the projects; the sample bank is raw data area, plat_data, read in place), the
 * updater's framing (felucca-bsp's chip/ota.c: the M-UPGRADE identity), the screen reader (announce.c), the panel
 * calibration (panel.c), the build with no radio (radio_none.c, fm1.toml ble = false), the device's plat.h
 * implementation (plat_fm1.c), the second core simulated (below) and the app (app.c: the sequencer, the screens, the
 * library). The engine (engine/ch_*.c) is fm1.toml's "o2" units, compiled apart (core_pingos1.cmake).
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 1.0.1's: "1.0.1" and
 * FM-1_888010001 (fm1.toml id_prefix FM-1_888 + 01 00 01), from core_pingos1.cmake.
 * THE SECOND CORE (fm1.toml cpu2 = true, FM1B_CPU2): each job runs at once, in order, on the host's one thread, as
 * fm1-base's host simulator runs it: the tracks the app gives the second core render "there" and are summed as on a
 * two-core unit.
 * Left out, as in every core: the splash card (APP_SPLASH: app_splash; plat_glue.h starts the UI at power-on; the
 * app's own logo animation, app_frame, still plays), the render's own time (simulated time). USB audio in (fm1.toml
 * usb_audio_in: the FM-1 recorded over USB) and the USB host port (usb_host: MIDI SYNC / SEND DIRECT to another FM-1)
 * are not built: the core's output is the host's audio, its MIDI the host's.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47) and the remote control (7D 4C 43); PING OS-1
 * has no app_sysex; then, every pass, ota_service (the M-UPGRADE handshake) and usb_cmd: the app's commands in the
 * update's framing (APP_USB_CMD, app_usb_cmd: the sample bank 0x50, the pattern 0x51, the project 0x52). The log
 * readout says what an emulated unit has: no crash recorded, then "end". The update key F0 22 24 35 7F F7 and UBOOT's
 * F0 22 24 35 7D F7 are not taken (plat_sx.h). */
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
#error "APP_VERSION: build.py --release's (core_pingos1.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_pingos1.cmake)"
#endif
#define FM1B_CPU2 1                       /* fm1.toml cpu2 = true: the second core, simulated (below) */
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
#include "../../cores/PING-OS-1/base/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_pg_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_pg_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_pg_ota_in(off, n))
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
static volatile uint8_t midi_usb_lost;   /* (usb.c's: the host's link never drops here; plat_usb_lost says 0) */
static void fm1_guard_pc_extend(uint32_t end) { (void)end; }   /* (the guard's code window, plat_code_window: no meaning here) */
#include "plat_fm1.c"
/* the second core (cpu2.c, not built): simulated as fm1-base's host simulator has it (base/host/plat_host.c): a job
 * runs at once, in order, on the one thread; the same sums as the device's split. Never given up */
static int fm1core_pg_cpu2_on;
int plat_cpu2_set(int on) { fm1core_pg_cpu2_on = on; return 0; }
int plat_cpu2_on(void) { return fm1core_pg_cpu2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1core_pg_cpu2_on)
        return 0;
    fn(arg);
    return 1;
}
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }
/* felucca-bsp's power-on LED sweep (bsp/boards/fm1/fm1_input.h), which app_frame starts with the logo's hit: the
 * panel's LEDs are the app's own here, no sweep over them */
static void fm1_led_anim_start(uint32_t glow) { (void)glow; }
/* PING AUDIO's fm1-base fork's timings (plat.h; main_fm1.c measures them on the device): 0, as plat.h says a host
 * gives (simulated time: the LOAD page shows no gaps) */
uint32_t plat_audio_gap_max(void) { return 0; }
uint32_t plat_tick_run_max(void) { return 0; }
uint32_t plat_audio_block_max(void) { return 0; }
#include "app.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_pg_log_line(const char *t)
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
static int fm1core_pg_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_pg_log_line("no crash recorded");
    fm1core_pg_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 */
static int fm1core_pg_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c usb_cmd: a frame in the update's framing that ota_service did not take: no backup, no resources, no
 * mirror in this build; the app's (APP_USB_CMD: the sample bank, the pattern, the project) */
static void fm1core_pg_usb_cmd(uint32_t d)
{
    if (d < 7u || ota_dec[2] == 0x11u)
        return;
    app_usb_cmd(ota_dec, d);
    ota_try_ms = 5000u;                   /* (the app's patience, plat_host_patience, back to the default) */
    ota_deadline = 0;
}
/* main_fm1.c fm1_main, each pass: a frame the screen reader, the log readout or the remote control took is done; then
 * ota_service (the M-UPGRADE handshake) and usb_cmd */
static void fm1core_pg_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || fm1core_pg_log_frame(p, n) || fm1core_fb_remote_frame(p, n)))
        ota_frame_done();
    fm1core_pg_usb_cmd(ota_service());
}
#define FM1CORE_PL_SX_ACCEPT fm1core_pg_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_pg_sx_service()
#define FM1CORE_PL_SX_WORK() (fm1core_pl_sxi.ready)

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
    app_init();                           /* (the engine, the second core, the saved settings and project, or the factory one) */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block, without USB audio */
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
/* nothing pending: SAVE (held) and the settings write at once, as on the device */
static int fm1core_pl_sync(void) { return 1; }
static void fm1core_pl_info(char *buf, uint32_t n)   /* the transport, the pattern, the view */
{
    const ch_snapshot *sn = ch_snapshot_get();
    snprintf(buf, n, "playing %d beat %.2f pattern %d view %d voices %d cores %d", sn->playing, (double)sn->beat,
             ui.cur_pat, (int)ui.view, sn->voices, plat_cpu2_on() ? 2 : 1);
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
