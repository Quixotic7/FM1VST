/* SPDX-License-Identifier: GPL-3.0-only */
/* The PERF-1 core: PERF-1 0.16.0 by deamonSLINKY (https://github.com/deamonSLINKY/PERF-1, the pinned, unmodified
 * submodule cores/PERF-1 at v0.16.0 = 22c295d), a fork of HYP-1 (RedtailWorks), itself a fork of AMB-1 (Charles
 * Vestal) on the plat layout, built as one unit with the plat glue (core-glue/plat/README.md), from HYP-1's glue.
 * The include list follows the device's unity build (firmware/src/main.c) with the hardware left out, as the
 * firmware's own host simulator (host/amb_host.c) does: gfx.c, plat.h, Felucca's storage.c (on the NOR image), the
 * app's header, the panel calibration (panel.c), the editor's and the backup's frames (dx_proto.h, backup_proto.h),
 * the device's plat.h implementation (plat_fm1.c), the project and the UI (with its runner and visualizers). The
 * instrument (dsp/amb.c, with the FM6 engine) is its own unit (perf1_amb.c).
 *
 * What the device has beyond HYP-1, and how it is taken here:
 *   - the second core (EN_CORE2, fm1_core1.h): off; the reverb runs inline (the engine's own fallback), the load
 *     reads 0 % (simulated time), plat_core2() says "not in this build"
 *   - the USB MIDI host (PERF_USB_HOST, usb_host.c): off (plat_fm1.c's own stubs); the MIDI IN page offers it and
 *     nothing answers, as with no device on the port
 *   - the sliced flash erase (the audio goes on through a write, plat_store_seamless): the NOR image's erase stall
 *     is set to 0 ms, so no store is faded and none is heard
 *   - the keys' second dim plane (fm1_led_dim2: the scale dim while a chord plays bright): plat_glue.h's optional
 *     FM1CORE_PL_DIM2_PLANE
 *   - its SysEx (usb.c USB_DX, main_fm1.c dx_service / px_service): the web editor (F0 7D 50 50 ..: parameters, a
 *     dump of all of them, a voice read back), the player's DX7 voices (a 32-voice bulk, a single voice, ours
 *     F0 7D 50 56 ..) and the installer's BACKUP AND RESTORE (F0 7D 50 42 ..) are answered by the firmware's own
 *     code: usb.c's frame sorting (px_end / dx_end) and main_fm1.c's two services, copied below unchanged (they sit
 *     in files that are mostly hardware), on plat_sx.h's transport. The M-UPGRADE identity is ota.c's.
 *     The telemetry and the bench (F0 7D 50 46 ..) are left out (a host's load is not the chip's).
 * Its version is a build-time define (EN_VERSION, "DEV" by default): the glue's VERSION (core_perf1.cmake). */
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

#define EN_HOST 1                         /* the device's .pool section off (plat.h EN_POOL) */
#define EN_VERSION FM1CORE_VERSION
#define FELUCCA_ID "FM-1_7001601"         /* the package identity: build.py --release 0.16.1 (FM-1_7 00 16 00) */
#define EN_CORE2 0                        /* the second core: not in this build (plat_fm1.c plat_core2) */
#define PERF_USB_HOST 0                   /* the USB MIDI host: plat_fm1.c's stubs */
#define FM1CORE_PL_SX 1                   /* the editor, the voices, the backup, the M-UPGRADE identity (plat_sx.h) */
#define FM1SX_IN_MAX 4112u                /* usb.c DX_MAX: a 32-voice bulk dump is one frame */
#include "plat_hal.h"
#include "plat_sx.h"

