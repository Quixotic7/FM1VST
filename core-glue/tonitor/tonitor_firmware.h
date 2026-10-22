/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware the Tonitor core runs: the ONE include list (included once, by tonitor_fw.c), from the unmodified
 * submodule cores/tonitor. It follows the firmware's own unity unit, firmware/src/tonitor.c, in its order and with
 * its settings, the device's HAL headers (firmware/hal/fm1_*.h) replaced by tonitor_hal.h and hal/fm1_flash.h, as
 * its tests/host/host.c leaves them out. What is built out (it needs the hardware), and why:
 *   lcd.c         the ST7789's SPI driver: tonitor_hal.h's lcd_fill / lcd_blit write the panel's RAM instead
 *   midi_uart.c   the TRS jack's UART1 + DMA: MIDI comes in over "USB" (the core's midi_in)
 *   FELUCCA_UAC   0: usb.c's USB audio out (0.52) feeds the computer over the cable; a core's audio is its output
 *   TELEMETRY     0 (tonitor.c's default: a diagnostic build's SysEx reports)
 * usb.c IS built, with FELUCCA_OTA 1 as tonitor.c builds it: its MIDI rings, its SysEx assembly (the UBOOT key, the
 * update's start, the one frame it keeps for the main loop) and its SysEx reply ring are the firmware's own, the
 * core's midi_in / midi_out_take take the place of its EP1 (usb_poll, the SIE, is never run). ota.c (the installer's
 * identity) and backup.c (the backup page's LIST / READ / WRITE) answer on them, as on the device.
 * main.c IS built: its audio ISR (fm1_alnk0_irq), audio_init, the flash hooks (st_read, fl_erase4k_quiet, the
 * sessions' and ota.c's), backup.c's bk_wire / bk_done and the splash are called verbatim; fm1_cstart (the reset
 * entry), TIMER5's ISR and fm1_main (the boot and the main loop, which never return) are compiled and never run:
 * tonitor_fw.c is their twin on the ABI's clock. */
#include <stddef.h>
#include <stdint.h>
#include "nor.h"                                   /* (core-glue/common) the flash image */
#include "coro.h"                                  /* the main loop's coroutine (the waits yield on it) */
#include "ticks.h"

/* ---- tonitor.c's settings ---- */
#ifndef TONITOR_ID
#define TONITOR_ID "FM-1_700"                      /* the package identity (tools/build.py sets it; its default) */
#endif
#ifndef TONITOR_VERSION
#define TONITOR_VERSION FM1CORE_VERSION            /* tonitor.c's own (core.cmake VERSION is the pinned commit's) */
#endif
#define FELUCCA_ID TONITOR_ID
#define TELEMETRY 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0                              /* (tonitor.c: 1; see above) */
#define FELUCCA_UAC_48K 0
#define HALF_FRAMES 256u
#define FM1_CORE1 1

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static volatile uint32_t fm1_ms;                   /* milliseconds since boot (TIMER5; here the ABI's tick) */

/* ------------------------------------------------------------- the HAL --- */
#include "tonitor_hal.h"                           /* fm1_time.h .. fm1_lcd_hw.h (this folder) */
#include "tonitor_tables.h"
#include "bootguard.h"
bootguard_t bootguard;                             /* (.noinit on the device: the core's boot_options sets it) */

/* libc.c is the device's (freestanding): its memset / memcpy / memcmp kept off the host's */
#define memset tonitor_memset
#define memcpy tonitor_memcpy
#define memcmp tonitor_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp

/* ------------------------------------------------------------ the sound --- */
#include "dsp.h"
#include "gfx.c"
#include "fm.c"
#include "anima.c"
#include "cal.c"
#include "russolo.c"
#include "druid.c"
#include "bayarea.c"
#include "hive.c"
#include "usb.c"
#include "reverb.c"
#include "layers.c"

/* ------------------------------------------------- the UI, the sessions --- */
#include "ui.c"
#include "sessions.c"
#include "setup.c"
#include "backup.c"

/* main.c's two external functions that would run the device (the TIMER5 ISR, the reset entry) are made static and
 * unused, so the compiler drops them and what only they reach (fm1_main, recovery_main, usb_start / usb_poll and
 * the USB registers); fm1_alnk0_irq and the rest stay callable */
#define fm1_timer5_irq fm1core_tn_t5_unused(void); static void fm1core_tn_t5
#define fm1_cstart fm1core_tn_cstart_unused(void); static void fm1core_tn_cstart
#include "main.c"
#undef fm1_timer5_irq
#undef fm1_cstart
