/* SPDX-License-Identifier: GPL-3.0-only */
/* The PurpleMonkey core's descriptor (crf_desc.h). */
#define FM1CORE_CRF_P pm
#define FM1CORE_CRF_NAME "PurpleMonkey"
#define FM1CORE_CRF_REPO "https://github.com/Quixotic7/PurpleMonkeyFM1"
/* PurpleMonkey's stickers (tools/emu/web/pm_keymap.js BTN_ROLE, firmware/src/pm_ui.c), FM1P_B_* order; the buttons
 * with no role there (GLO, SAVE, ARP, SEQ, REC) keep the panel's printed names */
#define FM1CORE_CRF_BUTTONS                                                                                        \
    {"DRUMS", "SYNTH", "KEYS", "TUNE", "TALK", "GLO", "HOME", "SAVE", "ARP", "SEQ", "BEAT", "REC", "OCT-", "OCT+"}
#define FM1CORE_CRF_PARAMS 0
#define FM1CORE_CRF_BOOT_OPTIONS 0        /* (no boot guard: pm_emu_fw.c's emu_fw_boot_options ignores everything) */
#include "crf_desc.h"
