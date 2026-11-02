/* SPDX-License-Identifier: GPL-3.0-only */
/* Read in place of Berlinan's app/libc/libc.c (core_berlinan.cmake's clang VFS overlay): that file is the device's
 * libc for the freestanding JieLi toolchain (memmove, strcmp, abs, the 64-bit divides), which app.c includes when
 * PLAT_HOST is off. The core has its own libc, so nothing here. */
