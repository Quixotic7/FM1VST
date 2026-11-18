/* SPDX-License-Identifier: GPL-3.0-only */
/* The ObsSynth FM-1 core's descriptor: the family's (fw_desc.h), over core_obssynth.c's entry points. No boot guard
 * (the simulator the unit is built on has no bootguard record) */
#define FM1CORE_FW_P ob
#define FM1CORE_FW_NAME "ObsSynth"
#define FM1CORE_FW_REPO "https://github.com/orianbsilva/obssynth-fm1"
#define FM1CORE_FW_BOOTGUARD 0
#include "fw_desc.h"
