/* SPDX-License-Identifier: MPL-2.0 */
/* This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* The FM-1's 1 MiB SPI NOR for a core (core-glue/common/README.md "nor"). Two ways to use it:
 *
 * A. The NOR itself (a glue whose HAL has no flash of its own: storage.c's st_read / st_erase / st_prog hooks,
 *    Dinghy's sector store, ..): fm1nor_image[] is the device's flash, erased (0xFF) at power-on and filled from
 *    the staged bytes (ABI 2 flash_stage / FM1CORE_FLASH_STAGED) or a file; every erase / program moves the dirty
 *    counter (the descriptor's flash_dirty) and is written through to the file when there is one; an erase stalls
 *    the audio as the device does (fm1nor_stall_take in the glue's audio()).
 *        options():  fm1nor_options(flash_path, no_flash, headless, "build/<id>/flash.bin");
 *        init():     fm1nor_open();                        (before the firmware's persist_boot)
 *        HAL:        st_read = fm1nor_read, st_erase = fm1nor_erase, st_prog = fm1nor_prog
 *        audio():    if (fm1nor_stall_take()) { silence this 128-frame block; continue; }
 *        exit hook:  fm1nor_close();
 *        descriptor: flash = fm1nor_flash, flash_dirty = fm1nor_dirty, flash_stage = fm1nor_stage
 *
 * B. The staged boot for a firmware whose own emulator keeps the image and does stdio on a path (ChoralRoot's
 *    emu_hal_fw.h): include nor_stdio.h before the firmware's sources (and nor_stdio_end.h after): fopen / fread /
 *    fwrite / fseek / ftell / fflush / fclose go through fm1nor_f*, which pass every real path to stdio and serve
 *    the path FM1CORE_FLASH_STAGED from the staged bytes (reads; writes are dropped: the firmware's RAM image is
 *    the truth, the host reads it with flash()). fopen(STAGED, "r..") with nothing staged returns NULL (a fresh
 *    flash), as a missing file would.
 *
 * The NOR's rules hold in A: an erase sets a 4 KiB sector to 0xFF, a program can only clear bits and must not
 * cross a 256-byte page (fm1nor_prog; fm1nor_prog_span splits a longer one into pages), out of range is -8
 * (storage.c's error). fm1nor_write is a raw overwrite (a firmware whose store is a RAM copy written back whole:
 * copy it into the image in the device's sector format, then fm1nor_written or fm1nor_write). Not thread-safe:
 * the ABI's one clock thread. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM1NOR_SIZE 0x100000u      /* 1 MiB */
#define FM1NOR_SECTOR 0x1000u      /* the erase unit */
#define FM1NOR_PAGE 0x100u         /* the program unit */
#define FM1NOR_ERASE_MS 45u        /* a 4 KiB erase on the FM-1's part (typ.; up to 400 ms): the audio stall */

extern uint8_t fm1nor_image[FM1NOR_SIZE];   /* A: the live image (the descriptor's flash()) */

/* ---- the stage (ABI 2): both A and B */
int fm1nor_stage(const uint8_t *bytes, uint32_t n);   /* flash_stage(): copied (n > 1 MiB: cut); bytes NULL or n 0:
                                                       * a fresh flash; 1 */
uint32_t fm1nor_stage_len(void);
const uint8_t *fm1nor_stage_bytes(void);

/* ---- A: where the image comes from, and the power-on */
/* options(): no_flash, or headless without a path: RAM only (a fresh flash every power-on); else flash_path (or
 * default_path when it is NULL; FM1CORE_FLASH_STAGED: the stage) */
void fm1nor_options(const char *flash_path, int no_flash, int headless, const char *default_path);
const char *fm1nor_path(void);              /* "" RAM only, FM1CORE_FLASH_STAGED, or the file */
void fm1nor_open(void);                     /* the power-on: erased, then the stage / the file read (a file is kept
                                             * open, padded to 1 MiB, every change written through) */
void fm1nor_close(void);                    /* flush and close the file: later changes stay in RAM (exit, shutdown);
                                             * also drops the path (a later fm1nor_open: RAM only) */
uint8_t *fm1nor_flash(void);                /* fm1nor_image */
uint32_t fm1nor_dirty(void);                /* erases + programs + writes since load */

/* ---- A: the NOR's operations (storage.c's hooks); 0, or -8 out of range / a page crossed */
int fm1nor_read(uint32_t off, void *dst, uint32_t n);
int fm1nor_erase(uint32_t off);             /* the 4 KiB sector holding off */
int fm1nor_erase_range(uint32_t off, uint32_t n);   /* every sector touching off .. off + n - 1 (a 32 / 64 KiB block
                                                     * erase: one stall per sector, as the device's sector loop) */
int fm1nor_prog(uint32_t off, const void *src, uint32_t n);        /* AND into one page */
int fm1nor_prog_span(uint32_t off, const void *src, uint32_t n);   /* AND, split at page boundaries */
int fm1nor_write(uint32_t off, const void *src, uint32_t n);       /* raw overwrite (no NOR rule) */
void fm1nor_written(uint32_t off, uint32_t n);   /* the glue changed fm1nor_image itself: count it, write it through */

/* ---- A: the erase stall: each erase queues FM1NOR_ERASE_MS (fm1nor_set_stall_ms; 0: none) of silent 128-frame
 * blocks; the glue's audio() takes one per block and outputs zeros instead of running the firmware's audio ISR */
void fm1nor_set_stall_ms(uint32_t ms);
int fm1nor_stall_take(void);                /* 1: this block is silence (one taken) */
uint32_t fm1nor_stalls(void);               /* erases so far (the stall count) */
uint32_t fm1nor_stall_blocks(void);         /* silent blocks played so far */
void fm1nor_on_erase(void (*fn)(uint32_t off));   /* called after every sector erase (a firmware's own counter) */

/* ---- B: stdio with the stage behind FM1CORE_FLASH_STAGED (nor_stdio.h maps the names) */
FILE *fm1nor_fopen(const char *path, const char *mode);
size_t fm1nor_fread(void *dst, size_t sz, size_t n, FILE *f);
size_t fm1nor_fwrite(const void *src, size_t sz, size_t n, FILE *f);
int fm1nor_fseek(FILE *f, long off, int whence);
long fm1nor_ftell(FILE *f);
int fm1nor_fflush(FILE *f);
int fm1nor_fclose(FILE *f);
int fm1nor_is_stage(const FILE *f);         /* the handle fm1nor_fopen gave for FM1CORE_FLASH_STAGED */

#ifdef __cplusplus
}
#endif
