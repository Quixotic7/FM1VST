/* SPDX-License-Identifier: GPL-3.0-only */
/* The RemoteFM1 core: RemoteFM1 0.3.3 by Quixotic7 (https://github.com/Quixotic7/RemoteFM1, the pinned, unmodified
 * submodule cores/RemoteFM1 at v0.3.3, MPL-2.0, on fm1-base and felucca-bsp, MPL-2.0; saadet's FM6 with MSFA,
 * Apache-2.0; the Jersey 10 font, OFL-1.1) built with the fm1-base glue (core-glue/fm1base/README.md: the plat
 * family's HAL and shared core). The meta joke: RemoteFM1 is the firmware that makes an FM-1 the panel of a host
 * running .rfm1 files, and here it is an .rfm1 itself.
 *
 * What runs is RemoteFM1's standalone instrument (firmware/sa/): a DX7 (saadet's FM6, sa/sa_synth.c its own unit,
 * fm1.toml "o2") with Yamaha's eight ROM cartridges compiled in (build/gen/dx7_rom.h from the builder's .syx files:
 * core_remotefm1.cmake, FM1_DX7_ROM_DIR) and the user cartridges, the MIDI controller and its REMOTE mode, the 16
 * loops, the arp, the scales, the screens. The panel half (a host's HELLO and session over USB SysEx, bulk or
 * Bluetooth) is compiled in as it is, but nothing here starts one: no vendor bulk pair, no Bluetooth (radio_none.c),
 * and the USB SysEx it would answer only comes from a host that sends RemoteFM1's own F0 7D 52 46 messages.
 *
 * fm1-base and felucca-bsp as RemoteFM1 builds them: build.sh applies firmware/patches/fm1-base/*.patch and
 * firmware/patches/felucca-bsp/*.patch to a copy of base/; core_remotefm1.cmake does the same into the build folder
 * (git apply, exactly or the configure stops) and this unit reads fm1-base from there. The include list follows
 * fm1-base's device unity build (base/device/unity.c) with the hardware left out, as the other fm1-base cores do:
 * plat.h, the app's header, lib/touch.h (app.h APP_TOUCH: the keys' velocity), gfx.c, fm1b_config.h, sys_text.c,
 * storage.c (app.h APP_STORE_MAP and APP_STORE_MUX: OBJ_RF's records, src/rfm_store.c), the screen reader
 * (announce.c, nothing sent), panel.c, radio_none.c (fm1.toml ble = true, not built: no radio), plat_fm1.c (patched:
 * the store's raw functions, plat_lights_levels), lib/touch.c's host part, then app.c (src/ and sa/).
 *
 * Two of RemoteFM1's MPL-2.0 files are read through a clang VFS overlay (core_remotefm1.cmake):
 *   src/rfm_dev.c   the port on the FM-1 (usb.c's rings and vendor pair, the panel's DMA): remotefm1_port.c here
 *   src/rfm_main.c  the submodule's file with one change: rf_run takes one turn a pass (as in fm1-base's host
 *                   simulator, PLAT_HOST's break), since it spins on the tick counter for 2 ms otherwise, and
 *                   simulated time does not move inside a pass (common/ticks.h)
 * The second core (fm1.toml cpu2) is not built (FM1B_CPU2 0): sa_synth.c renders every voice on the one thread. No
 * managers' SysEx (the update, backup / restore, the log readout: RemoteFM1's SysEx is the panel protocol).
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release's, from core_remotefm1.cmake. */
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
#error "APP_VERSION: build.py --release's (core_remotefm1.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_remotefm1.cmake)"
#endif
#if defined FM1B_CPU2 && FM1B_CPU2
#error "FM1B_CPU2: one thread runs the device here (core_remotefm1.c)"
#endif
#include "fm1base_hal.h"

/* felucca-bsp's two more steady LED levels (RemoteFM1's patch 0004, boards/fm1/fm1_input.h: fm1_led_lv, lit a8 / 8 and
 * b8 / 8 of the scan's frames), as plat_fm1.c's plat_lights_levels (patch 0007) fills them; shown here as the mid
 * plane with felucca-bsp's own (plat_glue.h FM1CORE_PL_MID_PLANE: one steady level between lit and the glow) */
