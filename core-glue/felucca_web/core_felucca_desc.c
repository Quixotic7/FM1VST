/* SPDX-License-Identifier: GPL-3.0-only */
/* The Felucca core's descriptor: the family's (fw_desc.h), with the Tier 2 map (core_felucca_params.c) */
#define FM1CORE_FW_P fe
#define FM1CORE_FW_NAME "Felucca"
#define FM1CORE_FW_REPO "https://github.com/hugelton/Felucca"
#define FM1CORE_FW_PARAMS 1
/* the second button keeps the name the Felucca core always had, SEL (panel.c's B_NAME says SCL, the scale layer): the
 * plugin's button parameters are named after it, and saved automation maps by name */
#define FM1CORE_FW_SCL "SEL"
#include "fw_desc.h"
