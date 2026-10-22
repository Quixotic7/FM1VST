/* SPDX-License-Identifier: GPL-3.0-only */
/* The Tonitor core's flash HAL: what main.c's `#include "fm1_flash.h"` (the device's hal/fm1_flash.h, the SPI NOR
 * driver that runs from RAM) gives it, on core-glue/common/nor.c's 1 MiB image. This folder is on the unit's include
 * path in the place of the firmware's hal/ (core.cmake INCLUDES), so main.c finds this file. The device's rules hold
 * (nor.c): an erase is a 4 KiB sector of 0xFF and silences the audio for its ~45 ms (the device's interrupts are off
 * while it erases: main.c fl_erase4k_quiet), a program only clears bits. irq_save / irq_restore are tonitor_hal.h's. */
#pragma once
#include <stdint.h>
#include "nor.h"

#define FL_FAR(fn) (fn)                            /* (the device: a call into RAM code from the flash) */
#define FL_DATA_LO 0x00097000u                     /* Felucca's main store: Tonitor's sessions (sessions.c SES_LO) */
#define FL_DATA_HI 0x000E0000u
#define FL_GLOB_LO 0x000FC000u
#define FL_GLOB_HI 0x000FF000u
#define FL_OTA_LO 0x000E0000u                      /* the update loader's staging (ota.c) */
#define FL_OTA_HI 0x000E5000u
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI))
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))

static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's 1 MiB part */
static int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int fl_erase4k_ram(uint32_t off, uint32_t *took_us)
{
    if (took_us)
        *took_us = 0;                              /* (its time: nor.c's stall, in the audio) */
    return fm1nor_erase(off);
}
static int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took_us)
{
    if (took_us)
        *took_us = 0;
    return fm1nor_prog(off, src, n);
}
static int fl_erase4k(uint32_t off, uint32_t *took_us) { return fl_erase4k_ram(off, took_us); }
static int fl_write(uint32_t off, const uint8_t *src_ram, uint32_t n) { return fm1nor_prog_span(off, src_ram, n); }
static void fl_plain_window_init(void) {}
