/* SPDX-License-Identifier: GPL-3.0-only */
/* The HAWK CHUA core's descriptor: the family's (fw_desc.h), over core_hawkchua.c's entry points. No boot guard (the
 * device's is main.c fm1_cstart's, not in this host build) */
#define FM1CORE_FW_P hc
#define FM1CORE_FW_NAME "HAWK CHUA"
#define FM1CORE_FW_REPO "https://github.com/Plieuw/hawk-chua"
#define FM1CORE_FW_BOOTGUARD 0
#include "fw_desc.h"
