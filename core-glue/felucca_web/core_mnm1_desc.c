/* SPDX-License-Identifier: GPL-3.0-only */
/* The MNM-1 core's descriptor: the family's (fw_desc.h), over core_mnm1.c's entry points. No boot guard (the
 * simulator MNM-1's unit is built on, Hortator's, has no bootguard record) */
#define FM1CORE_FW_P mn
#define FM1CORE_FW_NAME "MNM-1"
#define FM1CORE_FW_REPO "https://github.com/galfaroth/mnm1"
#define FM1CORE_FW_BOOTGUARD 0
#include "fw_desc.h"
