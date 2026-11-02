/* SPDX-License-Identifier: GPL-3.0-only */
/* The SiDJ core: SiDJ (https://github.com/thomasj/fm1-sidj, the pinned, unmodified submodule cores/fm1-sidj), a C64
 * music deck (a cycle-exact 6510 and reSID-derived SID chips playing .sid files, remixed live), on X0X's platform
 * (Felucca's), built as one unit with the plat glue (core-glue/plat/README.md). The include list follows the device's
 * unity build (firmware/src/sidj.c) with the hardware left out, as the firmware's own host simulator
 * (host/sidj_host.c) does: gfx.c, plat.h, the engine's header, Felucca's storage.c (on the NOR image: the settings,
 * the panel, the kept MIDI clocks), the song store (songs.c: the .sid files packed in the user area 0xA0000..0xDBFFF,
 * read in place from the image as the device reads them through XIP), the panel calibration (panel.c), the device's
 * plat.h implementation (plat_fm1.c), the kept clocks (clkstore.c) and the UI. main_fm1.c (the boot, the audio
 * interrupt, the main loop) is the hardware's: plat_glue.h redoes it, and the hooks below hold its statements (the
 * boot's order, the render's gains, the MIDI clock's timed bytes). The SID, the C64 and the engine are their own
 * units (sidj_*.c), as tools/build.py builds them (O2_UNITS).
 *
 * THE SONGS PAGE (web/site/index.html #songs, web/site/fm1songs.js; tools/fm1_sid.py): lists, adds, fetches, deletes
 * and plays the songs over SysEx, F0 7D 53 cmd pack7(..) F7 (songs.c). main_fm1.c's loop runs eng_poll, songs_service,
 * then ota_service, each pass; here the same, on the glue's transport (plat_sx.h). The update itself never runs (the
 * update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken: plat_sx.h).
 *
 * Left out: USB audio (FELUCCA_UAC 0: the five-channel stream is the computer's, the ABI has one stereo out), the TRS
 * jack's UART (the ABI's MIDI in is one stream), the load valve (eng_busy: no render load is measured), the 200 ms
 * fade a flash write waits for on the device (the song is stopped, the write happens at once).
 *
 * Its version is a build-time define (SIDJ_VERSION, tools/version.sh's by default; tools/build.py --release sets it):
 * the hub's version (core_sidj.cmake). */
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

#define SIDJ_HOST 1                       /* the device's .pool sections off (plat.h SIDJ_POOL, engine.h ENG_POOL) */
#define SIDJ_VERSION FM1CORE_VERSION
#define FELUCCA_ID "FM-1_64010300"        /* the package identity: build.py --release 1.3.0 (FM-1_64 01 03 00) */
#define FELUCCA_UAC 0                     /* (USB audio: not in the ABI) */
#define FELUCCA_UAC_STEMS 0
#define FM1CORE_PL_SX 1                   /* the songs page and the installer's M-UPGRADE identity (plat_sx.h) */
#define FM1CORE_PL_PALETTE 0u             /* main_fm1.c fm1_main: palette_set(0) (DEBUT) */
#include "plat_hal.h"
#include "plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "gfx.c"
#include "app/plat.h"
#include "app/engine.h"

/* sidj.c: before a flash erase a playing song stops (resume 1: it plays on 100 ms after the last erase, from the
 * main loop; 0: it stays stopped: a song store change). The device waits up to 200 ms for its fade; here the erase
 * happens at once (the audio's stall: fm1nor_stall_take) */
static uint8_t flash_resume;
static uint32_t flash_quiet_ms;
static void flash_quiet(int resume)
{
    flash_quiet_ms = fm1_ms;
    if (!resume)
        flash_resume = 0;
    if (!eng_playing)
        return;
    eng_post(EV_STOP, 0, 0, 0);
    if (resume)
        flash_resume = 1;
}
static int fm1core_sidj_st_erase(uint32_t off)   /* sidj.c st_erase: the song quiet first */
{
    flash_quiet(1);
    return fm1nor_erase(off);
}
#define st_erase fm1core_sidj_st_erase
#include "storage.c"
#undef st_erase

/* usb.c's MIDI clock in: each pulse's time (the 24 MHz ticks) and number, Start numbering from 0, Stop to Continue
 * not counted; F8 timed here, never queued; Start, Stop and Continue go on to the app's queue too
 * (FM1CORE_PL_MIDI_IN_HOOK) */
