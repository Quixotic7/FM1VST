/* SPDX-License-Identifier: GPL-3.0-only */
/* core-glue/common/fifo: a firmware's own block size, format and channels served as the ABI's 128-frame int16 stereo
 * blocks, sample for sample. A ramp generator stands in for the firmware: frame i of the stream carries a known
 * value, so any drop, repeat or misalignment shows. Every block size of the survey (256, 128, 64, 32) plus odd ones
 * (100, 441, 1), int16 and int32 (Q15 << 8 and plain Q15, saturated), mono and stereo, pulled in ABI blocks of 128
 * and in uneven sizes. */
#include <stdio.h>
#include <string.h>
#include "fifo.h"

typedef struct {
    int fmt, channels, shift;
    long next;                  /* the stream's next frame */
    unsigned calls;
} gen_t;

static int16_t want_l(long i) { return (int16_t)((i * 7) % 30000 - 15000); }
static int16_t want_r(long i) { return (int16_t)(-((i * 11) % 30000) + 15000); }

static void render(void *ctx, void *buf, uint32_t frames)
{
    gen_t *g = (gen_t *)ctx;
    uint32_t k;
    g->calls++;
    for (k = 0; k < frames; k++, g->next++) {
        const int16_t l = want_l(g->next), r = want_r(g->next);
        if (g->fmt == FM1FIFO_S16) {
            int16_t *s = (int16_t *)buf;
            s[k * g->channels] = l;
            if (g->channels == 2)
                s[k * 2 + 1] = r;
        } else {
            int32_t *s = (int32_t *)buf;
            s[k * g->channels] = (int32_t)l * (1 << g->shift);
            if (g->channels == 2)
                s[k * 2 + 1] = (int32_t)r * (1 << g->shift);
        }
    }
}

static void sat_render(void *ctx, void *buf, uint32_t frames)   /* 2 frames: beyond Q15, then in range */
{
    int32_t *s = (int32_t *)buf;
    (void)ctx;
    (void)frames;
    s[0] = 40000 * 16;
    s[1] = -40000 * 16;
    s[2] = 100 * 16;
    s[3] = -100 * 16;
}

static int fails;
static void check(int c, const char *what)
{
    if (!c) {
        printf("  FAIL  %s\n", what);
        fails++;
    }
}

static void run(uint32_t block, int channels, int fmt, int shift, const uint32_t *pulls, int npulls)
{
    static fm1fifo_t f;
    static int16_t out[4096 * 2];
    gen_t g = {fmt, channels, shift, 0, 0};
    long i = 0, total = 0, bad = -1;
    int p;
    char what[160];
    check(fm1fifo_init(&f, block, channels, fmt, shift, render, &g) == 1, "init");
    for (p = 0; p < 4000; p++) {
        const uint32_t n = pulls[p % npulls];
        uint32_t k;
        fm1fifo_pull(&f, out, n);
        for (k = 0; k < n; k++, i++) {
            const int16_t l = out[2 * k], r = out[2 * k + 1];
            const int16_t wr = channels == 2 ? want_r(i) : want_l(i);
            if (bad < 0 && (l != want_l(i) || r != wr))
                bad = i;
        }
        total += n;
        /* rendered just enough: never more than one block ahead */
        if (f.rendered < (uint64_t)total || f.rendered >= (uint64_t)total + block)
            bad = bad < 0 ? -2 : bad;
    }
    snprintf(what, sizeof what, "block %u, %s %s, shift %d: %ld frames in order (first bad %ld), %u renders", block,
             channels == 2 ? "stereo" : "mono", fmt == FM1FIFO_S16 ? "int16" : "int32", shift, total, bad, g.calls);
    check(bad == -1 && f.served == (uint64_t)total, what);
    printf("  %s  %s\n", bad == -1 ? "ok  " : "FAIL", what);
}

int main(void)
{
    static const uint32_t abi[] = {128};
    static const uint32_t uneven[] = {128, 256, 384, 128, 1, 127, 512};
    static const uint32_t blocks[] = {256, 128, 64, 32, 100, 441, 1, 2048};
    unsigned b;
    for (b = 0; b < sizeof blocks / sizeof blocks[0]; b++) {
        run(blocks[b], 2, FM1FIFO_S16, 0, abi, 1);
        run(blocks[b], 1, FM1FIFO_S32, 8, abi, 1);
        run(blocks[b], 2, FM1FIFO_S32, 0, uneven, 7);
    }
    {   /* a 256-frame firmware renders once per two ABI blocks, on the first of them */
        static fm1fifo_t f;
        int16_t out[128 * 2];
        gen_t g = {FM1FIFO_S16, 2, 0, 0, 0};
        fm1fifo_init(&f, 256, 2, FM1FIFO_S16, 0, render, &g);
        fm1fifo_pull(&f, out, 128);
        check(g.calls == 1 && fm1fifo_buffered(&f) == 128, "256: the first ABI block renders one firmware block");
        fm1fifo_pull(&f, out, 128);
        check(g.calls == 1 && fm1fifo_buffered(&f) == 0, "256: the second is served from the buffer");
        fm1fifo_pull(&f, out, 128);
        check(g.calls == 2, "256: the third renders the next");
    }
    {   /* saturation: int32 beyond Q15 */
        static fm1fifo_t f;
        int16_t out[2 * 2];
        fm1fifo_init(&f, 2, 2, FM1FIFO_S32, 4, sat_render, NULL);
        fm1fifo_pull(&f, out, 2);
        check(out[0] == 32767 && out[1] == -32768 && out[2] == 100 && out[3] == -100, "int32 >> 4, saturated");
    }
    {
        static fm1fifo_t f;
        check(!fm1fifo_init(&f, 0, 2, FM1FIFO_S16, 0, render, NULL), "block 0 refused");
        check(!fm1fifo_init(&f, 4096, 2, FM1FIFO_S16, 0, render, NULL), "block > FM1FIFO_MAX refused");
        check(!fm1fifo_init(&f, 256, 3, FM1FIFO_S16, 0, render, NULL), "3 channels refused");
    }
    printf("fifo_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
