/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the Vector37 core runs: the ONE include list (included once, by core_vector37.c), from the unmodified
 * submodule cores/Vector37 (v0.3.2). Vector37 is Speak37's sibling (the same author, the same SLOOP / Felucca board
 * layer: firmware/hal/fm1_*.h, libc.c, gfx.c, usb.c), so it runs on Speak37's host HAL, speak37_hal.h. This list
 * follows the device's unity unit, firmware/src/vector37.c, in its order, for the FM-1 build (build.py BOARD=fm1: no
 * BOARD_SMK37 flag, the panel panel_fm1.c), with the device's HAL headers replaced by speak37_hal.h and lcd.c (the
 * SPI driver) by its lcd_fill / lcd_blit, as the firmware's own FM-1 host test (tests/panel.h with BOARD_SMK37 0,
 * tests/fm1_panel.c) builds the sound and the screen.
 * Built out (they need the hardware), as vector37.c's own switches allow:
 *   FELUCCA_OTA  usb.c's SysEx plumbing and the M-UPGRADE update session; ota.c's identity is built on the glue's
 *                transport instead (common/sysex_ota.h, core_vector37.c)
 *   FELUCCA_CDC  the serial console (console.c, cdc_task)
 *   FELUCCA_UAC  the USB audio input (usb.c's uac_*; audio.c's two calls into it are no-ops below)
 * Not built: lcd.c, fm1_flash.h (speak37_hal.h has their host twins), recovery.c (the polled USB rescue updater:
 * the core halts instead, core_vector37.c) and main.c (the TIMER5 ISR, the power-on and main loop: core_vector37.c
 * is their twin on the ABI's clock). usb.c IS built (USB-MIDI in: midi_in_event and its ring, the UBOOT key; MIDI
 * out's ring), its USB driver never runs. audio.c IS built: its ALNK0 ISR (fm1_alnk0_irq: synth_block in CTL
 * blocks, the load guard) is the one the glue calls.
 * The FM-1 build keeps no store of its own (scenes.c persist_boot returns after the JEDEC probe, fm1_flash_map.h's
 * FL_STORE_OK is 0): the core never writes its flash image. */
#include <stddef.h>
#include <stdint.h>
#include "nor.h"                                   /* (core-glue/common) the image the XIP window reads */
#include "ticks.h"

/* ---------------------------------------------------- build options --- */
#define BOARD_SMK37 0                              /* the FM-1 (build.py BOARD=fm1) */
#define MK_VERSION FM1CORE_VERSION                 /* (vector37.c's, read by the cmake: "0.3.2") */
#define BRAND_NAME "VECTOR37"
#define BRAND_CRASH "VECTOR37 CRASH"
#define BRAND_RESCUE "VECTOR37 USB RESCUE"
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_UAC_TONE 0
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_700"                      /* build.py BOARD=fm1: the FM-1 package's identity */
#endif
/* the pace (draw.c): the ABI calls frame() every 15 ms, so the main loop's pass and the field frame run at 15 ms
 * (the device: at most every 16 ms, ~60 a second; with 16 here every other frame() would skip its field) */
#define UI_LOOP_MS 15u
#define UI_FIELD_MS 15u

/* ------------------------------------------------------------- HAL --- */
#include "speak37_hal.h"                           /* (Speak37's: the same device HAL, on the host) */
static uint8_t fm1_led_dim[FM1_NCOL];              /* fm1_input.h: the glow plane (panel_fm1.c fp_led level 1) */
#define PANEL_MASTER 1                             /* (mix.c: the pot's level after the master) */
static volatile int32_t panel_master_q12 = 4096;   /* the pot: 0 .. 4096 (panel_fm1.c) */

#include "mk_tables.h"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static volatile uint32_t fm1_ms;
#include "bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));

/* ----------------------------------------------------- base, display --- */
#define memset felucca_memset                      /* libc.c is the device's (freestanding): kept off the host's */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
static void lcd_sync_buf(const void *p, uint32_t n) { (void)p, (void)n; }   /* (lcd.c: no transfer in flight) */
#include "gfx.c"

/* ------------------------------------------------------------ sound --- */
#include "synth.h"
#include "orbit.c"
#include "engine.c"
#include "analog.c"
#include "fx.c"
#include "mix.c"
#include "params.c"
#include "presets.c"

#define HALF_FRAMES 128u                           /* I2S half buffer: one ABI block (the device: 256) */
static uint8_t trs_out;                            /* (vector37.c: the FM-1's jack is MIDI IN, no TRS MIDI OUT) */
static struct { uint8_t dead; } smo;
static void smo_put(uint32_t pkt) { (void)pkt; }
static void smo_tick(void) {}
static void smo_on(uint32_t on) { (void)on; }
#include "usb.c"                                   /* MIDI in and out (the driver never runs) */
#define uac_render_start() ((void)0)               /* (FELUCCA_UAC 0: no USB audio input) */
#define uac_tap(out, n) ((void)(out), (void)(n))
#include "audio.c"
#undef uac_render_start
#undef uac_tap

/* --------------------------------------------------- storage, update --- */
/* vector37.c's store hooks, on speak37_hal.h's fl_* over the NOR image */
static uint8_t flash_ok;                           /* JEDEC id matched at boot */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static int st_erase(uint32_t off)
{
    uint32_t took, f;
    int rc;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    f = irq_save();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
static int flash_probe(void)
{
    uint32_t f = irq_save();
    int ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());
    irq_restore(f);
    return ok;
}
#define FELUCCA_DATA_FAMILY "VECTOR"
#define FELUCCA_DATA_UNTAGGED_OK 0
#include "storage.c"
#include "scenes.c"

static uint8_t recovery_active;                    /* (recovery.c is not built: never set) */
#define OTA_IDENTITY (recovery_active ? "FM-1_000" : FELUCCA_ID)
/* ota.c: core_vector37.c, on the glue's SysEx transport (common/sysex_ota.h) */

#include "ui.c"                                    /* (with panel_fm1.c) */
#include "draw.c"
