/* SPDX-License-Identifier: GPL-3.0-only */
/* The core ABI. Phase 0: the ChoralRoot emulator's boundary (emu_hal_t and the emu_fw_* hooks) as is.
 * The versioned core descriptor (fm1core_t: name, version, flash size, button labels, entry points)
 * comes in phase 1 (see FM1-VST-PLAN.md section 4.4). */
#pragma once
#include "../cores/ChoralRootFM1/tools/emu/emu_hooks.h"
