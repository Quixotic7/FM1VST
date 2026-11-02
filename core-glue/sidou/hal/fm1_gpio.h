/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's HAL: hal/fm1_gpio.h on the host. The ports are plain words (input.c's power-on UBOOT gesture
 * writes them; it never runs in a core): nothing reads them back as pins. */
#pragma once
#include <stdint.h>

static volatile uint32_t fm1core_sd_gpio[8][8];
#define FM1_PR(p, r) (fm1core_sd_gpio[(p) & 7u][((r) >> 2) & 7u])
enum { FM1_PA = 0, FM1_PB = 1, FM1_PC = 2, FM1_PH = 7 };
enum { FM1_OUT = 0x00, FM1_IN = 0x04, FM1_DIR = 0x08, FM1_DIE = 0x0C, FM1_PU = 0x10,
       FM1_PD = 0x14, FM1_HD0 = 0x18, FM1_HD = 0x1C };
