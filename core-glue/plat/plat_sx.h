/* SPDX-License-Identifier: GPL-3.0-only */
/* The plat / fm1-base families' SysEx transport (research/managers-2026-10-19/MANAGERS.md, common/sysex.h), included
 * by a firmware unit (core_<id>.c) after plat_hal.h and BEFORE the firmware's sources, with
 *   FM1CORE_PL_SX_ACCEPT     an fm1sx_accept_fn: the frames the firmware's services read (the screen reader's
 *                            7D 53 52, X0X's editor 7D 46 4C, fm1-base's log 7D 4C 47, the M-UPGRADE 00 32 ..)
 *   FM1CORE_PL_SX_SERVICE()  what the device's main loop does with SysEx each pass (main_fm1.c: the screen reader's
 *                            frame, the editor, ota_service ..), in its order
 *   FM1CORE_PL_SX_CORO       1: the service waits the way the device does (X0X's restore pulls the backup from the
 *                            host with ota.c's read requests and asks on the screen: loops on fm1_ms and the panel
 *                            that only the device's interrupts move): it runs on a coroutine (common/coro.h) and
 *                            yields where the device feeds its watchdog (fm1_wdt_feed: ota_idle, rs_ask's loops),
 *                            so the clock, the input and the audio go on between passes, as the device's ISRs do
 *   FM1CORE_PL_SX_WORK()     with FM1CORE_PL_SX_CORO: 1 when the service has something to do this pass (a frame
 *                            waits, a backup streams); 0: the coroutine is not resumed
 * The device's usb.c (FELUCCA_OTA 1) gives every SysEx byte to its frame collector (sysex_byte: one frame, any
 * header; the UBOOT key F0 22 24 35 7D F7 and the update key F0 22 24 35 7F F7 only set flags that its main loop acts
 * on) and keeps SysEx out of the app's MIDI queue; replies leave ahead of the notes (ep1_tx). Here plat_glue.h's
 * midi_in gives SysEx packets (CIN 4..7) to fm1core_pl_sxi only, midi_out_take serves fm1core_pl_sxo first, and the
 * two keys are never accepted, so nothing acts on them (no update, no UBOOT: the panel's OCT- + OCT+ hold and the
 * boot guard stay the only ways into UBOOT). */
#pragma once
#include "sysex.h"
#include "coro.h"

#ifndef FM1CORE_PL_SX_CORO
#define FM1CORE_PL_SX_CORO 0
#endif

static fm1sx_in_t fm1core_pl_sxi;        /* the request waiting for the main loop (usb.c sx_frame) */
static fm1sx_out_t fm1core_pl_sxo;       /* the replies, CIN 4..7 packets, ahead of the firmware's MIDI out */
static uint8_t fm1core_pl_sx_held;       /* the main loop is inside the service (waiting for the host / a press) */
static int fm1core_pl_sx_send(const uint8_t *p, uint32_t n) { return fm1sx_send(&fm1core_pl_sxo, p, n); }

/* the frames the plat firmwares' services read */
static int fm1core_pl_sx_is(const uint8_t *f, uint32_t n, uint8_t h1, uint8_t h2)
{
    return n >= 3u && f[0] == 0x7Du && f[1] == h1 && f[2] == h2;
}
#define FM1CORE_PL_SX_READER(f, n) fm1core_pl_sx_is((f), (n), 0x53u, 0x52u)   /* announce.c "SR" */