#define CQ 32u
static uint32_t ci_t[CQ], ci_k[CQ], ci_n;
static volatile uint32_t ci_w, ci_r;
static uint8_t ci_stopped;
static void clock_in(uint32_t b)
{
    uint32_t k;
    if (b == 0xFBu) { ci_stopped = 0; return; }
    if (b == 0xFCu) { ci_stopped = 1; k = PLAT_CLK_STOP; }
    else if (b == 0xFAu) { ci_n = 0; ci_stopped = 0; k = PLAT_CLK_START; }
    else if (b != 0xF8u) return;
    else k = ci_stopped ? PLAT_CLK_FREE : ci_n++;
    if (ci_w - ci_r < CQ) {
        ci_t[ci_w % CQ] = fm1_ticks();
        ci_k[ci_w % CQ] = k;
        ci_w++;
    } else {                              /* full: the newest slot the latest */
        ci_t[(ci_w - 1u) % CQ] = fm1_ticks();
        ci_k[(ci_w - 1u) % CQ] = k;
    }
}
static int fm1core_sidj_midi_in(uint32_t pkt)   /* usb.c ep1_rx, after the SysEx: 1 taken */
{
    const uint32_t cin = pkt & 15u, b = (pkt >> 8) & 0xFFu;
    if (cin == 0xFu && b == 0xF8u) {
        clock_in(0xF8u);
        return 1;
    }
    if (cin == 0xFu && b >= 0xF8u)
        clock_in(b);
    else if (cin == 0xFu || cin < 8u)
        return 1;                         /* (usb.c queues channel messages and the real-time bytes only) */
    return 0;
}
#define FM1CORE_PL_MIDI_IN_HOOK(pkt) fm1core_sidj_midi_in(pkt)

/* ota.c on the glue's transport (common/sysex_ota.h): the M-UPGRADE identity answered, its flash hooks refuse; its
 * ota_frame_get / ota_frame_done / ota_wire_send are the three songs.c takes, as on the device (usb.c) */
#define FM1SX_OTA_C "../../cores/fm1-sidj/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

/* sidj.c: the song pool over the flash, read in place (the device's plain XIP window: here the NOR image itself) */
static const uint8_t *song_ptr(uint32_t off) { return fm1nor_image + off; }
static int song_blank(uint32_t off, uint32_t n)
{
    uint32_t i;
    if (!flash_ok || off + n > FM1NOR_SIZE)
        return 0;
    for (i = 0; i < n; i++)
        if (fm1nor_image[off + i] != 0xFFu)
            return 0;
    return 1;
}
static int song_erase_sector(uint32_t off)
{
    if (!flash_ok)
        return -1;
    flash_quiet(1);
    return fm1nor_erase(off) ? -1 : 0;
}
static int song_prog(uint32_t off, const uint8_t *src, uint32_t n)
{
    if (!flash_ok || fm1nor_prog_span(off, src, n))
        return -1;
    return memcmp(fm1nor_image + off, src, n) ? -2 : 0;   /* (fl_verify_xip) */
}
static void songs_changed(void);          /* the song store changed over USB (songs.c -> the UI) */
static int songs_play_request(int k, int subsong);
#include "app/settings.c"
#include "app/songs.c"
#include "app/panel.c"
#include "app/plat_fm1.c"
#include "app/clkstore.c"
#include "app/ui.c"
#undef __attribute__

/* main_fm1.c: the MIDI clock's bytes, timed. A byte rendered at frame f of a block goes out when that frame plays
 * (the device: a half buffer after its interrupt, TIMER5 sending it); here the frame's time on the device clock
 * (block k's first frame at k * 256 / 44.1 ms: plat_glue.h renders it then), sent from the tick that reaches it */
