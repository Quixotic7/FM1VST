/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's MIDI unit: platform/fm1/midi.c, unmodified (it includes Felucca's usb.c, SIDOU's usb_audio.c, its
 * TRS driver usb/midi_uart.c and Felucca's update protocol src/ota.c, over this folder's hal/), and the core's
 * MIDI in and out on usb.c's rings, in the place of its EP1 (usb_poll, the SIE, never runs):
 *   in    usb.c's ep1_take, packet by packet: midi_in_event (SIDOU's raw hook usb_raw_in into its own ring, usb.c's
 *         SysEx assembly for the update protocol's frame, its filtered copy), refused (0: the host retries after its
 *         next render) while usb.c's ring is short of EP1_ROOM (SIDOU's back-pressure, ep1_hold, sets it so), or
 *         while the update protocol's one frame waits and this packet starts another (usb.c would drop it)
 *   out   usb.c's ep1_tx: the update protocol's replies first, never split by notes, then MIDI out
 * USB is up and configured from the boot on (a host is there: MIDI out flows, SysEx replies go out). */
#include "sidou_glue.h"
#include "midi.c"

#include <stdio.h>

void fm1core_sd_usb_attached(void)
{
    usb.up = 1;
    usb.config = 1;
    usb.suspended = 0;
}

int fm1core_sd_midi_in(uint32_t pkt)
{
    const uint32_t cin = pkt & 15u;
    if (MQ - (mi_w - mi_r) < EP1_ROOM)
        usb_in_pump();                    /* (the main loop's: the copies SIDOU's ring has taken) */
    if (MQ - (mi_w - mi_r) < EP1_ROOM)
        return 0;
    if (cin >= 4u && cin <= 7u && ((pkt >> 8) & 0xFFu) == 0xF0u && sx_ready)
        return 0;
    poll_stamp = fm1_ticks();
    if (usb.up)
        midi_stats.usb_polls++;
    midi_in_event(pkt);
    return 1;
}

int fm1core_sd_midi_out_take(uint32_t *pkt)
{
    if (so_r != so_w) {
        *pkt = sx_out_q[so_r % SXQ];
        RING_PUBLISH();
        so_r++;
        return 1;
    }
    if (sx_busy || mo_r == mo_w)
        return 0;
    RING_PUBLISH();
    *pkt = midi_out_q[mo_r % MQ];
    RING_PUBLISH();
    mo_r++;
    return 1;
}

void fm1core_sd_midi_dump(void)
{
    printf("  midi: %u entries in, %u lost, %u restarts; usb ring in %u out %u, sysex out %u%s; sysex %s\n",
           (unsigned)midi_stats.entries, (unsigned)midi_stats.lost, (unsigned)midi_stats.restarts,
           (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r), (unsigned)(so_w - so_r),
           sx_ready ? ", an update frame waiting" : "", midi_sysex_running ? "running" : "idle");
}
