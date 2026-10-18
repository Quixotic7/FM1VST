/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio adapter (core-glue/common/README.md "fifo"): a firmware that renders its own block size (SLOOP, the
 * Vestal plat and Dinghy render 256 frames; any size 1..FM1FIFO_MAX), as int16 or int32 (Q15 << shift), mono or
 * stereo, served as the ABI's audio(int16 stereo, a multiple of 128 frames).
 *
 * Exact alignment: the frames out are the frames rendered, in order, none dropped or repeated, so the output is the
 * firmware's stream sample for sample (no drift, no resampling). A pull FIFO: audio() asks for n frames and the FIFO
 * renders one whole firmware block each time its buffer runs empty while serving them, so a block is rendered when
 * its first frame is due (a 256-frame firmware: once per two ABI blocks, on the first of them; the second is served
 * from the buffer). Latency: none beyond the firmware's own block.
 *
 *     static fm1fifo_t fifo;                                      (in the glue)
 *     static void render(void *ctx, void *buf, uint32_t frames) { app_audio((int32_t *)buf, frames); }
 *     init():  fm1fifo_init(&fifo, 256, 2, FM1FIFO_S32, 0, render, NULL);   (Q15 in int32: shift 0, saturated)
 *     audio(): fm1fifo_pull(&fifo, stereo, frames);
 *
 * int32 samples are shifted right by `shift` (arithmetic) and saturated to int16; mono is duplicated to both
 * channels. Not thread-safe (the ABI's one clock thread). */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM1FIFO_MAX 2048u          /* the largest firmware block, frames */
enum { FM1FIFO_S16 = 0, FM1FIFO_S32 = 1 };

/* render exactly `frames` (= the block size) into buf: int16_t or int32_t samples, interleaved when stereo */
typedef void (*fm1fifo_render_fn)(void *ctx, void *buf, uint32_t frames);

typedef struct {
    uint32_t block;                /* the firmware's block, frames */
    uint8_t channels, fmt, shift;  /* 1 or 2; FM1FIFO_S16 / S32; int32 >> shift -> int16 */
    fm1fifo_render_fn render;
    void *ctx;
    uint32_t have, pos;            /* frames buffered in out[], the next one to give */
    uint64_t rendered, served;     /* firmware frames rendered / frames given out, since init */
    int32_t raw[FM1FIFO_MAX * 2];  /* the firmware's block as rendered */
    int16_t out[FM1FIFO_MAX * 2];  /* converted: int16 stereo */
} fm1fifo_t;

/* 0 on a bad argument (block 0 or > FM1FIFO_MAX, channels not 1 / 2, no render) */
int fm1fifo_init(fm1fifo_t *f, uint32_t block, int channels, int fmt, int shift, fm1fifo_render_fn render, void *ctx);
void fm1fifo_reset(fm1fifo_t *f);   /* drop what is buffered (counters restart) */
/* frames int16 stereo frames (any number; the ABI asks multiples of 128) */
void fm1fifo_pull(fm1fifo_t *f, int16_t *stereo, uint32_t frames);
static inline uint32_t fm1fifo_buffered(const fm1fifo_t *f) { return f->have - f->pos; }

#ifdef __cplusplus
}
#endif
