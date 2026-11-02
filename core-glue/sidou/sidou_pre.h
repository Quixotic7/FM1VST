/* SPDX-License-Identifier: GPL-3.0-only */
/* Included first in every unit of the SIDOU core (core_sidou.cmake: -include). The firmware places its RAM regions
 * with section attributes (".pool", ".data.x", ".song_noinit": platform/fm1/app.ld), which a host has no use for and
 * Mach-O rejects (a section name there is "segment,section"): an ordinary object, aligned as before. */
#pragma once
#define section(x) aligned(4)
