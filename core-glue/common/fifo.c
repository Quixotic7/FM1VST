/* SPDX-License-Identifier: MPL-2.0 */
/* This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* The audio adapter (fifo.h). */
#include "fifo.h"

#include <string.h>

int fm1fifo_init(fm1fifo_t *f, uint32_t block, int channels, int fmt, int shift, fm1fifo_render_fn render, void *ctx)
{
    memset(f, 0, sizeof *f);
    if (!block || block > FM1FIFO_MAX || (channels != 1 && channels != 2) || !render || shift < 0 || shift > 31 ||
        (fmt != FM1FIFO_S16 && fmt != FM1FIFO_S32))
        return 0;
    f->block = block;
    f->channels = (uint8_t)channels;
    f->fmt = (uint8_t)fmt;
    f->shift = (uint8_t)shift;
    f->render = render;
    f->ctx = ctx;
    return 1;
}

void fm1fifo_reset(fm1fifo_t *f)
{
    f->have = f->pos = 0;
    f->rendered = f->served = 0;
}

static int16_t sat(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static void refill(fm1fifo_t *f)               /* one firmware block -> out[] (int16 stereo) */
{
    uint32_t i, n = f->block;
    memset(f->raw, 0, (size_t)n * f->channels * (f->fmt == FM1FIFO_S32 ? 4u : 2u));
    f->render(f->ctx, f->raw, n);
    if (f->fmt == FM1FIFO_S16) {
        const int16_t *s = (const int16_t *)f->raw;
        for (i = 0; i < n; i++) {
            f->out[2 * i] = s[i * f->channels];
            f->out[2 * i + 1] = s[i * f->channels + f->channels - 1u];
        }
    } else {
        const int32_t *s = f->raw;
        for (i = 0; i < n; i++) {
            f->out[2 * i] = sat(s[i * f->channels] >> f->shift);
            f->out[2 * i + 1] = sat(s[i * f->channels + f->channels - 1u] >> f->shift);
        }
    }
    f->have = n;
    f->pos = 0;
    f->rendered += n;
}

void fm1fifo_pull(fm1fifo_t *f, int16_t *stereo, uint32_t frames)
{
    if (!f->block) {                            /* (not initialised: silence) */
        memset(stereo, 0, (size_t)frames * 2u * sizeof *stereo);
        return;
    }
    while (frames) {
        uint32_t k;
        if (f->pos == f->have)
            refill(f);
        k = f->have - f->pos;
        if (k > frames)
            k = frames;
        memcpy(stereo, f->out + 2u * f->pos, (size_t)k * 2u * sizeof *stereo);
        stereo += 2u * k;
        f->pos += k;
        f->served += k;
        frames -= k;
    }
}