/* the HAL beyond plat_hal.h that plat_fm1.c reads */
static uint8_t fm1_led_dim2[FM1P_NCOL];   /* the keys' dim plane (plat_keys_dim) */
static volatile uint32_t fm1_led_dim2_div; /* its duty, one scan frame in N (0: off) */
static uint8_t susp_bad;                  /* main.c: the part suspends an erase (the sliced erase works): 0 */
static uint32_t fm1_core1_load(void) { return 0; }
static int fm1_core1_online(void) { return 0; }
static void usb_detach(void) {}
static void fm1_reboot(void) { exit(2); } /* (a restore's restart: the core halts as the unit resets) */

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator) */
#include "gfx.c"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"

/* usb.c (USB_DX 1, v0.16.0 lines 594..654), unchanged but for the .pool section: the editor's short messages caught
 * whole into px_q (px_end), and the DX7 voices and a restore's chunks into dx_buf (dx_end). usb.c's sysex_byte feeds them byte by byte; here the whole frame the
 * transport collected is given to them at once (perf1_sx_frame). */
#define PX_N 32u
static uint32_t px_q[PX_N];
static volatile uint32_t px_w, px_r;
static uint8_t px_buf[16];
static uint32_t px_pos;
static uint8_t px_on;
static void px_end(void)
{
    const uint8_t *m = px_buf;
    const uint32_t n = px_pos;
    uint32_t e = 0;
    px_on = 0;
    if (n == 8u && m[0] == 0x7Du && m[1] == 0x50u && m[2] == 0x50u && m[3] == 0x01u)
        e = 1u << 28 | (uint32_t)((m[4] << 7 | m[5]) & 0x3FFF) << 14 | (uint32_t)((m[6] << 7 | m[7]) & 0x3FFF);
    else if (n == 4u && m[0] == 0x7Du && m[1] == 0x50u && m[2] == 0x50u && m[3] == 0x02u)
        e = 2u << 28;
    else if (n == 5u && m[0] == 0x7Du && m[1] == 0x50u && m[2] == 0x56u && m[3] == 0x7Eu)
        e = 3u << 28 | (uint32_t)(m[4] & 0x7Fu);
    else if (n == 5u && m[0] == 0x7Du && m[1] == 0x50u && m[2] == 0x42u && m[3] == 0x01u)         /* backup: read an object */
        e = 4u << 28 | (uint32_t)(m[4] & 0x7Fu);
    else if (n == 7u && m[0] == 0x7Du && m[1] == 0x50u && m[2] == 0x42u && m[3] == 0x04u)         /* restore: the object is complete, write it */
        e = 5u << 28 | (uint32_t)(m[4] & 0x1Fu) << 14 | (uint32_t)((m[5] << 7 | m[6]) & 0x3FFF);
    else if (n == 4u && m[0] == 0x7Du && m[1] == 0x50u && m[2] == 0x42u && m[3] == 0x05u)         /* restore: restart */
        e = 6u << 28;
    if (e && px_w - px_r < PX_N) {
        px_q[px_w % PX_N] = e;
        RING_PUBLISH();
        px_w++;
    }
}
#define DX_MAX 4112u
static uint8_t dx_buf[DX_MAX];
static uint32_t dx_pos;
static uint8_t dx_on;
static volatile uint8_t dx_state;                /* 0 free, 2 a message is ready */
static volatile uint8_t dx_kind;                 /* 1 bulk, 2 single, 3 ours, 4 ours: clear */
static void dx_end(void)
{
    const uint8_t *m = dx_buf;
    const uint32_t n = dx_pos;
    uint8_t kind = 0;
    if (n >= 5u && m[0] == 0x43u && (m[1] & 0xF0u) == 0u) {
        if (m[2] == 0x09u && m[3] == 0x20u && m[4] == 0x00u && n == 4102u)
            kind = 1;
        else if (m[2] == 0x00u && m[3] == 0x01u && m[4] == 0x1Bu && n == 161u)
            kind = 2;
    } else if (n >= 4u && m[0] == 0x7Du && m[1] == 0x50u && m[2] == 0x56u) {
        if (n == 133u && m[3] < 16u)
            kind = 3;
        else if (n == 4u && m[3] == 0x7Fu)
            kind = 4;
    } else if (n >= 12u && m[0] == 0x7Du && m[1] == 0x50u && m[2] == 0x42u && m[3] == 0x03u) {   /* a chunk of a restore (backup_proto.h) */
        kind = 5;
    }
    dx_on = 0;
    if (kind) {
        dx_kind = kind;
        RING_PUBLISH();
        dx_state = 2;
    }
}
/* one whole frame (the bytes between F0 and F7) through usb.c sysex_byte's two collectors: px_buf takes up to 16
 * bytes, dx_buf up to DX_MAX while no message waits in it */