#define FM1CORE_SIDJ_RT_Q 32u
static struct {
    uint32_t due;
    uint8_t b;
} fm1core_sidj_rt[FM1CORE_SIDJ_RT_Q];
static uint32_t fm1core_sidj_rt_w, fm1core_sidj_rt_r;
static uint64_t fm1core_sidj_frames;      /* frames rendered since power-on: the block being rendered starts here */
uint32_t plat_frame_ticks(uint32_t frame)
{
    return (uint32_t)((fm1core_sidj_frames + frame) * (uint64_t)(FM1_TICKS_PER_US * 1000000u) / ENG_RATE);
}
void plat_midi_rt(uint8_t b, uint32_t frame)
{
    if (fm1core_sidj_rt_w - fm1core_sidj_rt_r >= FM1CORE_SIDJ_RT_Q)
        return;
    fm1core_sidj_rt[fm1core_sidj_rt_w % FM1CORE_SIDJ_RT_Q].due = plat_frame_ticks(frame);
    fm1core_sidj_rt[fm1core_sidj_rt_w % FM1CORE_SIDJ_RT_Q].b = b;
    fm1core_sidj_rt_w++;
}
static void fm1core_sidj_rt_service(void)   /* main_fm1.c rt_service (TIMER5): the bytes due */
{
    const uint32_t now = fm1_ticks();
    while (fm1core_sidj_rt_r != fm1core_sidj_rt_w &&
           (int32_t)(now - fm1core_sidj_rt[fm1core_sidj_rt_r % FM1CORE_SIDJ_RT_Q].due) >= 0) {
        midi_out_event(0x0Fu | (uint32_t)fm1core_sidj_rt[fm1core_sidj_rt_r % FM1CORE_SIDJ_RT_Q].b << 8);
        fm1core_sidj_rt_r++;
    }
}
#define FM1CORE_PL_TICK_HOOK() fm1core_sidj_rt_service()

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
static int fm1core_sidj_sx_accept(const uint8_t *f, uint32_t n)   /* the songs' 7D 53 (S), the M-UPGRADE 00 32 */
{
    return fm1sx_is_mupg(f, n) || (n >= 3u && f[0] == 0x7Du && f[1] == 0x53u);
}
static void fm1core_sidj_sx_service(void)   /* main_fm1.c fm1_main, each pass (the update key's ota_req: not taken) */
{
    eng_poll();                           /* a song load, outside the audio interrupt */
    songs_service();                      /* .sid files over SysEx (the songs page, tools/fm1_sid.py) */
    if (flash_resume && fm1_ms - flash_quiet_ms > 100u) {   /* paused for a settings save: on again */
        flash_resume = 0;
        eng_post(EV_PLAY, 0, 0, 0);
    }
    ota_service();                        /* M-UPGRADE handshake (other SysEx is dropped) */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_sidj_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_sidj_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "SiDJ"
#define FM1CORE_PL_UI_FRAME() ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* (plat_leds: lit only, no glow plane) */
static void fm1core_pl_flash_boot(void) {}   /* (ota_boot_cleanup: the update's staging, never written here) */
static void fm1core_pl_app_boot(void)    /* main_fm1.c fm1_main, after panel_init and led_pos_init */
{
    eng_init();
    settings_load();                      /* defaults if nothing is saved */
    eng_usb4 = set.usb4;                  /* the speed cap (USB audio's format: not here) */
    songs_scan();                         /* the index of the song pool */
    fm1core_sidj_frames = 0;
    fm1core_sidj_rt_w = fm1core_sidj_rt_r = 0;
    ci_w = ci_r = ci_n = 0;
    ci_stopped = 0;
    flash_resume = 0;
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init();
}
#define FM1CORE_SIDJ_UNITY_Q12 8192u      /* main_fm1.c UNITY_Q12: the chip's 16 bits as 24 */
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block, one 256-frame part */
{
    const int32_t m = (int32_t)master_q12;
    uint32_t i;
    eng_render(lr, (int)n, FM1CORE_SIDJ_UNITY_Q12);
    for (i = 0; i < 2u * n; i++)          /* the jack: MASTER */
        lr[i] = ((lr[i] >> 5) * m) >> 8;
    fm1core_sidj_frames += n;
}
static void fm1core_pl_master(int32_t knob)
{
    const uint32_t k10 = (uint32_t)(knob / 16);
    master_q12 = (k10 * k10) >> 8;        /* 0 .. ~4096 */
}
static void fm1core_pl_loop(void) {}
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        ui_say("UPDATE MODE IN ", d);
    } else {
        ui_say("UPDATE MODE CANCELLED", 0);
    }
}
/* the lazy save now: SAVE writes the settings at once; the kept MIDI clocks (clkstore.c) wait for a quiet moment,
 * as cs_service does each frame: while a song plays, deferred */
static int fm1core_pl_sync(void)
{
    if (!cs_dirty)
        return 1;
    if (eng_playing || eng_file.pending)
        return 0;
    cs_service();
    return !cs_dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "mode %u intro %u songs %d sel %d cur %d playing %u loaded %u output %u", (unsigned)ui.mode,
             (unsigned)ui.intro, song_count, ui.sel, ui.cur, (unsigned)eng_playing, (unsigned)eng_loaded,
             (unsigned)set.output);
}

#include "plat_glue.h"
#include "exit_end.h"
