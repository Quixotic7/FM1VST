/* SPDX-License-Identifier: GPL-3.0-only */
/* The Ravel core's descriptor (plat_desc.h) */
#define FM1CORE_PL_TITLE "Ravel"
#define FM1CORE_PL_REPO "https://github.com/De-Pattern/Ravel_FM1"
/* firmware/fm1/src/panel.c B_NAME, in FM1P_B_* order (Ravel prints SEL as SCL, as Felucca does) */
#define FM1CORE_PL_BUTTONS                                                                                     \
    {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"}
#include "plat_desc.h"
