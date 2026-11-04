/* SPDX-License-Identifier: GPL-3.0-only */
/* OberVave 3.3.2's FM-1 Mirror (firmware/src/obv_mirror.h, included by obervave.c right after platform.c) on a host:
 * it reaches into the device's usb.c (usb.config, its SysEx queue SXQ / so_w / so_r) and ota.c's transport
 * (ota_wire_send, ota_frame_get, ota_frame_done), which tests/host.c does not have. Here the mirror stays idle:
 * host.c paints with its own gfx (no lcd.c), so lcd_mirror_px is never fed, and usb.config is 0, so nothing is sent.
 * The firmware's own SysEx (the M-UPGRADE identity, ota_service) is untouched.
 *
 * core_obervave.c includes this before obervave.c: the ota_* hooks are dinghy_glue.h's (defined after it, on the
 * core's MIDI out; ota_frame_get holds a frame only while platform_poll hands it to ota_service, so mirror_poll,
 * which runs before platform_poll, never takes one). The golden check's host sim (dinghy_golden_check, -include)
 * has no ota.c: OBV_MIRROR_HOST_STUBS gives it do-nothing ones. */
#ifndef OBERVAVE_MIRROR_HOST_H
#define OBERVAVE_MIRROR_HOST_H
#include <stdint.h>

static struct { uint8_t config; } usb;           /* 0: not configured, the mirror never sends */
#define SXQ 64u
static uint32_t so_w, so_r;

#ifdef OBV_MIRROR_HOST_STUBS
static int ota_wire_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return -1; }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
static void ota_frame_done(void) {}
#else
static int ota_wire_send(const uint8_t *p, uint32_t n);
static int ota_frame_get(const uint8_t **p, uint32_t *n);
static void ota_frame_done(void);
#endif
#endif