static void perf1_sx_frame(const uint8_t *f, uint32_t n)
{
    if (n <= sizeof px_buf) {
        memcpy(px_buf, f, n);
        px_pos = n;
        px_end();
    }
    if (!dx_state && n <= DX_MAX) {
        memcpy(dx_buf, f, n);
        dx_pos = n;
        dx_end();
    }
}
/* usb.c usb_sysex_try: one SysEx F0..F7 if there is room for it all at once, else nothing */
static int usb_sysex_try(const uint8_t *p, uint32_t n) { return fm1sx_send(&fm1core_pl_sxo, p, n); }

#include "storage.c"
#include "app/app.h"
#include "app/panel.c"
#include "dx_proto.h"
#include "backup_proto.h"
/* plat_fm1.c plat_seed reads the MASTER ADC at power-on for its noise; in a core the pot's reading comes through the
 * ABI's input (plat_glue.h), after boot: a fixed mid reading here, the ticks still vary the seed */
#define FM1_ADC_MASTER 0
static int32_t fm1_adc_read(int ch) { (void)ch; return 512; }
#include "app/plat_fm1.c"
#include "app/project.c"
#include "app/ui.c"

/* main_fm1.c (v0.16.0, lines 412..554), unchanged but for the .pool sections: BACKUP AND RESTORE's state, THE PLAYER'S DX7 VOICES (dx_service)
 * and THE EDITOR (px_service) */
/* BACKUP AND RESTORE (backup_proto.h; the installer page's buttons): an object of flash read out in frames, or written from chunks. The chunks gather in bk_buf,
 * the object is checked as its loader would take it, and written when the page says it is complete; after any write nothing else is stored (plat_store_restore)
 * and the page asks for a restart, which loads everything afresh. */
static uint8_t bk_buf[BK_MAX];
static bk_sess_t bk_sess;
static uint8_t bk_restored;                      /* an object was written: a restart may follow */
static uint8_t bk_ack[8], bk_ack_n;              /* an answer waiting for room in the USB queue */
static uint32_t bk_rd_obj = 0xFFFFFFFFu, bk_rd_i, bk_rd_len;   /* an object being sent: which, how far, how long */
static uint32_t bk_wait_obj = 0xFFFFFFFFu;       /* an object being written (the music fading out): which */
static int bk_obj_ok(uint32_t obj) { return obj < OBJ_NOBJ && obj != OBJ_PANEL; }
static void bk_ack_put(uint32_t obj, uint32_t idx, uint32_t status) { bk_ack_n = (uint8_t)bk_build_ack(bk_ack, obj, idx, status); }

/* THE PLAYER'S DX7 VOICES. usb.c collects a DX7 SysEx (a 32-voice bulk dump: the first EN_NUSER voices replace the loaded ones; a single voice, appended; or
 * ours, F0 7D 50 56 slot 128-bytes checksum F7: one slot, or 7F alone to clear), here it is checked and handed to the engine, and the UI keeps them. */
