/* SPDX-License-Identifier: GPL-3.0-only */
/* nor.h's B: the firmware unit's stdio through fm1nor_f* (FM1CORE_FLASH_STAGED served from the stage, every other
 * path real stdio). Include after <stdio.h> and before the firmware's sources; nor_stdio_end.h after them. */
#include "nor.h"
#define fopen fm1nor_fopen
#define fread fm1nor_fread
#define fwrite fm1nor_fwrite
#define fseek fm1nor_fseek
#define ftell fm1nor_ftell
#define fflush fm1nor_fflush
#define fclose fm1nor_fclose
