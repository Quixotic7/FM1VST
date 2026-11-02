/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_usb.h on the host. Felucca's usb.c is built for its rings, its SysEx assembly and
 * the update protocol's replies (core-glue/sidou/sidou_midi.c); its SIE poll (usb_poll) never runs. usb_start runs
 * once at boot (midi_init): the SIE answers at once, nothing is sent. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

static volatile uint32_t fm1core_sd_usb_reg[16];
#define FM1_USB_CON0 (fm1core_sd_usb_reg[0])
#define FM1_USB_CON1 (fm1core_sd_usb_reg[1])
#define FM1_USB_EP_CNT(ep) (fm1core_sd_usb_reg[2])
#define FM1_USB_EP0_ADR (fm1core_sd_usb_reg[3])
#define FM1_USB_EP_TADR(ep) (fm1core_sd_usb_reg[4])
#define FM1_USB_EP_RADR(ep) (fm1core_sd_usb_reg[5])
#define FM1_USB_EP4_CNT (fm1core_sd_usb_reg[6])
#define FM1_USB_EP4_TADR (fm1core_sd_usb_reg[7])
#define FM1_USB_EP4_RADR (fm1core_sd_usb_reg[8])
#define FM1_USB_IO_CON0 (fm1core_sd_usb_reg[9])
#define FM1_CLK_CON1 (fm1core_sd_usb_reg[10])
FM1_INLINE void fm1_usb_reset(void) {}
FM1_INLINE void fm1_usb_attach(void *ep0) { (void)ep0; }
FM1_INLINE void fm1_usb_off(void) {}
FM1_INLINE uint32_t fm1_usb_sie_on(void) { return 4u; }
FM1_INLINE void fm1_usb_sie_wr_start(uint32_t r, uint32_t v)
{
    (void)r;
    (void)v;
}
FM1_INLINE void fm1_usb_sie_rd_start(uint32_t r) { (void)r; }
FM1_INLINE uint32_t fm1_usb_sie_done(void) { return 0x8000u; }
FM1_INLINE uint32_t fm1_usb_sie_data(void) { return 0u; }
FM1_INLINE void fm1_usb_ep0_buf(void *p) { (void)p; }
FM1_INLINE void fm1_usb_ep_txbuf(uint32_t ep, void *p)
{
    (void)ep;
    (void)p;
}
FM1_INLINE void fm1_usb_ep_rxbuf(uint32_t ep, void *p)
{
    (void)ep;
    (void)p;
}
FM1_INLINE void fm1_usb_ep0_send(void *p, uint32_t n)
{
    (void)p;
    (void)n;
}
FM1_INLINE void fm1_usb_ep_send(uint32_t ep, void *p, uint32_t n)
{
    (void)ep;
    (void)p;
    (void)n;
}
FM1_INLINE void fm1_usb_ep4_txbuf(void *p) { (void)p; }
FM1_INLINE void fm1_usb_ep4_send(void *p, uint32_t n)
{
    (void)p;
    (void)n;
}
FM1_INLINE void fm1_usb_rx_sync(void) {}
FM1_INLINE void fm1_usb_ep_enable(uint32_t eps) { (void)eps; }
FM1_INLINE uint32_t fm1_usb_sof_take(void) { return 0u; }
