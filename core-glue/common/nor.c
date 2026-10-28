/* SPDX-License-Identifier: MPL-2.0 */
/* This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* The 1 MiB NOR image, the staged boot and the stdio redirection (nor.h). One copy per core module. */
#include "nor.h"

#include <string.h>
#include "fm1core.h"

uint8_t fm1nor_image[FM1NOR_SIZE];

static uint8_t stage[FM1NOR_SIZE];
static uint32_t stage_len;                /* bytes staged (0: a fresh, erased flash) */
static char path[1024];                   /* "": RAM only */
static FILE *file;                        /* A: the backing file (write-through), or NULL */
static uint32_t dirty;
static uint32_t stall_ms = FM1NOR_ERASE_MS, stall_left, stalls, stall_played;
static void (*erase_hook)(uint32_t off);

/* ------------------------------------------------------------------ the stage --- */
int fm1nor_stage(const uint8_t *bytes, uint32_t n)
{
    if (!bytes)
        n = 0;
    if (n > FM1NOR_SIZE)
        n = FM1NOR_SIZE;
    if (n)
        memcpy(stage, bytes, n);
    stage_len = n;
    return 1;
}
uint32_t fm1nor_stage_len(void) { return stage_len; }
const uint8_t *fm1nor_stage_bytes(void) { return stage; }

/* ---------------------------------------------------------- A: the power-on --- */
void fm1nor_options(const char *flash_path, int no_flash, int headless, const char *default_path)
{
    if (no_flash || (headless && !flash_path))
        path[0] = 0;
    else
        snprintf(path, sizeof path, "%s", flash_path ? flash_path : default_path ? default_path : "");
}
const char *fm1nor_path(void) { return path; }

void fm1nor_open(void)
{
    memset(fm1nor_image, 0xFF, sizeof fm1nor_image);
    if (file) {
        fclose(file);
        file = NULL;
    }
    if (!path[0])
        return;
    if (!strcmp(path, FM1CORE_FLASH_STAGED)) {
        memcpy(fm1nor_image, stage, stage_len);
        return;
    }
    file = fopen(path, "r+b");
    if (file) {
        size_t n = fread(fm1nor_image, 1, sizeof fm1nor_image, file);
        if (n < sizeof fm1nor_image)          /* short (or new): pad the file to the full image */
            memset(fm1nor_image + n, 0xFF, sizeof fm1nor_image - n);
    } else {
        file = fopen(path, "w+b");
    }
    if (!file) {
        fprintf(stderr, "fm1nor: flash file %s: cannot open (RAM only)\n", path);
        return;
    }
    fseek(file, 0, SEEK_SET);
    fwrite(fm1nor_image, 1, sizeof fm1nor_image, file);
    fflush(file);
}

void fm1nor_close(void)
{
    if (file) {
        fflush(file);
        fclose(file);
        file = NULL;
    }
    path[0] = 0;
}

uint8_t *fm1nor_flash(void) { return fm1nor_image; }
uint32_t fm1nor_dirty(void) { return dirty; }

void fm1nor_written(uint32_t off, uint32_t n)
{
    dirty++;
    if (!file || off >= FM1NOR_SIZE)
        return;
    if (n > FM1NOR_SIZE - off)
        n = FM1NOR_SIZE - off;
    fseek(file, (long)off, SEEK_SET);
    fwrite(fm1nor_image + off, 1, n, file);
    fflush(file);
}

/* ------------------------------------------------------- A: the operations --- */
static int in_range(uint32_t off, uint32_t n) { return off <= FM1NOR_SIZE && n <= FM1NOR_SIZE - off; }

int fm1nor_read(uint32_t off, void *dst, uint32_t n)
{
    if (!in_range(off, n))
        return -8;
    memcpy(dst, fm1nor_image + off, n);
    return 0;
}

int fm1nor_erase(uint32_t off)
{
    off &= ~(FM1NOR_SECTOR - 1u);
    if (off >= FM1NOR_SIZE)
        return -8;
    stall_left += (stall_ms * (uint32_t)FM1P_FS / 1000u + FM1P_BLOCK - 1u) / FM1P_BLOCK;
    stalls++;
    memset(fm1nor_image + off, 0xFF, FM1NOR_SECTOR);
    fm1nor_written(off, FM1NOR_SECTOR);
    if (erase_hook)
        erase_hook(off);
    return 0;
}

