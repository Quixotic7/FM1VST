/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the Speak37 core runs: the ONE include list (included once, by core_speak37.c), from the unmodified
 * submodule cores/Speak37 (v1.0). It follows the device's unity unit, firmware/src/speak.c, in its order, for the FM-1
 * build (build.py BOARD=fm1speak: BOARD_SMK37 0, the panel sp_panel_fm1.c), with the device's HAL headers
 * (firmware/hal/fm1_*.h) replaced by speak37_hal.h and lcd.c (the SPI driver) by its lcd_fill / lcd_blit, as the
 * firmware's own FM-1 host test (tests/sp_screen.c -DT_FM1=1) builds the sound and the screen.
 * Built out (they need the hardware), as speak.c's own switches allow:
 *   FELUCCA_OTA  usb.c's SysEx plumbing and the M-UPGRADE update session; ota.c's identity is built on the glue's
 *                transport instead (common/sysex_ota.h, core_speak37.c)
 *   FELUCCA_CDC  the serial console (sp_console.c, cdc_task)
 *   FELUCCA_UAC  the USB audio input (usb.c's uac_*; sp_main.c's three calls into it are no-ops below)
 * usb.c IS built (USB-MIDI in: midi_in_event and its ring, the UBOOT key; MIDI out's ring), its USB driver never
 * runs. sp_main.c IS built: the ALNK0 ISR (fm1_alnk0_irq: USB-MIDI drained, sp_render, MASTER, the soft limiter, the
 * load governor) is the one the glue calls, and sp_store_boot / sp_store_save / sp_user_save are the device's;
 * fm1_main (its power-on and main loop, which never return), fm1_cstart (the reset entry) and the TIMER5 ISR are
 * compiled and never run: core_speak37.c is their twin on the ABI's clock. */
#include <stddef.h>
#include <stdint.h>
#include "nor.h"                                   /* (core-glue/common) the image the XIP window reads */
#include "ticks.h"

/* ---------------------------------------------------- build options --- */
#define BOARD_SMK37 0                              /* the FM-1 */
#define SPEAK_BUILD 1                              /* (usb.c: the USB names are Speak37's) */
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_CDC 0
#define FELUCCA_CDC_DEFAULT 0
#define FELUCCA_UAC 0
#define FELUCCA_UAC_TONE 0
#define FELUCCA_UART 0
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_510"                      /* the v1.0 release package's identity (build.py --release 1.0) */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "v1.0"
#endif
#define SP_VERSION FELUCCA_VERSION
#define FELUCCA_USBHOST 0

/* ------------------------------------------------------------- HAL --- */
#include "speak37_hal.h"

/* ---------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static volatile uint32_t fm1_ms;
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
#define HALF_FRAMES 128u                           /* I2S half buffer: 2.9 ms (one ABI block) */
#define FS 44118u                                  /* 24 MHz / 544 */
static int32_t abuf[2u * 2u * HALF_FRAMES] __attribute__((aligned(4)));

/* ----------------------------------------------------- base, display --- */
#define memset felucca_memset                      /* libc.c is the device's (freestanding): kept off the host's */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
#define GFX_DOUBLE 1                               /* two canvases: one drawn while the other goes out */
#include "gfx.c"

/* ------------------------------------------------------------ sound --- */
#define SP_KEYS() ((uint64_t)fm1_in.notes << 5)    /* (the FM-1's 27 keys F3..G5: the SMK's keys 5..31) */
#define SP_VEL(k) 100u                             /* (no velocity) */
#include "sp_dsp.c"                                /* the speech engine, the instrument */
#include "usb.c"                                   /* MIDI in and out (the driver never runs) */
#include "sp_state.c"                              /* the record, the pads, the knobs' parameters */
#include "sp_view.c"                               /* the pictures: the display, the mouth */
#include "sp_ui.c"                                 /* the screen, the shared actions */
#include "sp_panel_fm1.c"

/* --------------------------------------------------- storage, update --- */
#include "storage_hw.c"                            /* (on speak37_hal.h's fl_* over the NOR image) */
#include "storage.c"

/* sp_main.c: the USB audio input is built out (FELUCCA_UAC 0); its two external functions besides the audio ISR
 * (the TIMER5 ISR, the reset entry) would be emitted and pull in the USB driver: made static and unused, so the
 * compiler drops them; fm1_alnk0_irq stays (the glue's audio) */
#define uac_render_start() ((void)0)
#define uac_tap(out, n) ((void)(out), (void)(n))
#define uac_service() ((void)0)
#define fm1_timer5_irq fm1core_sp_t5_unused(void); static void fm1core_sp_t5
#define fm1_cstart fm1core_sp_cstart_unused(void); static void fm1core_sp_cstart
#include "sp_main.c"
#undef fm1_timer5_irq
#undef fm1_cstart
#undef uac_render_start
#undef uac_tap
#undef uac_service
