/* SPDX-License-Identifier: GPL-3.0-only */
/* The Dungeon Tracker core's descriptor: the family's (fw_desc.h) */
#define FM1CORE_FW_P dt
#define FM1CORE_FW_NAME "Dungeon Tracker"
#define FM1CORE_FW_REPO "https://github.com/BourbonWX/Dungeon-Tracker"
#ifdef FM1CORE_RFM1
#define FM1CORE_FW_PARAMS 1                 /* (the .rfm1: Felucca's map, fw_map_fork.h) */
#endif
#include "fw_desc.h"