static void dx_service(void)
{
    uint32_t i, k, sum = 0;
    uint8_t b[128], ack[7];
    int kind, r;                                     /* r: the voices taken; -1 a bad checksum; -2 not taken */
    if (dx_state != 2u)
        return;
    RING_PUBLISH();
    kind = dx_kind;
    if (kind == 5) {                                 /* a chunk of a restore: taken into bk_buf, answered */
        bk_chunk_t c;
        uint8_t f[8];
        int st = bk_parse_chunk(dx_buf, dx_pos, &c);
        const uint32_t obj = dx_pos > 4u ? dx_buf[4] & 0x7Fu : 0u;
        if (st == 0 && (!bk_obj_ok(c.obj) || ui_restore_state() == 1 || bk_accept(&bk_sess, bk_buf, &c) != 0))
            st = 2;
        dx_state = 0;
        usb_sysex_try(f, bk_build_ack(f, obj, st == 0 ? c.idx : 0x7Eu, (uint32_t)st));
        return;
    }
    if (kind == 1) {                                 /* bulk: 5 header bytes, 4096 data, the checksum */
        for (i = 0; i < 4096u; i++)
            sum += dx_buf[5u + i];
        if (((sum + dx_buf[5u + 4096u]) & 0x7Fu) != 0u) {
            r = -1;
        } else {
            en_user_clear();
            for (r = 0, k = 0; k < EN_NUSER; k++)
                r += en_user_voice((int)k, dx_buf + 5u + k * 128u);
        }
    } else if (kind == 2) {                          /* a single voice: 155 bytes, packed, appended */
        for (i = 0; i < 155u; i++)
            sum += dx_buf[5u + i];
        if (((sum + dx_buf[5u + 155u]) & 0x7Fu) != 0u) {
            r = -1;
        } else {
            en_voice_pack(dx_buf + 5u, b);
            r = en_user_voice(en_user_next(), b) ? 1 : -2;
        }
    } else if (kind == 3) {                          /* ours: one slot */
        for (i = 0; i < 128u; i++)
            sum += dx_buf[4u + i];
        if (((sum + dx_buf[4u + 128u]) & 0x7Fu) != 0u)
            r = -1;
        else
            r = en_user_voice(dx_buf[3], dx_buf + 4u) ? 1 : -2;
    } else {
        en_user_clear();
        r = 0;
    }
    ui_voices_changed(r, kind);
    ack[0] = 0xF0, ack[1] = 0x7D, ack[2] = 0x50, ack[3] = 0x57, ack[4] = (uint8_t)kind, ack[5] = r < 0 ? 0x7Fu : (uint8_t)r, ack[6] = 0xF7;
    dx_state = 0;                                    /* dx_buf free before the answer goes, so the next voice the answer releases is never dropped */
    usb_sysex_try(ack, sizeof ack);                  /* the answer: the page waits for it before the next voice */
}

/* THE EDITOR (web/editor): parameter changes, a dump of every parameter, a voice read back. Changes are applied as the knobs would; a dump goes out in
 * frames of up to 24 parameters (F0 7D 50 51 first-hi first-lo count, then each value + 8192 as two 7-bit bytes, F7), a frame a turn of the loop as the
 * USB queue has room; a voice goes back as F0 7D 50 58 slot loaded [128 bytes, a checksum] F7 (no bytes when the slot is empty). */
