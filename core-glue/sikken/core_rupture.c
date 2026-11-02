/* SPDX-License-Identifier: GPL-3.0-only */
/* RUPTURE (https://github.com/SikkenWorks/RUPTURE, the pinned submodule cores/RUPTURE): the core unit. RUPTURE is a
 * native FM-1 application (vendor/etch-v1.88-source/firmware/fm1-native: bringup.c and the units build.py links
 * with it) on Felucca's HAL, with no emulator of its own. This unit is bringup.c (its reset entry, its main loop,
 * the UI, usb.c, ota.c, the store's flash side, the audio ISR: one translation unit on the device too) over
 * sikken_hal.h and hal/fm1_flash.h (the core's VFS overlay reads them in the place of the device's hal/fm1_*.h,
 * sikken.cmake), and the glue that translates to and from the ABI's panel (core-api/fm1core.h). The DSP and the
 * codecs (audio_core.c, native_fx.c, .. 15 units) are their own units (core_rupture.cmake EXTRA_SOURCES), as build.py
 * builds them.
 *
 * THE FIRMWARE RUNS ON A COROUTINE (core-glue/common/coro.h): fm1_cstart, the device's reset entry, from its first
 * line (its boot: the splash, the store, the project; then the main loop, which never returns). It yields where the
 * device waits for its interrupts (sikken_hal.h: fm1_wdt_feed, fm1_delay_ms) and is resumed once per device ms by
 * frame() / idle(). What runs in its place:
 *   TIMER5 (fm1_timer5_irq: the input scan, etch_ms, usb_poll)   tick(ms): etch_ms / fm1_ms = the device ms, the
 *            panel's keys, buttons and encoder detents into fm1_in, debounced as below; USB is configured
 *   ALNK0 (fm1_alnk0_irq)    audio(): once per 128-frame block, its DMA half (Q15 << 7) out as s16
 *   EP1 (usb_poll's MIDI)    midi_in: usb.c midi_in_event (the MIDI ring, the SysEx frame ota.c answers);
 *                            midi_out_take: usb.c's SysEx reply ring first, then its MIDI out ring
 * Input: the main loop reads the buttons every 25 ms (bringup.c: their level, its own edges) and the audio ISR the
 * keys every block, so a press is held at least FM1CORE_SK_HOLD_MS here (a tap shorter than the main loop's poll
 * would be lost; a finger is never that short). MASTER: the pot, 0..1023.
 * Output: the LEDs as levels (fm1_led lit, fm1_led_mid MID, fm1_led_dim DIM; the first wins), the LCD (lcd.c's
 * window writes decoded, sikken_hal.h).
 * Not here (the hardware): USB audio (ETCH_NATIVE_UAC 0: the device's --usb-audio-48k feeds the computer, a core's
 * audio is its output), the TRS jack, the update session (usb.ota_req: M-UPGRADE's F0 22 24 35 7F F7) and the USB
 * UBOOT key (usb.uboot_req: F0 22 24 35 7D F7): both are dropped as they arrive, the core plays on
 * (core-glue/common/README.md, sysex). OCT- + OCT+ held 5 s (UBOOT), OCT- held at power-on, a second failed boot in
 * a row (the boot guard: boot_options): exit 3, the core halts. */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) */
#include "version.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

/* ---- what the device's linker script gives fm1_cstart: its copy loops (.bss, .pool, .data, .ram_text) run over
 * nothing here (a core's RAM is fresh, its data initialised, at each power-on) ---- */
static uint32_t fm1core_sk_lnk[1];
#define _bss_start fm1core_sk_lnk
#define _bss_end fm1core_sk_lnk
#define _pool_start fm1core_sk_lnk
#define _pool_end fm1core_sk_lnk
#define _data_load fm1core_sk_lnk
#define _data_start fm1core_sk_lnk
#define _data_end fm1core_sk_lnk
#define _rt_load fm1core_sk_lnk
#define _rt_start fm1core_sk_lnk
#define _rt_end fm1core_sk_lnk

/* libc.c is the device's (freestanding, included by bringup.c): its memset / memcpy / memcmp kept off the host's */
#define memset rupture_memset
#define memcpy rupture_memcpy
#define memcmp rupture_memcmp
#include "bringup.c"                      /* (the patched copy: core_rupture.cmake) */
#undef memset
#undef memcpy
#undef memcmp

/* the page and project of the dump (sikken_glue.h) */
#define SIKKEN_PAGE_NAME() (ru_page < RU_PAGE_COUNT ? ru_page_names[ru_page] : "?")
#define SIKKEN_PROJECT ru_project_slot
#include "sikken_glue.h"
