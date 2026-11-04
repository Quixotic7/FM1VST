/* SPDX-License-Identifier: GPL-3.0-only */
/* The Ravel core: Ravel (https://github.com/De-Pattern/Ravel_FM1, the pinned, unmodified submodule cores/Ravel_FM1)
 * built with the plat glue (core-glue/plat/README.md). Ravel is portable C on its own platform interface
 * (firmware/app/plat.h); its FM-1 layer (firmware/fm1) is X0X's Felucca layer, adapted: the device implements plat.h
 * in plat_fm1.c on Felucca's HAL, with panel.c's label map and calibration, storage.c's A/B objects and projstore.c's
 * compressed projects on the NOR. This unit is the device's platform unit (firmware/fm1/src/ravel_fm1.c) with the
 * hardware left out, as for X0X: the HAL half (plat_hal.h), then storage.c, the updater's ota.c (the M-UPGRADE
 * identity, through common/sysex_ota.h), panel.c and plat_fm1.c (projstore.c with it), unchanged. main_fm1.c (the
 * boot, the audio interrupt, the main loop) is the hardware's: plat_glue.h redoes it, and the hooks below hold its
 * statements (the render's halving and MASTER ramp, the main loop's transfer messages, the frame every 33 ms).
 * The instrument (firmware/app/ravel_app.c) is its own unit, as tools/build_fm1.py builds it (ravel_app_unit.c).
 *
 * THE MANAGERS: Ravel's own transfer page (projects and sound banks to and from a computer, app/rv_xfer.c) and the
 * installer speak the update's framing (F0 pack7(00 59 cmd len24 body ~sum) F7). main_fm1.c's loop runs ota_service
 * each pass and hands commands 0x60..0x64 to rv_xfer_msg; here the same, on the glue's transport (plat_sx.h), so the
 * transfers read and write the NOR image's slots as on the device. The update itself never runs (the update key
 * F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken: plat_sx.h).
 *
 * Its version is a build-time define (RAVEL_BUILD, "dev" by default; tools/build_fm1.py sets the release's): the
 * hub's version (core_ravel.cmake), in ravel_app_unit.c. */
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
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

#define FELUCCA_ID "FM-1_82011200"        /* the package identity: tools/build_fm1.py --release 1.12.0 (FM-1_82 01 05 00) */
#define FELUCCA_OTA 1                     /* (ravel_fm1.c: the M-UPGRADE entry; plat_sysex_out sends on its wire) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (plat_sx.h) */
#include "plat_hal.h"
#include "plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (the device's .noinit / .pool sections, alignment hints) */
#include "plat.h"                         /* Ravel's platform interface (firmware/app/plat.h) */
#include "rv.h"                           /* (rv_live: the info line) */

/* the instrument (ravel_app.c), as ravel_fm1.c declares it */
void ravel_start(void);
void rv_render(int32_t *out, int frames);
void ui_frame(void);
void ui_input_only(void);
void ui_notice(const char *title, const char *l1, const char *l2, const char *l3, int red);
void rv_transport(int run);
void rv_xfer_msg(const uint8_t *m, uint32_t d);
void rv_bank_settle(void);

/* ravel_fm1.c's storage objects and hooks (storage.c: only the settings and the panel calibration are objects;
 * projects and sound banks are plat_fm1.c's slots), on the HAL half's st_read / st_erase / st_prog */
enum { OBJ_SET, OBJ_SOUND, OBJ_PAT0, OBJ_PAT1, OBJ_PANEL, OBJ_SONG, OBJ_MOTION, OBJ_NOBJ,
       OBJ_RPREF = 0x5250 };
static uint32_t flash_jedec = 0x856014u, flash_sr, flash_con, flash_baud;   /* (GLO > FLASH: the FM-1's part) */
static void audio_silence(void) {}        /* (an erase stalls the audio: fm1nor_stall_take) */
#include "fm1/src/storage.c"
int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    return flash_ok && (obj == OBJ_SET || obj == OBJ_PANEL) ? st_save(obj, src, len) : -9;
}
int plat_prefs_load(void *dst, int max) { return flash_ok && max > 0 ? st_load(OBJ_RPREF, dst, (uint32_t)max) : -1; }
int plat_prefs_save(const void *src, int len)
{
    return flash_ok && len > 0 ? st_save(OBJ_RPREF, src, (uint32_t)len) : -9;
}

/* the updater's ota.c on the glue's transport: the identity answered, the frame left in ota_dec for the transfers;
 * its flash hooks refuse (common/sysex_ota.h) */
#define FM1SX_OTA_C "../../cores/Ravel_FM1/firmware/fm1/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

/* what plat_fm1.c needs of the HAL beyond the plat family's (the second core, the guards, the crash record, the
 * performance counters, usb.c's MIDI time stamp), as for X0X: the second core runs its job at once, in order (the
 * split path, the same sums: Ravel's host simulator's RAVEL_FAKE_CORE1) */
