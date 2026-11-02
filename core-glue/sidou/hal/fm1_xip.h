/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_xip.h on the host: the XIP window is the NOR image (common/nor.c), read in place. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "sidou_glue.h"

#define FM1_XIP(off) ((uintptr_t)(fm1core_sd_nor() + (uint32_t)(off)))
FM1_INLINE const uint8_t *fm1_xip_ptr(uint32_t off) { return (const uint8_t *)FM1_XIP(off); }
