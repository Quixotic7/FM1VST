/* SPDX-License-Identifier: GPL-3.0-only */
/* platform/fm1/store_fm1.c, unmodified but for its interrupt switch (cli / sti: nothing on one thread) and ALNK0's
 * pending register (fm1core_sd_alnk_con2), core_sidou.cmake's overlay. */
#include "sidou_glue.h"
#include "store_fm1.c"
