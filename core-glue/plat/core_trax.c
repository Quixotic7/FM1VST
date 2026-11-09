/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM1-TRAX core: FM1-TRAX (https://github.com/jartificer/fm1-trax, the pinned, unmodified submodule
 * cores/fm1-trax), a Mega Drive music tracker (YM2612 + SN76489) on FM1-GM's platform (Felucca's, by way of X0X and
 * FoMni), built as one unit with the plat glue (core-glue/plat/README.md). The include list follows the device's
 * unity build (firmware/src/main.c) with the hardware left out, as the firmware's own browser build
 * (host/drive_web.c) does: libc.c's string helpers, gfx.c, plat.h, Felucca's storage.c with its chunk store (on the
 * NOR image: the settings, the panel, the 256 song slots and the instrument library), the app's header, the panel
 * calibration (panel.c), the device's plat.h implementation (plat_fm1.c, with plat_store.c), the settings and song
 * slots (settings.c), the UI (ui.c with ui_pat.c and ui_views.c) and the link to the web editor (link.c). main_fm1.c
 * (the boot, the audio interrupt, the main loop) is the hardware's: plat_glue.h redoes it, and the hooks below hold
 * its statements (the boot's order, the render's scaling with MASTER). The chips and the tracker (dsp/opn2.c, psg.c,
 * mdsnd.c, trk/song.c, player.c, presets.c, presets_fm.c) are their own units, as tools/build.py builds them
 * (trax_*.c: -O2, no fused multiply-add).
 *
 * THE EDITOR: the web editor (web/editor.html, web/fm1link.js) lists, reads, writes and deletes the songs and the
 * library banks over SysEx, F0 7D 46 44 cmd .. F7 (link.c's header comment). main_fm1.c's loop runs link_service,
 * then ota_service, each pass; here the same, on the glue's transport (plat_sx.h): usb.c's frame collector is
 * plat_sx.h's (one frame of up to 640 bytes, link.c's DATA fits), its replies leave ahead of the notes. The update
 * itself never runs (the update key F0 22 24 35 7F F7 and UBOOT's F0 22 24 35 7D F7 are not taken: plat_sx.h).
 *
 * Its version is a build-time define (OM_VERSION, "DEV" by default; tools/build.py --release sets it): the hub's
 * version (core_trax.cmake). */
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

#define OM_HOST 1                         /* the device's .pool section off (plat.h OM_POOL) */
#define OM_VERSION FM1CORE_VERSION
#define FELUCCA_ID "FM-1_601000905"       /* the package identity: build.py --release 0.9.5 (FM-1_601 00 09 03) */
#define FM1CORE_PL_SX 1                   /* the editor's link and the installer's M-UPGRADE identity (plat_sx.h) */
#include "plat_hal.h"
#include "plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (the device's .pool / .noinit sections: as the browser build) */
/* libc.c: the freestanding device's memset / memcpy / memcmp under other names (the host's are used), then its
 * string helpers (str_eq, str_len, str_cpy, fmt_fix, fmt_int, rng) as the UI calls them */
#define memset fm1core_trax_memset
#define memcpy fm1core_trax_memcpy
#define memcmp fm1core_trax_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include "gfx.c"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"
static void audio_silence(void) {}        /* (main_fm1.c: an erase silences the DMA buffer: fm1nor_stall_take) */
#include "storage.c"
#include "lz4.c"
#include "app/app.h"
song_t song;                              /* the song (trk/song.h), shared with the player (main.c) */
#include "app/panel.c"
#include "app/plat_fm1.c"
#include "app/settings.c"
#include "app/ui.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* ota.c on the glue's transport (common/sysex_ota.h): the M-UPGRADE identity answered, its flash hooks refuse; then
 * link.c on the same three calls main.c gives it (usb.c's SysEx frame, ota_wire_send) */
#define FM1SX_OTA_C "../../cores/fm1-trax/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
static int link_rx(const uint8_t **p, uint32_t *n) { return ota_frame_get(p, n); }   /* usb.c's SysEx frame */
static void link_rx_done(void) { ota_frame_done(); }
static int link_tx(const uint8_t *p, uint32_t n) { return ota_wire_send(p, n); }
#include "app/link.c"
static int fm1core_trax_sx_accept(const uint8_t *f, uint32_t n)   /* the editor's 7D 46 44, the M-UPGRADE 00 32 */
{
    return fm1sx_is_mupg(f, n) || fm1core_pl_sx_is(f, n, 0x46u, 0x44u);
}
static void fm1core_trax_sx_service(void)   /* main_fm1.c fm1_main, each pass (the update key's ota_req: not taken) */
{
    link_service();                       /* the songs and the library (the web editor); also a load it waits for */
    ota_service();                        /* M-UPGRADE handshake (other SysEx is dropped) */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_trax_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_trax_sx_service()

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "FM1-TRAX"
#define FM1CORE_PL_UI_FRAME() ui_frame()
#define FM1CORE_PL_UI_INPUT() ui_input_only()
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the buttons' glow: one scan frame in GLOW_DIV */
static void fm1core_pl_flash_boot(void) {}
static void fm1core_pl_app_boot(void)    /* main_fm1.c fm1_main, after panel_init and led_pos_init */
{
    presets_bank_init();                  /* the DAC kit */
    player_init(44100u);                  /* the chips (main_fm1.c FS) */
    settings_load();                      /* defaults when nothing (or another format) is saved */
    song_boot_load();                     /* the song last used (the demo song on a fresh flash) */
}
static void fm1core_pl_ui_start(void) { ui_init(); }   /* (main_fm1.c: the splash stays until ui_init draws) */
/* main_fm1.c render_block, one 256-frame part: the chips' 16 bits (clipped by the player) to 24, times the MASTER
 * (Q12), saturated */
static void fm1core_pl_render(int32_t *lr, uint32_t n)
{
    const int32_t g = (int32_t)master_q12;
    uint32_t i;
    memset(lr, 0, (size_t)n * 2u * sizeof *lr);
    player_render(lr, n);
    for (i = 0; i < 2u * n; i++) {
        const int32_t v = lr[i] * g >> 4;
        lr[i] = v > 8388607 ? 8388607 : v < -8388607 ? -8388607 : v;
    }
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
        ui_say("UPDATE MODE IN", d);
    } else {
        ui_say("UPDATE MODE", "CANCELLED");
    }
}
/* nothing lazy: the settings are written when they change, a song when SAVE (or the editor) asks */
static int fm1core_pl_sync(void) { return 1; }
static void fm1core_pl_info(char *buf, uint32_t n)
{
    uint32_t k, used = 0;
    for (k = 0; k < NSLOTS; k++)
        used += slot_info[k].used != 0;
    snprintf(buf, n, "view %u playing %u order %u row %u slot %u dirty %u songs %u title %.24s", (unsigned)ui.view,
             (unsigned)player.playing, (unsigned)player.ord, (unsigned)player.row, (unsigned)set.song,
             (unsigned)ui.dirty, (unsigned)used, song.title);
}

#include "plat_glue.h"
#include "exit_end.h"
