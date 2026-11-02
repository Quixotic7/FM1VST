/* SPDX-License-Identifier: GPL-3.0-only */
/* The MONO2K core's descriptor: the family's (fw_desc.h), over core_mono2k.c's entry points. No boot guard (the
 * device's is the platform's, not in the core this glue drives). The second button is SEL, as MONO2K names it
 * (m2k_ui.c BTN_N) */
#define FM1CORE_FW_P m2
#define FM1CORE_FW_NAME "MONO2K"
#define FM1CORE_FW_REPO "https://github.com/InigoHernandez/mono2k"
#define FM1CORE_FW_BOOTGUARD 0
#define FM1CORE_FW_SCL "SEL"
#include "fw_desc.h"
