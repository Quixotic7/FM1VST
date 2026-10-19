/* SPDX-License-Identifier: GPL-3.0-only */
/* The managers' SysEx transport for a core (core-glue/common/README.md "sysex"): what the device's usb.c does around
 * its SysEx service, for a glue whose firmware unit leaves it out (FELUCCA_OTA 0, the web units, the host builds).
 *
 *   in   fm1sx_in_pkt(&in, pkt)   every USB-MIDI packet the core's midi_in takes (it also goes to the firmware as
 *        before: its own parsers, DX7 / CZ-1 receive, see each byte). The bytes between F0 and F7 of a frame whose
 *        first bytes the glue's filter accepts (a manager's header: 7D 46 4C, the M-UPGRADE 00 32 45 ..) are kept in
 *        ONE frame buffer, as usb.c's sx_frame: a frame that is too long is not ours (dropped). Any other frame
 *        (notes, a DX7 bank) is never held. While a kept frame waits for its service, a new accepted frame's F0 is
 *        refused: fm1sx_in_pkt returns 0 and the core's midi_in returns 0, "no room, retry" (the host keeps the
 *        packet and offers it again after its next render), so no request is lost and nothing else is held back.
 *        fm1sx_frame(&in, &p, &n) gives the waiting frame to the service (the main loop), fm1sx_done(&in) frees it.
 *   out  fm1sx_send(&out, p, n)    one reply (or a part of one, CZ-1 style) into a ring of CIN 4..7 packets, as usb.c
 *        ota_wire_send packs them: CIN 4 continues, CIN 5 / 6 / 7 end a frame at its F7 only. Whole or nothing: -1
 *        when the ring cannot take it (the device's send gives up after 200 ms of a full ring the same way).
 *        fm1sx_take(&out, &pkt): the core's midi_out_take serves this ring FIRST, the firmware's notes only when it
 *        is empty (usb.c ep1_tx: "SysEx first, never split by notes").
 *
 * Sizes: FM1SX_IN_MAX bytes of a request (default 640: usb.c sx_frame, OTA_FRAME_MAX; a glue may set more before
 * including this file), FM1SX_OUT_PKTS packets of replies queued (default 4096 = 12 KiB of SysEx: the largest reply
 * of any manager service, a Melodee bank or ChoralRoot's 1 KiB, many times over). All static, no allocation, no lock:
 * the core's one clock thread (fm1core.h THREADING CONTRACT) is the only caller. */
#pragma once
#include <stdint.h>

#ifndef FM1SX_IN_MAX
#define FM1SX_IN_MAX 640u
#endif
#ifndef FM1SX_OUT_PKTS
#define FM1SX_OUT_PKTS 4096u           /* a power of two */
#endif
#define FM1SX_PREFIX 4u                /* the bytes after F0 the filter sees (a shorter frame: at its F7) */

/* 1: the frame that starts with f[0..n) (n = FM1SX_PREFIX, or fewer when the frame ended sooner) is for the
 * service; 0: not ours, never held */
typedef int (*fm1sx_accept_fn)(const uint8_t *f, uint32_t n);

typedef struct {
    uint8_t f[FM1SX_IN_MAX];           /* the frame's bytes between F0 and F7 */
    uint32_t n;
    uint8_t on;                        /* inside a frame (F0 seen) */
    uint8_t keep;                      /* 1: collecting (accepted or not decided yet), 0: dropped */
    uint8_t ready;                     /* a whole accepted frame waits for the service */
    fm1sx_accept_fn accept;
    uint32_t frames, dropped, refused; /* counters (dump) */
} fm1sx_in_t;

typedef struct {
    uint32_t q[FM1SX_OUT_PKTS];
    uint32_t w, r;
    uint32_t frames, lost;             /* counters (dump): replies queued, replies the ring could not take */
} fm1sx_out_t;

static void fm1sx_in_init(fm1sx_in_t *s, fm1sx_accept_fn accept)
{
    s->n = 0;
    s->on = s->keep = s->ready = 0;
    s->accept = accept;
}

