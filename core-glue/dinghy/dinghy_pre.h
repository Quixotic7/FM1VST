/* SPDX-License-Identifier: GPL-3.0-only */
/* The Dinghy family glue, part 1 of 2: included by each Dinghy core's unit (core_<id>.c) BEFORE the firmware's
 * sources. Dinghy (zvenson's FM-1 platform: OberVave, Frodhes, FM-106) apps supply app_audio(int32_t *, 256) (the
 * audio ISR, Q15 stereo) and app_main(), which never returns and calls platform_poll() every pass. Each app runs on
 * its own host shim (the fake FM-1 its tests / web build use): fm1_in, host_edges, host_enc[], fb[], mq[] and the RAM
 * stores. The glue (dinghy_glue.h, after the firmware) runs app_main as a coroutine (common/coro), resumed once per
 * device millisecond from tick(); the shim's poll / sleep points yield (fm1core_dg_poll / fm1core_dg_sleep below).
 *
 * This header: the C library before the firmware (exit / atexit redirected: common/exit.h), the ABI and the common
 * helpers, and what the firmware's shims call into the glue before it is defined (MIDI out, the poll, the NOR
 * sector format). Every glue name is prefixed fm1core_ (core-glue/common/README.md pitfalls). */
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include <math.h>
#include "fm1core.h"
#include "version.h"
#include "ticks.h"
#include "nor.h"
#include "coro.h"
#include "fifo.h"
#include "panel.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */

/* ---- what the firmware's shim calls (defined here: they need nothing of the firmware) */

/* the device's poll point (platform_poll): one pass of the main loop per device millisecond */
static void fm1core_dg_poll(void) { fm1coro_yield(); }
/* a wait of ms device milliseconds (emscripten_sleep, delay loops): yield until the clock has moved on */
static void fm1core_dg_sleep(uint32_t ms)
{
    const uint32_t t0 = fm1t_ms();
    do
        fm1coro_yield();
    while (fm1coro_inside() && fm1t_ms() - t0 < (ms ? ms : 1u));
}

/* MIDI out: USB-MIDI packets, cable 0, as Dinghy's platform.c midi_send builds them (CIN = status >> 4) */
#define FM1CORE_DG_MQ 512u
static uint32_t fm1core_dg_mo[FM1CORE_DG_MQ], fm1core_dg_mo_r, fm1core_dg_mo_w;
static void fm1core_dg_midi_pkt(uint32_t pkt)
{
    if (fm1core_dg_mo_w - fm1core_dg_mo_r >= FM1CORE_DG_MQ)
        fm1core_dg_mo_r++;                /* (nobody takes it: the oldest goes) */
    fm1core_dg_mo[fm1core_dg_mo_w++ % FM1CORE_DG_MQ] = pkt;
}
static void fm1core_dg_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    st &= 0xFFu;
    fm1core_dg_midi_pkt((st >> 4) | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}
/* a SysEx message (F0 .. F7) as USB-MIDI packets: CIN 4 (three bytes, more follow), then 5 / 6 / 7 (the last 1..3) */
static void fm1core_dg_sysex_out(const uint8_t *p, uint32_t n)
{
    while (n) {
        const uint32_t k = n > 3u ? 3u : n, last = n <= 3u;
        const uint32_t cin = last ? 4u + k : 4u;
        uint32_t pkt = cin, i;
        for (i = 0; i < k; i++)
            pkt |= (uint32_t)p[i] << (8u * (i + 1u));
        fm1core_dg_midi_pkt(pkt);
        p += k;
        n -= k;
    }
}

/* ---- the NOR in Dinghy's sector format (platform.c sector_load / sector_save): [magic "DNGY"][length][crc32] then
 * the bytes, one 4 KiB sector; the header written last */
#define FM1CORE_DG_MAGIC 0x59474E44u
#define FM1CORE_DG_SECTOR_MAX (4096u - 12u)
static uint32_t fm1core_dg_crc32(const uint8_t *p, uint32_t n)   /* platform.c crc32 */
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
/* the record at `at`: 0 and its payload (*n bytes, at most max) copied, else -1 (p untouched) */
static int fm1core_dg_sector_read(uint32_t at, void *p, uint32_t max, uint32_t *n)
{
    uint32_t h[3];
    if (fm1nor_read(at, h, sizeof h) || h[0] != FM1CORE_DG_MAGIC || h[1] > max || h[1] > FM1CORE_DG_SECTOR_MAX)
        return -1;
    if (fm1core_dg_crc32(fm1nor_image + at + sizeof h, h[1]) != h[2])
        return -1;
    memcpy(p, fm1nor_image + at + sizeof h, h[1]);
    *n = h[1];
    return 0;
}
/* sector_load: the record must hold exactly n bytes */
static int fm1core_dg_sector_load(uint32_t at, void *p, uint32_t n)
{
    static uint8_t buf[FM1CORE_DG_SECTOR_MAX];
    uint32_t got = 0;
    if (n > sizeof buf || fm1core_dg_sector_read(at, buf, sizeof buf, &got) || got != n)
        return -1;
    memcpy(p, buf, n);
    return 0;
}
/* sector_save: erase (the device's audio stall: common/nor.c), the bytes, then the header */
static int fm1core_dg_sector_save(uint32_t at, const void *p, uint32_t n)
{
    uint32_t h[3] = {FM1CORE_DG_MAGIC, n, 0};
    if (n > FM1CORE_DG_SECTOR_MAX)
        return -1;
    h[2] = fm1core_dg_crc32((const uint8_t *)p, n);
    if (fm1nor_erase(at) || fm1nor_prog_span(at + sizeof h, p, n))
        return -8;
    return fm1nor_prog_span(at, h, sizeof h);
}
/* a raw 4 KiB sector the app keeps as a RAM image in the device's own layout (Frodhes' preset banks): written as the
 * device would (erase, program) when the RAM copy differs from the flash; 1 if written */
static int fm1core_dg_raw_sync(uint32_t at, const uint8_t *ram)
{
    if (!memcmp(fm1nor_image + at, ram, FM1NOR_SECTOR))
        return 0;
    fm1nor_erase(at);
    fm1nor_prog_span(at, ram, FM1NOR_SECTOR);
    return 1;
}