static void px_service(void)
{
    static uint32_t dump_i = 0xFFFFFFFFu, vox_req = 0xFFFFFFFFu;
    uint8_t f[PX_FRAME_MAX];                         /* the longest frame (a voice) fits: dx_proto.h, checked by tests/host/dxproto_test.c */
    while (px_r != px_w) {
        const uint32_t e = px_q[px_r % PX_N];
        RING_PUBLISH();
        px_r++;
        if (e >> 28 == 1u)
            ui_param_set((int)(e >> 14 & 0x3FFFu), (int)(e & 0x3FFFu) - 8192);
        else if (e >> 28 == 2u)
            dump_i = 0;
        else if (e >> 28 == 3u)
            vox_req = e & 0x7Fu;
        else if (e >> 28 == 4u) {                        /* backup: send an object (a restore under way is dropped) */
            const uint32_t obj = e & 0x7Fu;
            if (bk_obj_ok(obj) && bk_rd_obj == 0xFFFFFFFFu && ui_restore_state() != 1) {
                const int n = plat_store_load(obj, bk_buf, BK_MAX);
                bk_sess.mask = 0;
                bk_rd_len = n > 0 ? (uint32_t)n : 0u;
                bk_rd_i = 0;
                bk_rd_obj = obj;
            }
        } else if (e >> 28 == 5u) {                      /* restore: the object is complete: check it, write it */
            const uint32_t obj = e >> 14 & 0x1Fu, len = e & 0x3FFFu;
            if (!bk_obj_ok(obj) || !bk_complete(&bk_sess, obj, len) || ui_restore_state() == 1)
                bk_ack_put(obj, 0x7Fu, 1u);
            else if (!ui_restore_valid(obj, bk_buf, len))
                bk_ack_put(obj, 0x7Fu, 2u);
            else {
                bk_wait_obj = obj;
                ui_restore_start(obj, bk_buf, len);
            }
            bk_sess.mask = 0;
        } else if (e >> 28 == 6u && bk_restored && ui_restore_state() == 2) {   /* restore: restart, once something was written */
            usb_detach();
            fm1_delay_ms(30);
            fm1_reboot();
        }
    }
    if (bk_wait_obj != 0xFFFFFFFFu && ui_restore_state() >= 2) {   /* the written object's answer */
        bk_restored |= ui_restore_state() == 2;
        bk_ack_put(bk_wait_obj, 0x7Fu, ui_restore_state() == 2 ? 0u : 3u);
        bk_wait_obj = 0xFFFFFFFFu;
    }
    if (bk_ack_n && usb_sysex_try(bk_ack, bk_ack_n) == 0)
        bk_ack_n = 0;
    if (bk_rd_obj != 0xFFFFFFFFu) {                      /* an object going out, a frame a turn of the loop as the queue has room */
        const uint32_t n = bk_rd_len - bk_rd_i > BK_FRAME ? BK_FRAME : bk_rd_len - bk_rd_i;
        if (usb_sysex_try(f, bk_build_frame(f, bk_rd_obj, bk_rd_i / BK_FRAME, bk_rd_len, bk_buf + bk_rd_i, n)) == 0) {
            bk_rd_i += n;
            if (bk_rd_i >= bk_rd_len)
                bk_rd_obj = 0xFFFFFFFFu;
        }
    }
    if (dump_i < (uint32_t)P_NPARAMS) {
        int32_t vals[24];
        uint32_t k, n = (uint32_t)P_NPARAMS - dump_i > 24u ? 24u : (uint32_t)P_NPARAMS - dump_i;
        for (k = 0; k < n; k++)
            vals[k] = ui_param_get((int)(dump_i + k));
        if (usb_sysex_try(f, px_build_dump(f, dump_i, vals, n)) == 0)
            dump_i += n;
    }
    if (vox_req != 0xFFFFFFFFu) {
        const uint32_t loaded = (uint32_t)en_user_count();
        const uint8_t *d = vox_req < loaded ? en_user_get((int)vox_req) : 0;
        if (usb_sysex_try(f, px_build_voice(f, vox_req, loaded, d)) == 0)
            vox_req = 0xFFFFFFFFu;
    }
}
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* The installer (the hub's, fm1ota-style) finds the unit by the M-UPGRADE identity: ota_service (the firmware's
 * ota.c, through common/sysex_ota.h). The update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken
 * (plat_sx.h): no read requests follow (the installer times out), nothing is written, the core runs on. */
#define FM1SX_OTA_C "../../cores/PERF-1/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
/* the frames usb.c's collectors and ota.c read: the M-UPGRADE's, ours (7D 50, without the telemetry's 46) and a
 * DX7's (43 0n: a bulk dump or a single voice) */