int fm1nor_erase_range(uint32_t off, uint32_t n)
{
    uint32_t a = off & ~(FM1NOR_SECTOR - 1u), end;
    if (!n)
        return 0;
    if (!in_range(off, n))
        return -8;
    end = off + n;
    for (; a < end; a += FM1NOR_SECTOR)
        fm1nor_erase(a);
    return 0;
}

int fm1nor_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (!in_range(off, n) || (off & (FM1NOR_PAGE - 1u)) + n > FM1NOR_PAGE)
        return -8;                            /* (a page program must not wrap: the device's rule) */
    for (i = 0; i < n; i++)
        fm1nor_image[off + i] &= ((const uint8_t *)src)[i];
    fm1nor_written(off, n);
    return 0;
}

int fm1nor_prog_span(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    if (!in_range(off, n))
        return -8;
    while (n) {
        uint32_t k = FM1NOR_PAGE - (off & (FM1NOR_PAGE - 1u));
        if (k > n)
            k = n;
        fm1nor_prog(off, s, k);
        off += k;
        s += k;
        n -= k;
    }
    return 0;
}

int fm1nor_write(uint32_t off, const void *src, uint32_t n)
{
    if (!in_range(off, n))
        return -8;
    memcpy(fm1nor_image + off, src, n);
    fm1nor_written(off, n);
    return 0;
}

/* ------------------------------------------------------------ A: the stall --- */
void fm1nor_set_stall_ms(uint32_t ms) { stall_ms = ms; }
int fm1nor_stall_take(void)
{
    if (!stall_left)
        return 0;
    stall_left--;
    stall_played++;
    return 1;
}
uint32_t fm1nor_stalls(void) { return stalls; }
uint32_t fm1nor_stall_blocks(void) { return stall_played; }
void fm1nor_on_erase(void (*fn)(uint32_t off)) { erase_hook = fn; }

/* ------------------------------------------------- B: stdio with the stage --- */
static uint32_t stage_pos;
static FILE *const STAGE_F = (FILE *)&stage_pos;   /* a handle nothing dereferences */

int fm1nor_is_stage(const FILE *f) { return f == STAGE_F; }

FILE *fm1nor_fopen(const char *p, const char *mode)
{
    if (!p || strcmp(p, FM1CORE_FLASH_STAGED))
        return p ? fopen(p, mode) : NULL;
    stage_pos = 0;
    return mode[0] == 'r' && !stage_len ? NULL : STAGE_F;
}
size_t fm1nor_fread(void *dst, size_t sz, size_t n, FILE *f)
{
    size_t want = sz * n, have;
    if (f != STAGE_F)
        return fread(dst, sz, n, f);
    have = stage_pos < stage_len ? stage_len - stage_pos : 0;
    if (want > have)
        want = have;
    memcpy(dst, stage + stage_pos, want);
    stage_pos += (uint32_t)want;
    return sz ? want / sz : 0;
}
size_t fm1nor_fwrite(const void *src, size_t sz, size_t n, FILE *f)
{
    if (f != STAGE_F)
        return fwrite(src, sz, n, f);
    stage_pos += (uint32_t)(sz * n);          /* (dropped: the firmware's RAM image is the truth) */
    return n;
}
int fm1nor_fseek(FILE *f, long off, int whence)
{
    long base;
    if (f != STAGE_F)
        return fseek(f, off, whence);
    base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)stage_pos : (long)stage_len;
    if (base + off < 0)
        return -1;
    stage_pos = (uint32_t)(base + off);
    return 0;
}
long fm1nor_ftell(FILE *f) { return f == STAGE_F ? (long)stage_pos : ftell(f); }
int fm1nor_fflush(FILE *f) { return f == STAGE_F ? 0 : fflush(f); }
int fm1nor_fclose(FILE *f) { return f == STAGE_F ? 0 : fclose(f); }
