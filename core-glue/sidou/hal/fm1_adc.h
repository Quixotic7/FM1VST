/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_adc.h on the host: MASTER is the panel's pot. */
#pragma once
#include <stdint.h>
#include "sidou_glue.h"

enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static void fm1_adc_init(void) {}
static int32_t fm1_adc_read(uint32_t ch) { return fm1core_sd_adc(ch); }