static int fm1core_perf1_sx_accept(const uint8_t *f, uint32_t n)
{
    if (fm1sx_is_mupg(f, n))
        return 1;
    if (n >= 2u && f[0] == 0x7Du && f[1] == 0x50u)
        return n < 3u || f[2] != 0x46u;
    return n >= 2u && f[0] == 0x43u && (f[1] & 0xF0u) == 0u;
}
static uint32_t perf1_dx_ms = 0xFFFFFFFFu;   /* the device ms of the last pass that handed a voice over */
static void fm1core_perf1_sx_service(void)   /* main_fm1.c fm1_main, each pass: ota_service, dx_service, px_service */
{
    const uint8_t *f;
    uint32_t n;
    if (fm1sx_frame(&fm1core_pl_sxi, &f, &n) && !fm1sx_is_mupg(f, n)) {
        perf1_sx_frame(f, n);             /* (usb.c sorts it as it arrives) */
        fm1sx_done(&fm1core_pl_sxi);
    }
    ota_service();                        /* M-UPGRADE handshake */
    if (dx_state == 2u)
        perf1_dx_ms = fm1_ms;             /* (a voice handed to the engine this pass: see perf1_ui_frame) */
    dx_service();
    px_service();
}
/* The engine takes a voice from en_user_voice a render chunk later (fm6.inc: posted), and ui.c's autosave writes the
 * voices (en_user_count) as soon as the panel has been quiet. On the device the main loop turns many times a
 * millisecond and the audio interrupt renders between dx_service and the next UI frame; here a pass is one device ms
 * after that ms's audio, so a frame in the same pass would save the voices before the engine has them (an empty
 * set kept, the voice lost at the next power-on). That pass draws no frame: the input only, the frame 16 ms on. */
static void perf1_ui_frame(void)
{
    if (fm1_ms == perf1_dx_ms)
        ui_input_only();
    else
        ui_frame();
}
#define FM1CORE_PL_SX_ACCEPT fm1core_perf1_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_perf1_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "PERF-1"
#define FM1CORE_PL_UI_FRAME() perf1_ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the buttons' glow: one scan frame in GLOW_DIV (4) */
#define FM1CORE_PL_DIM2_PLANE fm1_led_dim2
#define FM1CORE_PL_DIM2_LEVEL (FM1P_LED_DIM * 4 / 6)   /* the keys' dim: one frame in KEYS_DIV (6), fainter than the glow */
#define FM1CORE_PL_DIM2_ON (fm1_led_dim2_div != 0)
static void fm1core_pl_flash_boot(void)
{
    fm1nor_set_stall_ms(0);               /* the device's sliced erase: the audio goes on through a write */
}
static void fm1core_pl_app_boot(void)    /* main_fm1.c fm1_main */
{
    en_init();
    project_start(plat_seed());           /* a piece at random, a little changed (plat_fm1.c: the MASTER ADC, ticks) */
    project_apply();
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n) { en_render(lr, n, master_q12); }
static void fm1core_pl_master(int32_t knob)
{
    const uint32_t k10 = (uint32_t)(knob / 16);
    master_q12 = (k10 * k10) >> 8;
}
static void fm1core_pl_loop(void) {}
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        ui_say("UPDATE MODE IN", d);
    } else {
        ui_say("UPDATE MODE", "CANCELLED");
    }
}
/* the lazy save now: what waits for flash written, as before an update (ui.c ui_flush: a store's fade undone, the
 * DX7 voices, the effects' settings, the pieces and the player's three); nothing while a restore holds the store */
static int fm1core_pl_sync(void)
{
    if (plat_store_locked())
        return 1;
    ui_flush();
    return !project_pending();
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "playing %u genre %u piece %u pending %u voices %d", (unsigned)en_playing, (unsigned)proj.genre,
             (unsigned)proj.piece, (unsigned)project_pending(), en_user_count());
}

#include "plat_glue.h"
#include "exit_end.h"
