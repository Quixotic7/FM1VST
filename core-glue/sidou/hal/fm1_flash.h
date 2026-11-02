/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_flash.h on the host (midi.c, its one includer): the SPI NOR driver on
 * core-glue/common/nor.c's 1 MiB image. The device's rules hold (nor.c): an erase is a 4 KiB sector of 0xFF and
 * silences the audio for its ~45 ms (core_sidou.c's audio takes nor.c's stall), a program only clears bits. The
 * address macros are the device header's, unchanged. */
#pragma once
#include <stdint.h>
#include "fm1_xip.h"
#include "nor.h"

#define RAMFN
#define RAMINL static inline
#define FL_FAR(fn) (fn)

#define FL_XIP(off)     FM1_XIP(off)
#define FL_DATA_LO      0x00097000u
#define FL_DATA_HI      0x000E0000u
#define FL_GLOB_LO      0x000FC000u
#define FL_GLOB_HI      0x000FF000u
#define FL_OTA_LO       0x000E0000u
#define FL_OTA_HI       0x000E5000u
#define FL_AUTO_LO      0x000E5000u
#define FL_AUTO_HI      0x000E7000u
#define FL_VBANK_LO     0x000E7000u
#define FL_VBANK_HI     0x000E8000u
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                             FL_IN(off, n, FL_AUTO_LO, FL_VBANK_HI))
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif

static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's 1 MiB part */
static uint32_t fl_status_ram(void) { return 0u; }
static int fl_erase4k_ram(uint32_t off, uint32_t *took_us)
{
    if (took_us)
        *took_us = 0;
    if (!FL_RANGE_OK(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int fl_prog_ram(uint32_t off, const uint8_t *src, uint32_t n, uint32_t *took_us)
{
    if (took_us)
        *took_us = 0;
    if (!FL_RANGE_OK(off, n) || n > 256u || (off & 0xFFu) + n > 256u)
        return -8;
    return fm1nor_prog(off, src, n);
}
static int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static void fl_plain_window_init(void) {}
static inline uint32_t irq_save(void) { return 0; }
static inline void irq_restore(uint32_t f) { (void)f; }
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
    const uint8_t *p = (const uint8_t *)FL_XIP(off);
    uint32_t i;
    for (i = 0; i < n; i++)
        if (p[i] != ref[i])
            return -(int)(i + 1);
    return 0;
}
