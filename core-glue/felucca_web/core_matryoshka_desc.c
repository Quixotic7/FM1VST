/* SPDX-License-Identifier: GPL-3.0-only */
/* The Matryoshka core's descriptor: the family's (fw_desc.h), over core_matryoshka.c's entry points. No boot guard
 * (a core boots once per power-on: core_matryoshka.c). The second button is SEL on Matryoshka (panel.c BTN_SEL) */
#define FM1CORE_FW_P mt
#define FM1CORE_FW_NAME "Matryoshka"
#define FM1CORE_FW_REPO "https://github.com/glvno/matryoshka"
#define FM1CORE_FW_BOOTGUARD 0
#define FM1CORE_FW_SCL "SEL"
#include "fw_desc.h"
