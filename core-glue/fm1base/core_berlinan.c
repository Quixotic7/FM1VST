/* SPDX-License-Identifier: GPL-3.0-only */
/* The Berlinan core: Berlinan 0.5.1 for the FM-1 by Jiří Píša (https://github.com/jiripisa/fm1-berlinan, the pinned,
 * unmodified submodule cores/fm1-berlinan, GPL-2.0-or-later, with its own submodules base = fm1-base and base/bsp =
 * felucca-bsp, MPL-2.0) built with the fm1-base glue (core-glue/fm1base/README.md), from AMB-1's (core_amb.c: the same
 * fm1-base, 15dff6d). The include list follows fm1-base's device unity build (base/device/unity.c) with the hardware
 * left out: plat.h, the app's header, gfx.c, fm1-base's defaults (fm1b_config.h), the store (storage.c on the NOR
 * image, the app's APP_STORE_MAP: the project, the panel calibration, the six slots), the screen reader
 * (announce.c), the panel calibration (panel.c), the build with no radio (radio_none.c, fm1.toml ble = false), the
 * device's plat.h implementation (plat_fm1.c) and the app (app.c: the transport, the project, the panel, the screen).
 * The engine (app/engine/*.c and Berkeley SoftFloat, app/engine/softfloat/*.c) is fm1.toml's "o2" units, compiled
 * apart (core_berlinan.cmake). app.c's device-only libc (app/libc/libc.c: memmove, strcmp, abs, the 64-bit divides,
 * for the freestanding JieLi toolchain) is read as an empty file through a clang VFS overlay (berlinan_nolibc.c):
 * the core has its own libc.
 *
 * Berlinan makes no sound: app_render writes silence; the sequencer's MIDI (notes, CC, the clock, Start / Stop) goes
 * out through plat_midi_out to the USB-MIDI out ring (fm1base_hal.h), and the host's MIDI in is plat_midi_in's (EXT
 * clock from USB or TRS).
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release 0.5.1's: "0.5.1" and
 * FM-1_23000501 (fm1.toml id_prefix FM-1_23 + 00 05 01, README), from core_berlinan.cmake.
 * Left out, as in every core: the splash (plat_glue.h starts the UI at power-on).
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (base/device/main_fm1.c fm1_main) gives a SysEx frame
 * to the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47) and the remote control (7D 4C 43, this
 * fm1-base's: no lease); Berlinan has no app_sysex (no APP_SYSEX) and no WiFi; then ota_service (the M-UPGRADE
 * handshake: felucca-bsp's chip/ota.c through common/sysex_ota.h with the package identity). Here the same, in that
 * order. The log readout says what an emulated unit has: no crash recorded, then "end". The update key
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
#error "APP_VERSION: build.py --release's (core_berlinan.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_berlinan.cmake)"
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
#include "storage.c"
#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), not sent */
{
    return fm1core_pl_sx_send(p, n);
}
#include "panel.c"
#include "radio_none.c"
#include "plat_fm1.c"
#include "app.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
#define FM1SX_OTA_C "../../cores/fm1-berlinan/base/bsp/chip/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_bn_log_line(const char *t)
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
static int fm1core_bn_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_bn_log_line("no crash recorded");
    fm1core_bn_log_line("end");
    return 1;
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base: no lease) */
static int fm1core_bn_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c fm1_main, each pass: the screen reader, the log readout, the remote control; a frame none of them took
 * goes to ota_service (the M-UPGRADE handshake; the update key's ota_req: not taken) */
static void fm1core_bn_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (fm1sx_frame(&fm1core_pl_sxi, &p, &n)) {
        if (ann_frame(p, n, fm1_ms) || fm1core_bn_log_frame(p, n) || fm1core_fb_remote_frame(p, n))
            fm1sx_done(&fm1core_pl_sxi);
        else
            ota_service();
    }
}
#define FM1CORE_PL_SX_ACCEPT fm1core_bn_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_bn_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain: none, it makes no sound) */
/* the 1 kHz tick (APP_TICK: the sequencer's clock, MIDI in and out), in the timer interrupt on the device */
#define FM1CORE_PL_TICK_HOOK()                                                                                 \
    do {                                                                                                       \
        in_tick = 1;                                                                                           \
        app_tick();                                                                                            \
        in_tick = 0;                                                                                           \
    } while (0)

static void fm1core_pl_flash_boot(void) {}
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* the transport, then the saved project (or the Demo trio) */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n) { app_render(lr, n, APP_GAIN(master_pos)); }   /* silence */
static void fm1core_pl_master(int32_t knob)   /* main_fm1.c: MASTER's position 0..4092 (Berlinan's seed reads it) */
{
    fm1core_fb_master_adc = knob / 16;
    if (!fm1_an.con)
        fm1_an.master = (uint16_t)fm1core_fb_master_adc;
    master_pos = remote.master >= 0 ? (uint32_t)remote.master : (uint32_t)(knob / 4);
}
static void fm1core_pl_loop(void)
{
    ble_poll();
    app_loop();                           /* APP_LOOP: the sequencer renders ahead into its queue */
}
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        app_say("UPDATE MODE IN", d);
    } else {
        app_say("UPDATE MODE", "CANCELLED");
    }
}
/* the lazy save: Berlinan has none (only SAVE writes; pressed while playing, it waits for the stop, project.c
 * proj_stopped): pending until then */
static int fm1core_pl_sync(void) { return !proj_save_pending(); }
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "view %u template %u bpm %d seed %08x playing %d seeded %d save-pending %d", (unsigned)ui.view,
             (unsigned)proj.tpl, (int)proj.bpm, (unsigned)(uint32_t)proj.seed, rt_playing(), proj_seeded(),
             proj_save_pending());
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
