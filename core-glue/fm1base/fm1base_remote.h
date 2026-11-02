/* SPDX-License-Identifier: GPL-3.0-only */
/* fm1-base's remote control (base/device/main_fm1.c remote_frame, MPL-2.0: the glue does not build main_fm1.c, so its
 * body is here, as it is) for the fm1-base cores, included after plat_fm1.c (its remote state) and plat_sx.h:
 *
 *   F0 7D 4C 43 ("LC") op .. F7, 7-bit bytes, added to the panel's own input (plat_fm1.c remote):
 *     1 b0 b1          the buttons held (B_* bits)
 *     2 k0 k1 k2 k3    the keys held
 *     3 role delta     an encoder turned (delta - 64 detents)
 *     4 lo hi          MASTER's position (0..4096; more: the pot's own again)
 *     5 on             (FM1CORE_FB_REMOTE_LEASE 1: an fm1-base with plat_fm1.c's remote_lease) the browser editor's
 *                      lease, renewed by any remote frame; stale controls let go (remote_expire)
 *   answered F0 7D 4C 43 op F7; an op it does not know is taken, not answered (as on the device).
 * The core's accept filter takes the frame with fm1core_pl_sx_is(f, n, 0x4C, 0x43), and its service asks
 * fm1core_fb_remote_frame in main_fm1.c's order (after the screen reader and the log, before app_sysex). */
#pragma once
#ifndef FM1CORE_FB_REMOTE_LEASE
#define FM1CORE_FB_REMOTE_LEASE 0
#endif

static int fm1core_fb_remote_frame(const uint8_t *p, uint32_t n)
{
    uint8_t ack[6] = {0xF0, 0x7D, 0x4C, 0x43, 0, 0xF7};
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x43u)
        return 0;
#if FM1CORE_FB_REMOTE_LEASE
    if (remote_lease)
        remote_lease = fm1_ms + 1800u;
#endif
    switch (p[3]) {
#if FM1CORE_FB_REMOTE_LEASE
    case 5:
        if (n == 5u)
            remote_lease = p[4] ? fm1_ms + 1800u : 0u;
        break;
#endif
    case 1:
        if (n >= 6u)
            remote.btn = ((uint32_t)p[4] | (uint32_t)p[5] << 7) & ((1u << NB) - 1u);
        break;
    case 2:
        if (n >= 8u)
            remote.keys = ((uint32_t)p[4] | (uint32_t)p[5] << 7 | (uint32_t)p[6] << 14 | (uint32_t)p[7] << 21) &
                          ((1u << NKEYS) - 1u);
        break;
    case 3:
        if (n >= 6u && p[4] < NE)
            remote.enc[p[4]] += (int32_t)p[5] - 64;
        break;
    case 4:
        if (n >= 6u) {
            uint32_t v = (uint32_t)p[4] | (uint32_t)p[5] << 7;
            remote.master = v > 4096u ? -1 : (int32_t)v;
        }
        break;
    default:
        return 1;
    }
    ack[4] = p[3];
    fm1core_pl_sx_send(ack, sizeof ack);
    return 1;
}