static uint8_t fm1_led_lv[2][FM1P_NCOL];
static uint8_t fm1core_rf_lv8[2];
static void fm1_led_levels(uint32_t a8, uint32_t b8)
{
    fm1core_rf_lv8[0] = (uint8_t)(a8 > 8u ? 8u : a8);
    fm1core_rf_lv8[1] = (uint8_t)(b8 > 8u ? 8u : b8);
}
static uint8_t fm1core_rf_mid[FM1P_NCOL];
static const uint8_t *fm1core_rf_mid_plane(void)
{
    uint32_t c;
    for (c = 0; c < FM1P_NCOL; c++)
        fm1core_rf_mid[c] = (uint8_t)(fm1_led_mid[c] | (fm1core_rf_lv8[0] ? fm1_led_lv[0][c] : 0u) |
                                      (fm1core_rf_lv8[1] ? fm1_led_lv[1][c] : 0u));
    return fm1core_rf_mid;
}

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#include "touch.h"
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"                     /* the faces of the platform's own screens (panel.c's) */
#include "storage.c"

/* plat_fm1.c's host reads (plat_host_read, plat_host_patience): felucca-bsp's chip/ota.c reads, over the managers'
 * SysEx, which this core does not answer: nothing read */
static uint32_t ota_try_ms, ota_deadline;
static uint32_t ota_now_ms(void) { return fm1_ms; }
static int ota_read(uint32_t addr, uint8_t *dst, uint32_t n)
{
    (void)addr, (void)dst, (void)n;
    return -1;
}
/* felucca-bsp's usb.c and guard, as plat_fm1.c reads them: the USB link never lost (a host is always there); the
 * guard's code window has no meaning here */
static volatile uint8_t midi_usb_lost;
static void fm1_guard_pc_extend(uint32_t end) { (void)end; }

#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* (no managers' SysEx: not sent) */
{
    (void)p, (void)n;
    return -1;
}
#include "panel.c"
#include "radio_none.c"
#include "plat_fm1.c"
#define PLAT_HOST 1                       /* lib/touch.c's host part: app_host_key / app_host_tick (the readings) */
#include "touch.c"
#undef PLAT_HOST

/* fm1-base's lcd.c lcd_idle (RemoteFM1's patch 0001): the panel's transfer is over. lcd_blit here is done when it
 * returns */
static int lcd_idle(void) { return 1; }
#include "app.c"
#undef __attribute__

/* the ROM cartridges are compiled in (core_remotefm1.cmake fails the configure without them): never an INIT VOICE
 * build by accident */
#if SA_ROM_NBANKS != 8
#error "dx7_rom.h has no ROM cartridges: give FM1_DX7_ROM_DIR (or DX7_ROM_DIR) the eight .syx files"
#endif

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1core_rf_mid_plane()   /* plat_lights' mid level and plat_lights_levels' two */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_fb_sense_tick(void)  /* the keys' readings, while plat_sense is on (the converter is the scan's) */
{
    if (fm1_an.con)
        app_host_tick();
}
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
/* the keys' velocity and pressure (APP_TOUCH: VELOCITY on, the default): the panel's key_vel / key_lean become the
 * readings the scan would hand the app, through lib/touch.c's own host model (as core_amb.c, core_ripple.c) */
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

static void fm1core_pl_flash_boot(void) {}   /* (no backup and restore: nothing to finish) */
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    app_init();                           /* the store, the settings, the ROM voice, the loops kept */
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
/* the lazy save now: the settings (sa_main.c sa_store_step saves them 2 s after a change, when quiet); a bank or loop
 * still being written (sa_store.c's job, a sector a pass): deferred */
static int fm1core_pl_sync(void)
{
    if (sf_busy())
        return 0;
    if (sa.store_dirty && !sa.panel)
        sa_save_now();
    return !sa.store_dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "%s screen %u base %u bank %u (%s) voice %u '%s' octave %d channel %u voices %u recording %d",
             sa.panel ? "panel" : "standalone", (unsigned)sa.ov, (unsigned)sa.base, (unsigned)sa.bank + 1u,
             sa.bank < SA_NBANKS ? sa_rom_name(sa.bank) : "USER", (unsigned)sa.voice + 1u, sa.name, (int)sa.oct,
             (unsigned)sa.ch, (unsigned)sa_synth_voices(), sl_recording());
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