/* one byte of the SysEx stream (usb.c sysex_byte's rules: real-time bytes anywhere, another status aborts) */
static void fm1sx_in_byte(fm1sx_in_t *s, uint8_t b)
{
    if (b >= 0xF8u)
        return;
    if ((b & 0x80u) && b != 0xF0u && b != 0xF7u) {
        s->on = s->keep = 0;
        return;
    }
    if (b == 0xF0u) {
        s->on = 1;
        s->keep = !s->ready;
        s->n = 0;
        return;
    }
    if (!s->on)
        return;
    if (b == 0xF7u) {
        s->on = 0;
        if (s->keep && s->n && s->n < FM1SX_PREFIX && !(s->accept && s->accept(s->f, s->n)))
            s->keep = 0;
        if (s->keep && s->n) {
            s->ready = 1;
            s->frames++;
        }
        s->keep = 0;
        return;
    }
    if (!s->keep)
        return;
    if (s->n >= FM1SX_IN_MAX) {        /* too long: not ours (usb.c) */
        s->keep = 0;
        s->dropped++;
        return;
    }
    s->f[s->n++] = b;
    if (s->n == FM1SX_PREFIX && !(s->accept && s->accept(s->f, s->n)))
        s->keep = 0;
}

/* 1: the packet was taken (or carries no SysEx); 0: a new frame's F0 while one waits: retry the packet later.
 * A packet refused here must not have been given to the firmware either (fm1sx_in_would_refuse first). */
static int fm1sx_in_would_refuse(const fm1sx_in_t *s, uint32_t pkt)
{
    const uint32_t cin = pkt & 15u, b0 = (pkt >> 8) & 0xFFu;
    return s->ready && b0 == 0xF0u && ((cin >= 4u && cin <= 7u) || cin == 0xFu);
}
static int fm1sx_in_pkt(fm1sx_in_t *s, uint32_t pkt)
{
    const uint32_t cin = pkt & 15u, b0 = (pkt >> 8) & 0xFFu;
    if (fm1sx_in_would_refuse(s, pkt)) {
        s->refused++;
        return 0;
    }
    if (cin >= 4u && cin <= 7u) {
        const uint32_t nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        uint32_t k;
        for (k = 0; k < nb; k++)
            fm1sx_in_byte(s, (uint8_t)(pkt >> (8u * (k + 1u))));
    } else if (cin == 0xFu && b0 < 0xF8u && (s->on || b0 == 0xF0u)) {
        fm1sx_in_byte(s, (uint8_t)b0);  /* (a SysEx byte sent as a single byte: usb.c midi_in_event) */
    } else if (b0 >= 0x80u && b0 < 0xF8u && cin >= 2u) {
        s->on = s->keep = 0;            /* channel / system common also ends SysEx */
    }
    return 1;
}
static int fm1sx_frame(const fm1sx_in_t *s, const uint8_t **p, uint32_t *n)
{
    if (!s->ready)
        return 0;
    *p = s->f;
    *n = s->n;
    return 1;
}
static void fm1sx_done(fm1sx_in_t *s) { s->ready = 0; }

static void fm1sx_out_reset(fm1sx_out_t *o) { o->r = o->w; }
static uint32_t fm1sx_out_queued(const fm1sx_out_t *o) { return o->w - o->r; }
/* p[0..n): F0 .. F7, or a part of a frame (no F7 at its end: the frame goes on in the next call) */
static int fm1sx_send(fm1sx_out_t *o, const uint8_t *p, uint32_t n)
{
    uint32_t i = 0;
    if (!n)
        return 0;
    if ((n + 2u) / 3u > FM1SX_OUT_PKTS - (o->w - o->r)) {
        o->lost++;
        return -1;
    }
    while (i < n) {
        const uint32_t k = n - i >= 3u ? 3u : n - i;
        const uint32_t cin = i + k < n || p[n - 1u] != 0xF7u ? 4u : 4u + k;
        o->q[o->w % FM1SX_OUT_PKTS] = cin | (uint32_t)p[i] << 8 | (k > 1u ? (uint32_t)p[i + 1] << 16 : 0u) |
                                      (k > 2u ? (uint32_t)p[i + 2] << 24 : 0u);
        o->w++;
        i += k;
    }
    if (p[n - 1u] == 0xF7u)
        o->frames++;
    return 0;
}
static int fm1sx_take(fm1sx_out_t *o, uint32_t *pkt)
{
    if (o->r == o->w)
        return 0;
    *pkt = o->q[o->r % FM1SX_OUT_PKTS];
    o->r++;
    return 1;
}

/* the managers' headers: the Felucca editor / backup protocol "FL" (7D 46 4C) and the M-UPGRADE updater's
 * pack7(00 59 ..) frames, whose first two wire bytes are 00 32 (pack7 of 00 59: 00, 0x59 << 1 & 0x7F | 0 = 32) */
static int fm1sx_is_fl(const uint8_t *f, uint32_t n)
{
    return n >= 3u && f[0] == 0x7Du && f[1] == 0x46u && f[2] == 0x4Cu;
}
static int fm1sx_is_mupg(const uint8_t *f, uint32_t n) { return n >= 2u && f[0] == 0x00u && f[1] == 0x32u; }
