/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot family's SysEx transport, included by a firmware unit (core_<id>.c) BEFORE its emu_fw.c: the glue's
 * frame buffer and reply ring (common/sysex.h), and the firmware's send hooks pointed at the ring, which the emulator
 * units leave as no-ops ("no SysEx transport here"):
 *   CRB_SEND(p, n)   cr_backup.c / cf_backup.c: every backup reply (INFO, LIST, GET, PUT, RESTART, ..)
 *   CZ_SEND(p, n)    cz_store.c: the CZ-1 tone dumps and handshakes (ChoralRoot)
 * The service that hands the frames to the firmware's handler is crf_glue.h's (FM1CORE_CRF_SX). */
#pragma once
#include "sysex.h"

static fm1sx_in_t fm1core_crf_sxi;       /* the request waiting for the main loop */
static fm1sx_out_t fm1core_crf_sxo;      /* the replies, as CIN 4..7 packets, ahead of the firmware's MIDI out */
static int fm1core_crf_sx_send(const uint8_t *p, uint32_t n) { return fm1sx_send(&fm1core_crf_sxo, p, n); }
#define CRB_SEND(p, n) fm1core_crf_sx_send((p), (n))
#define CZ_SEND(p, n) fm1core_crf_sx_send((p), (n))
