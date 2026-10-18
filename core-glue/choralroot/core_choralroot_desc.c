/* SPDX-License-Identifier: GPL-3.0-only */
/* The ChoralRoot core's descriptor (crf_desc.h): the one exported symbol of choralroot.fm1core. Its own translation
 * unit (-Wall), with no firmware header. */
#define FM1CORE_CRF_P cr
#define FM1CORE_CRF_NAME "ChoralRoot"
#define FM1CORE_CRF_REPO "https://github.com/Quixotic7/ChoralRootFM1"
/* ChoralRoot's roles of the printed buttons (tools/emu/web/keymap.js BTN_ROLE, docs/CHORD-LAYOUT.md), FM1P_B_*
 * order: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+. LFO is LOCK with Options > Chord Keys FM-1
 * (the default since 0.25.1; Classic: LATCH, and the chord keys' labels move: fm1core_cr_layout) */
#define FM1CORE_CRF_BUTTONS                                                                                        \
    {"FX", "KEY", "BASS", "LOCK", "EDIT", "OPT", "HOME", "SAVE", "PERF", "SEQ", "LOOP", "REC", "OCT-", "OCT+"}
#define FM1CORE_CRF_PARAMS 1
#include "crf_desc.h"
