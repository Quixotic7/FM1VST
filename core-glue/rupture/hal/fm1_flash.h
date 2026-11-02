/* SPDX-License-Identifier: GPL-3.0-only */
/* The RUPTURE core's flash HAL: what the device's hal/fm1_flash.h (the SPI NOR driver that runs from RAM) gives
 * wave_store_target.c, ota.c and ota_target.c, on core-glue/common/nor.c's 1 MiB image. The core's VFS overlay
 * (core.cmake) reads this file in its place. The device's rules hold: the ranges (FL_RANGE_OK, which
 * wave_store_target.c sets before it includes this file, and FL_ERASE_RANGE_OK, its WIPE ALL window), a page program
 * within one 256-byte page that only clears bits, a 4 KiB erase of 0xFF that silences the audio for its ~45 ms
 * (nor.c's stall: the device's interrupts are off while it erases). Each operation marks fm1core_ru_flash_op, so the
 * fm1_wdt_feed the firmware calls after it does not yield (rupture_hal.h). irq_save / irq_restore only count. */
#pragma once
#include <stdint.h>
#include "rupture_hal.h"

#define RAMFN
#define RAMINL static inline
#define FL_FAR(fn) (fn)                            /* (the device: a call into RAM code from the flash) */
#define FL_XIP(off) FM1_XIP(off)
#define FL_DATA_LO 0x00097000u                     /* Felucca main store */
#define FL_DATA_HI 0x000E0000u
#define FL_GLOB_LO 0x000FC000u                     /* Felucca superblock / globals */
#define FL_GLOB_HI 0x000FF000u
#define FL_OTA_LO 0x000E0000u                      /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI 0x000E5000u
#define FL_AUTO_LO 0x000E5000u
#define FL_AUTO_HI 0x000E7000u
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                             FL_IN(off, n, FL_AUTO_LO, FL_AUTO_HI))
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif
#ifndef FL_ERASE_RANGE_OK
#define FL_ERASE_RANGE_OK(off, n) FL_RANGE_OK(off, n)
#endif
#ifndef ETCH_FLASH_READ_ONLY
#define ETCH_FLASH_READ_ONLY 0
#endif
#if ETCH_FLASH_READ_ONLY && (ETCH_NATIVE_STORAGE || ETCH_NATIVE_OTA)
#error "Read-only build cannot include persistent storage or OTA writers"
#endif

static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's 1 MiB part */
static uint32_t fl_status_ram(void) { return 0; }
static int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n)
{
    fm1core_ru_flash_op = 1;
    return fm1nor_read(off, dst, n) ? -2 : 0;
}
#if !ETCH_FLASH_READ_ONLY
static int fl_erase4k_ram(uint32_t off, uint32_t *took_us)
{
    fm1core_ru_flash_op = 1;
    if (took_us)
        *took_us = 0;                              /* (its time: nor.c's stall, in the audio) */
    if (!FL_ERASE_RANGE_OK(off, 0x1000u) || (off & 0xFFFu))
        return -1;
    return fm1nor_erase(off) ? -2 : 0;
}
static int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took_us)
{
    fm1core_ru_flash_op = 1;
    if (took_us)
        *took_us = 0;
    if (!FL_RANGE_OK(off, n) || n == 0 || n > 256u || ((off & 0xFFu) + n) > 256u)
        return -1;                                 /* one page, no wrap */
    return fm1nor_prog(off, src, n) ? -2 : 0;
}
#else
static int fl_erase4k_ram(uint32_t off, uint32_t *took)
{
    (void)off;
    (void)took;
    return -1;
}
static int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took)
{
    (void)off;
    (void)src;
    (void)n;
    (void)took;
    return -1;
}
#endif
static void fl_plain_window_init(void) {}
static uint32_t irq_save(void)
{
    fm1_irq_off();
    return 0;
}
static void irq_restore(uint32_t f)
{
    (void)f;
    fm1_irq_on();
}
static int fl_erase4k(uint32_t off, uint32_t *took_us) { return fl_erase4k_ram(off, took_us); }
static int fl_write(uint32_t off, const uint8_t *src_ram, uint32_t n)
{
    uint32_t took;
    while (n) {
        uint32_t chunk = 256u - (off & 0xFFu);
        int rc;
        if (chunk > n)
            chunk = n;
        rc = fl_prog_ram(off, src_ram, chunk, &took);
        if (rc)
            return rc;
        off += chunk;
        src_ram += chunk;
        n -= chunk;
    }
    return 0;
}
static int fl_verify_xip(uint32_t off, const uint8_t *ref, uint32_t n)
{
    const uint8_t *p = fm1nor_image + off;
    uint32_t i;
    if (off >= FM1NOR_SIZE || n > FM1NOR_SIZE - off)
        return -1;
    for (i = 0; i < n; i++)
        if (p[i] != ref[i])
            return -(int)(i + 1);
    return 0;
}
