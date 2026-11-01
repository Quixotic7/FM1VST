/* SPDX-License-Identifier: GPL-3.0-only */
/* The Hortator core's descriptor: the family's (fw_desc.h), over core_hortator.c's entry points. No boot guard (the
 * simulator Hortator's unit is built on has no bootguard record) */
#define FM1CORE_FW_P hr
#define FM1CORE_FW_NAME "Hortator"
#define FM1CORE_FW_REPO "https://github.com/deadactive/hortator"
#define FM1CORE_FW_BOOTGUARD 0
#include "fw_desc.h"