static int fm1_c1_on, fm1_c1_gave_up;
static int fm1_cpu1_start(void)
{
    fm1_c1_on = 1;
    return 0;
}
static void fm1_cpu1_hold(void) { fm1_c1_on = 0; }
static int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1_c1_on)
        return 0;
    fn(arg);
    return 1;
}
static int fm1_cpu1_wait(void) { return 0; }
static void fm1_cpu1_sync(void) {}
#define FM1_GUARD_PC 0u
static void fm1_guard_unlock_top(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_guard_pc_open(void) {}
static void fm1_guard_enable(uint32_t g) { (void)g; }
#define FM1_C1_BAR_MAGIC 0x43314241u
static struct {
    uint32_t magic, barred;
} fm1_c1_bar;                             /* (power-on: the second core was not running) */
static int fm1_perf_cycles_run(void) { return 0; }   /* (no CPU cycle counter: plat_cycles is the 24 MHz timer) */
static uint32_t fm1_perf_cycles(void) { return fm1_ticks(); }
static void fm1_perf_stalls(uint32_t s[3]) { s[0] = s[1] = s[2] = 0; }
static void fm1_perf_stalls_enable(int on) { (void)on; }
static uint32_t fm1core_ravel_irq_stack[64];
#define _sstack_lo fm1core_ravel_irq_stack
#define _sstack_top (fm1core_ravel_irq_stack + 64)
/* usb.c stamps a real-time byte as it arrives (bytes 2..3 of its packet) and plat_midi_in reads its age; here the
 * core takes each packet in the device ms it arrives: the stamp is the packet's own, the age 0 */
#define MIDI_STAMP() ((pkt) >> 16)

#include "fm1/src/panel.c"
#include "fm1/src/plat_fm1.c"
#undef __attribute__
#undef _sstack_lo
#undef _sstack_top

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
static int fm1core_ravel_sx_accept(const uint8_t *f, uint32_t n)   /* the update's framing (00 59 .. packed) */
{
    return fm1sx_is_mupg(f, n);
}
static void fm1core_ravel_sx_service(void)   /* main_fm1.c fm1_main, each pass (the update key: not taken) */
{
    const uint32_t d = ota_service();     /* M-UPGRADE handshake; Ravel's own messages: projects and sounds */
    if (d >= 7u && ota_dec[2] >= 0x60u && ota_dec[2] <= 0x64u)
        rv_xfer_msg(ota_dec, d);
}
#define FM1CORE_PL_SX_ACCEPT fm1core_ravel_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_ravel_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "Ravel"
#define FM1CORE_PL_UI_FRAME() ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_FRAME_MS 33u           /* main_fm1.c: ~30 frames a second (66 ms when the audio is busy: never here) */
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the backlight: one scan frame in back_div */
/* plat.h numbers the labels OCT-, OCT+, FX, SCL, .. REC and the knobs SELECT, ALGO, K1..K4, PRESETS */
static const uint8_t fm1core_ravel_btn_of[FM1P_NB] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME,
                                                      B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP};
static const uint8_t fm1core_ravel_enc_of[FM1P_NE - 1] = {EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4};
#define FM1CORE_PL_BTN_OF(label) fm1core_ravel_btn_of[(label)]
#define FM1CORE_PL_ENC_OF(role) fm1core_ravel_enc_of[(role)]
/* what plat_glue.h's UBOOT screen draws with (gfx.c's in the other plat firmwares): main_fm1.c enter_uboot's notice */
#define C_BLACK 0
#define draw_text_box(x, y, w, font, s, c, a) ui_notice((s), "UPDATE MODE", 0, 0, 0)
#define palette_set(p) ((void)(p))        /* (no palettes) */
static void fm1core_pl_flash_boot(void) {}   /* (ota_boot_cleanup: the update's staging is never written here) */
static void fm1core_pl_app_boot(void)    /* main_fm1.c fm1_main, after panel_init and led_pos_init */
{
    ravel_start();                        /* a blank project, the sound banks, the UI (ui_init) */
}
static void fm1core_pl_ui_start(void) {}  /* (ravel_start ran ui_init) */
/* main_fm1.c render_block, one 256-frame part: full scale 2^23 halved to the codec's comfortable 2^22, then the
 * MASTER knob, ramped 16 steps every 16 samples */
static uint32_t fm1core_ravel_gain_q12;
static void fm1core_pl_render(int32_t *lr, uint32_t n)
{
    uint32_t i;
    rv_render(lr, (int)n);
    for (i = 0; i < 2u * n; i++)
        lr[i] >>= 1;
    for (i = 0; i < 2u * n; i++) {
        if ((i & 15u) == 0u) {
            const uint32_t target = master_q12, g = fm1core_ravel_gain_q12;
            if (g < target)
                fm1core_ravel_gain_q12 += target - g > 16u ? 16u : target - g;
            else if (g > target)
                fm1core_ravel_gain_q12 -= g - target > 16u ? 16u : g - target;
        }
        lr[i] = (int32_t)(((int64_t)lr[i] * (int32_t)fm1core_ravel_gain_q12) >> 12);
    }
}
static void fm1core_pl_master(int32_t knob)
{
    const uint32_t k10 = (uint32_t)(knob / 16);
    master_q12 = (k10 * k10) >> 8;        /* 0 .. ~4096, an audio taper */
}
static void fm1core_pl_loop(void) {}
static void fm1core_pl_uboot_say(uint32_t left) { (void)left; }   /* (main_fm1.c: no countdown, UBOOT at 5 s) */
/* projects and sound banks are written when SAVE asks (and the device settings when changed): no lazy save, but a
 * sound's name being edited is kept now (rv_xfer.c INFO's rv_bank_settle) */
static int fm1core_pl_sync(void)
{
    rv_bank_settle();
    return 1;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "playing %u recording %u bar %u beat %u", (unsigned)rv_live.playing,
             (unsigned)rv_live.recording, (unsigned)rv_live.bar, (unsigned)rv_live.beat);
}

#include "plat_glue.h"
#include "exit_end.h"
