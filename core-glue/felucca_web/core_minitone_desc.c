/* SPDX-License-Identifier: GPL-3.0-only */
/* The Mini-Tone core's descriptor: the family's (fw_desc.h) */
#define FM1CORE_FW_P mt
#define FM1CORE_FW_NAME "Mini-Tone"
#define FM1CORE_FW_REPO "https://github.com/eldontyrellcorp/Mini-Tone"
#define FM1CORE_FW_BOOTGUARD 1
#ifdef FM1CORE_RFM1
#define FM1CORE_FW_PARAMS 1                 /* (the .rfm1: Felucca's map, fw_map_fork.h) */
#endif
#include "fw_desc.h"
