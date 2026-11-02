/* SPDX-License-Identifier: GPL-3.0-only */
/* RemoteFM1's port (src/rfm_main.c's rp_*) in the core: read in place of the firmware's src/rfm_dev.c (a clang VFS
 * overlay, core_remotefm1.cmake), whose stand-ins are felucca-bsp's usb.c with RemoteFM1's vendor interface and the
 * panel's DMA. A core has neither: here the host's MIDI is the USB port (plat_glue.h's midi_in_q / midi_out_event),
 * nothing waits, nothing is ever full, and there is no vendor bulk pair and no Bluetooth (the .rfm1 is the
 * standalone instrument: no host session is expected, and a host that says HELLO over USB SysEx still gets its
 * replies on the USB out ring).
 *
 * USB SysEx in: usb.c hands every SysEx byte of the USB port to rf_sx_tap (usb_poll's context) and never queues it;
 * rp_service, which the loop calls before it reads MIDI in (rf_turn: rf_poll, then rf_rx_step), does the same with
 * the SysEx packets the host put in midi_in_q: their bytes to rf_sx_tap in order (a DX7 bank dump for the user
 * cartridges, sa_ubank.c; RemoteFM1's own F0 7D 52 46 messages), the other packets left in the ring, in order. */

static uint8_t rp_in_sx;                            /* the USB port is inside a SysEx (a status byte ends it) */
static void rp_service(void)
{
    uint32_t i, k;
    int any = 0;
    for (i = mi_r; i != mi_w; i++) {                /* the SysEx bytes, in order */
        const uint32_t p = midi_in_q[i % MQ], cin = p & 15u, st = p >> 8 & 0xFFu;
        if ((p >> 4 & 15u) != 0u)
            continue;                               /* (cable 1: the TRS jack's, none here) */
        if (cin == 4u || cin == 6u || cin == 7u || (cin == 5u && (st == 0xF7u || st < 0x80u))) {
            const uint32_t nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
            uint32_t j;
            for (j = 0; j < nb; j++) {
                const uint32_t b = p >> (8u * (j + 1u)) & 0xFFu;
                rf_sx_tap(b);
                rp_in_sx = (uint8_t)(b != 0xF7u);
            }
            any = 1;
        } else if (rp_in_sx && cin != 15u && st >= 0x80u && st < 0xF8u) {
            rf_sx_tap(st);                          /* (a status ends a SysEx cut short; the packet stays) */
            rp_in_sx = 0;
        }
    }
    if (!any)
        return;
    k = mi_w;                                       /* the rest kept, in order, ending at mi_w (rf_ep1_room's way) */
    for (i = mi_w; i != mi_r;) {
        const uint32_t p = midi_in_q[--i % MQ], cin = p & 15u, st = p >> 8 & 0xFFu;
        if ((p >> 4 & 15u) == 0u && (cin == 4u || cin == 6u || cin == 7u || (cin == 5u && (st == 0xF7u || st < 0x80u))))
            continue;
        midi_in_q[--k % MQ] = p;
    }
    RING_PUBLISH();
    mi_r = k;
}

static int rp_usb_cfg(void) { return usb.up && usb.config; }
static int rp_usb_live(void) { return usb.up && usb.config && !usb.suspended; }
static uint32_t rp_usb_resets(void) { return 0; }

/* SysEx out: a whole message's packets onto the USB out ring (plat_hal.h midi_out_event: dropped there when the host
 * reads none). Always room: rf_send_ms would otherwise wait for it, and simulated time does not move inside a pass */
static uint32_t rp_sx_room(void) { return SXQ; }
static void rp_sx_begin(void) {}
static void rp_sx_put(uint32_t pkt) { midi_out_event(pkt); }
static void rp_sx_end(void) { RING_PUBLISH(); }

/* no vendor bulk pair (WebUSB): nothing comes, everything sent is taken */
static uint32_t rp_bk_avail(void) { return 0; }
static uint32_t rp_bk_read(uint8_t *dst, uint32_t n)
{
    (void)dst, (void)n;
    return 0;
}
static uint32_t rp_bk_room(void) { return 0x10000u; }
static int rp_bk_send(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb)
{
    (void)a, (void)na, (void)b, (void)nb;
    return 1;
}
static uint32_t rp_bk_rx_n(void) { return 0; }
static uint32_t rp_bk_tx_n(void) { return 0x10000u; }

/* no Bluetooth (radio_none.c: never connected) */
static void rp_ble_put(uint32_t pkt) { (void)pkt; }

static void rp_lcd_baud(uint32_t b) { (void)b; }
static void rp_wdt(void) {}

#if !RFM_SHADOW
static void rp_wait_idle(void) {}                   /* (lcd_blit is done when it returns) */
#endif

static void rp_counters(uint32_t *c)
{
    uint32_t i;
    for (i = 0; i < RC_N; i++)
        c[i] = 0;
}
