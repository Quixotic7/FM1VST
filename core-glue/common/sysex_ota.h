/* SPDX-License-Identifier: GPL-3.0-only */
/* The M-UPGRADE identity for a core (core-glue/common/README.md "sysex"): a Felucca-family firmware's own ota.c (its
 * "step 1 lite" updater entry: the portable core the device and its Mac test share), compiled into the glue with
 * hooks on the glue's SysEx transport (sysex.h), so the updater's handshake gets the firmware's own answer:
 *
 *   F0 00 32 45 00 00 00 40 7F F7   (pack7 of 00 59 11 len 0: "who are you?")
 *   -> F0 pack7(00 59 11 len 27 FELUCCA_ID .. sum) F7, the package identity ("FM-1_920", ...)
 *
 * ota_service() is what the device's main loop calls after its editor service: it takes the waiting frame (any
 * frame the editor left), answers the handshake, and frees it. The update itself is NOT run: the device starts it on
 * the separate key F0 22 24 35 7F F7 (usb.c ota_req -> main.c ota_session), which no glue acts on, so an installer
 * pointed at a core gets its identity and then no reply to its read requests (it times out), and nothing is written.
 * The flash hooks refuse (-1) all the same, should anything reach them.
 *
 * Include it after the firmware unit and sysex.h, with
 *   FM1SX_OTA_C          "<path>/firmware/src/ota.c"
 *   FM1SX_OTA_IN / _OUT  the glue's fm1sx_in_t / fm1sx_out_t (lvalues)
 *   FM1SX_OTA_MS()       the device's ms (fm1_ms)
 *   FELUCCA_ID           the device unit's package identity (its felucca.c / choralroot.c define; the emulator units
 *                        leave it out)
 * Names: ota.c's own (ota_*), static in the unit; a unit whose usb.c already defines ota_wire_send (FELUCCA_OTA 1)
 * does not use this file. */
#pragma once
#ifndef FM1SX_OTA_C
#error "FM1SX_OTA_C: the firmware's ota.c"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the firmware's package identity (its device unit's)"
#endif

static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1sx_send(&(FM1SX_OTA_OUT), p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&(FM1SX_OTA_IN), p, n); }
static void ota_frame_done(void) { fm1sx_done(&(FM1SX_OTA_IN)); }
static uint32_t ota_now_ms(void) { return (uint32_t)(FM1SX_OTA_MS()); }
static void ota_idle(void) {}
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }

#include FM1SX_OTA_C
