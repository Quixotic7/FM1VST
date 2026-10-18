/* SPDX-License-Identifier: GPL-3.0-only */
/* The fm1-base family's HAL (core-glue/fm1base/README.md): what Charles Vestal's fm1-base platform (base/device:
 * unity.c, plat_fm1.c, panel.c, storage.c, radio_none.c) takes from felucca-bsp, on the host. It is the plat family's
 * HAL half (core-glue/plat/plat_hal.h: time, the debounced matrix, the encoders, the LED planes, the LCD, usb.c's MIDI
 * rings, storage.c's flash hooks on core-glue/common/nor.c) plus what fm1-base's plat_fm1.c reaches beyond it:
 *   fm1_led_dim_level   the glow's two strengths (DIM LO / DIM HI)
 *   fm1_irq_off / on    plat_lock (the 1 kHz tick's lock: one thread here)
 *   fm1_an, fm1_adc_*   the analog rows (plat_sense: the keys' readings; the converter is the scan's then)
 *   FL_IN, FL_XIP       the data area read in place (plat_data: the NOR image itself)
 *   ota_wire_send       a SysEx frame out (plat_sysex_send: no SysEx path here, dropped)
 * Included by an fm1-base core's unit after fm1core.h, nor.h, ticks.h and panel.h, before the firmware's sources. */
#pragma once
#include "../plat/plat_hal.h"

static uint8_t fm1core_fb_dim_lo;                 /* the glow: 1 DIM LO, 0 DIM HI (fm1_led_dim_level) */
static void fm1_led_dim_level(uint32_t lo) { fm1core_fb_dim_lo = (uint8_t)(lo != 0); }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}

/* the analog rows: plat_sense(1) hands the converter to the scan (fm1_an.con), which the host's key readings stand
 * in for (lib/touch.c app_host_key / app_host_tick, as the fm1-base host simulator feeds them) */
#define FM1_ADC_CON(a, b) ((uint16_t)(((a) << 8) | (b) | 0x8000u))
#define FM1_ADC_MASTER 0
static struct {
    volatile uint16_t con, pend, master, drive, probe;
} fm1_an;
static int32_t fm1core_fb_master_adc = 512;      /* the pot as the converter reads it (the glue's MASTER) */
static int32_t fm1_adc_read(int ch) { return ch == FM1_ADC_MASTER ? fm1core_fb_master_adc : -1; }

/* the data area (0x97000..0xDFFFF), read in place: the NOR image is the flash the XIP window shows */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (uint32_t)(lo) && (uint32_t)(off) + (uint32_t)(n) <= (uint32_t)(hi))
#define FL_XIP(off) ((const uint8_t *)fm1nor_image + (uint32_t)(off))
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    (void)p;
    (void)n;
    return -1;
}
