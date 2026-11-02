/* SPDX-License-Identifier: GPL-3.0-only */
/* Elasticat FM-1's firmware unit: empty. The firmware is native C written against the ABI; its units come in as
 * EXTRA_SOURCES (core_elasticat.cmake), with -Wall, and src/plat/ec_core.c defines fm1core_get. */
typedef int fm1core_elasticat_unit;
