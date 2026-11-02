/* SPDX-License-Identifier: GPL-3.0-only */
/* The SPELLBOX core's descriptor: the family's (fw_desc.h), over core_spellbox.c's entry points. No boot guard (the
 * device's is the platform's, fm1.c, not in the core). The second button is SEL, as foma's fm1.h names it (BTN_SEL) */
#define FM1CORE_FW_P sb
#define FM1CORE_FW_NAME "SPELLBOX"
#define FM1CORE_FW_REPO "https://github.com/pjac1810/spellbox"
#define FM1CORE_FW_BOOTGUARD 0
#define FM1CORE_FW_SCL "SEL"
#include "fw_desc.h"
