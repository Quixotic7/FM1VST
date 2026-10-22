/* SPDX-License-Identifier: GPL-3.0-only */
/* The managers' SysEx for a SLOOP-family core (research/managers-2026-10-19/MANAGERS.md): the firmware's OWN web
 * editor protocol (editor.c: INFO, the parameters, the steps, user presets, BK_LIST / BK_GET / BK_PUT backup and
 * restore, the user sample slots, sloopDX's DX7 bank and voices, the LIVE page) and ota.c's M-UPGRADE identity, on
 * the glue's transport (common/sysex.h). On the device both are compiled under FELUCCA_OTA with usb.c's SysEx plumbing
 * (sx_frame, the 64-packet sx_out_q, ota_wire_send); the cores build usb.c with FELUCCA_OTA 0 (the update modes need
 * the hardware), so this file gives editor.c and ota.c those hooks instead:
 *   ota_frame_get / ota_frame_done   the glue's one waiting frame (FL 7D 46 4C and M-UPGRADE 00 32 frames only)
 *   ota_wire_send                    the glue's reply ring, served by midi_out_take ahead of the firmware's MIDI out
 *   so_w / so_r / SXQ                the ring's indices for editor.c's ed_room (WATCH pushes only into a ring at most
 *                                    half of the device's 64 packets full: the device's pacing)
 *   fl_write / fl_inval / fl_erase4k_quiet   felucca.c's flash helpers (the user sample slots), on common/nor.c,
 *                                    so a manager's writes land in the core's image like every other save
 * Included by sloop_firmware.h where felucca.c includes ota.c and editor.c (after storage.c, upreset.c, project.c).
 * The service (fm1core_sl_sx_service) is main.c's: ed_service then ota_service in the loop's body, ed_service alone
 * in its wait for the next frame (sloop_fw.c frame / idle).
 *
 * The update entries are not built: F0 22 24 35 7F F7 (M-UPGRADE's start) reaches usb.c, which without FELUCCA_OTA
 * sets nothing, and the frame filter never holds it; UBOOT's F0 22 24 35 7D F7 sets usb.uboot_req, which sloop_fw.c
 * clears and ignores (the device would leave for its ROM loader; a core has none). The protocol has no restart command.
 *
 * Busy waits: none on these paths. ota_wire_send never waits (the ring has room for every reply: the largest,
 * editor.c's ed_out, is 600 bytes = 200 packets; it returns -1 when full, as the device after 200 ms); ota.c's
 * waiting loops (ota_read, 5 s on ota_now_ms) run only inside ota_session, which is never called; the flash calls
 * are nor.c's and return at once; sloop_hal.h's fm1_delay_ms is a no-op. Real time: static buffers only. */
#pragma once
#include "sysex.h"

static fm1sx_in_t fm1core_sl_sxi;        /* the request waiting for the main loop */
static fm1sx_out_t fm1core_sl_sxo;       /* the replies, as CIN 4..7 packets */

static int fm1core_sl_sx_accept(const uint8_t *f, uint32_t n) { return fm1sx_is_fl(f, n) || fm1sx_is_mupg(f, n); }

/* ---- felucca.c's flash helpers (hal/fm1_flash.h under them): the image is the XIP window, nothing to invalidate;
 * an erase silences the audio for its time (nor.c's stall), as fl_erase4k_quiet's interrupts-off window does ---- */
static void fl_inval(uint32_t off, uint32_t len)
{
    (void)off;
    (void)len;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n) { return fm1nor_prog_span(off, src, n); }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    *took = 0;
    return fm1nor_erase(off);
}

/* ---- ota.c: the M-UPGRADE identity (common/sysex_ota.h). FELUCCA_ID: felucca.c's default, which tools/build.py
 * keeps without --release: every one of these packages answers "FM-1_900" (SLOOP's DUALCORE.md: "the package keeps
 * SLOOP's development identity"; No Man Sky's nomanskyfm-0.5.fwsc carries "ota-FM-1_900") ---- */
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_900"
#endif
#define FM1SX_OTA_C "ota.c"
#define FM1SX_OTA_IN fm1core_sl_sxi
#define FM1SX_OTA_OUT fm1core_sl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

/* ---- editor.c (and the editor_*.c it includes), on the hooks above ---- */
#define SXQ 64u                          /* usb.c's sx_out_q */
#define so_w (fm1core_sl_sxo.w)
#define so_r (fm1core_sl_sxo.r)
#include "editor.c"
#undef so_w
#undef so_r
#undef SXQ

static void fm1core_sl_sx_init(void)
{
    fm1sx_in_init(&fm1core_sl_sxi, fm1core_sl_sx_accept);
    fm1sx_out_reset(&fm1core_sl_sxo);
}
/* main.c: ed_service() then ota_service() in the loop's body (frame 1), ed_service() in the wait (frame 0) */
static void fm1core_sl_sx_service(int frame)
{
    ed_service();
    if (frame)
        ota_service();
}
/* the core's midi_in: refused (0, "retry") while a request waits and this packet starts another, or usb.c's EP1
 * ring is short of room; else to usb.c (notes, clock, the UBOOT key) and the frame buffer */
static int fm1core_sl_sx_in(uint32_t pkt)
{
    if (fm1sx_in_would_refuse(&fm1core_sl_sxi, pkt))
        return 0;
    if (MQ - (mi_w - mi_r) < 8u)
        return 0;
    midi_in_event(pkt);
    fm1sx_in_pkt(&fm1core_sl_sxi, pkt);
    return 1;
}
