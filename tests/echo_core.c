/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake core for editor_port_test (tests/editor_port_test.cpp): no firmware, only the ABI. It answers SysEx the way
 * a firmware's editor service does, from its clock (so a reply proves the clock ran):
 *   - a SysEx F0 <body> F7 that arrives through midi_in is answered at the next tick() with F0 7D 45 43 <body> F7
 *     ("EC": echo; one reply per request, never the request itself);
 *   - a channel message is sent back unchanged at the next tick() (the firmware's MIDI out: notes, CCs);
 *   - every tick counts: the reply to F0 7D 45 43 01 F7 ("how many ticks") is F0 7D 45 43 01 <the count, 4 x 7 bits> F7.
 * Silence, a blank screen, a 64 KiB flash. */
#include <stdint.h>
#include <string.h>

#include "fm1core.h"

static fm1panel_t panel;
static int8_t keymap[6 * FM1P_NCOL];
static uint8_t flash[65536];
static uint32_t ticks;

#define NQ 16
static uint8_t req[NQ][512];
static uint32_t req_n[NQ];
static uint32_t req_w, req_r;
static uint8_t sx[512];
static uint32_t sx_n;
static int sx_on;
static uint32_t chan[64];
static uint32_t chan_w, chan_r;

#define NOUT 4096
static uint32_t outq[NOUT];
static uint32_t out_w, out_r;

static void out_pkt(uint32_t p)
{
    if (out_w - out_r < NOUT)
        outq[out_w++ % NOUT] = p;
}
static void out_sysex(const uint8_t *d, uint32_t n)   /* d: F0 .. F7 */
{
    uint32_t i = 0;
    while (n - i > 3) {
        out_pkt(4u | (uint32_t)d[i] << 8 | (uint32_t)d[i + 1] << 16 | (uint32_t)d[i + 2] << 24);
        i += 3;
    }
    {
        const uint32_t r = n - i;
        out_pkt((r == 1 ? 5u : r == 2 ? 6u : 7u) | (uint32_t)d[i] << 8 | (r > 1 ? (uint32_t)d[i + 1] << 16 : 0u) |
                (r > 2 ? (uint32_t)d[i + 2] << 24 : 0u));
    }
}

static void sx_byte(uint8_t b)
{
    if (b == 0xF0) {
        sx_on = 1, sx_n = 0;
    }
    if (!sx_on)
        return;
    if (sx_n < sizeof sx)
        sx[sx_n++] = b;
    if (b == 0xF7) {
        sx_on = 0;
        if (req_w - req_r < NQ && sx_n >= 2) {
            memcpy(req[req_w % NQ], sx, sx_n);
            req_n[req_w % NQ] = sx_n;
            req_w++;
        }
    }
}

static int midi_in(uint32_t p)
{
    const uint32_t cin = p & 15u;
    const uint8_t b[3] = {(uint8_t)(p >> 8), (uint8_t)(p >> 16), (uint8_t)(p >> 24)};
    if (cin == 4 || cin == 7) {
        sx_byte(b[0]), sx_byte(b[1]), sx_byte(b[2]);
    } else if (cin == 6) {
        sx_byte(b[0]), sx_byte(b[1]);
    } else if (cin == 5) {
        sx_byte(b[0]);
    } else if (cin >= 8) {
        if (chan_w - chan_r < 64)
            chan[chan_w++ % 64] = p;
    }
    return 1;
}

static int midi_out_take(uint32_t *p)
{
    if (out_r == out_w)
        return 0;
    *p = outq[out_r++ % NOUT];
    return 1;
}

static void tick(uint32_t ms)
{
    (void)ms;
    ticks++;
    while (req_r != req_w) {
        const uint8_t *q = req[req_r % NQ];
        const uint32_t n = req_n[req_r % NQ];
        uint8_t r[600];
        uint32_t k = 0;
        r[k++] = 0xF0, r[k++] = 0x7D, r[k++] = 0x45, r[k++] = 0x43;
        if (n == 6 && q[1] == 0x7D && q[2] == 0x45 && q[3] == 0x43 && q[4] == 0x01) {
            r[k++] = 0x01;
            r[k++] = (uint8_t)(ticks & 0x7F), r[k++] = (uint8_t)((ticks >> 7) & 0x7F);
            r[k++] = (uint8_t)((ticks >> 14) & 0x7F), r[k++] = (uint8_t)((ticks >> 21) & 0x7F);
        } else {
            for (uint32_t i = 1; i + 1 < n; i++)
                r[k++] = q[i];
        }
        r[k++] = 0xF7;
        out_sysex(r, k);
        req_r++;
    }
    while (chan_r != chan_w)
        out_pkt(chan[chan_r++ % 64]);
}

static void options(const char *f, int nf, int s, int h) { (void)f, (void)nf, (void)s, (void)h; }
static void init(int demo)
{
    (void)demo;
    for (int i = 0; i < FM1P_NB; i++)
        panel.btn_id[i] = (uint8_t)i;
    for (int i = 0; i < FM1P_NE - 1; i++)
        panel.enc_id[i] = (uint8_t)i, panel.enc_dir[i] = 1;
    panel.lcd_writes++;
    ticks = 0, req_w = req_r = 0, chan_w = chan_r = 0, out_w = out_r = 0, sx_on = 0;
}
static void frame(void) {}
static void idle(void) {}
static void audio(int16_t *st, uint32_t n) { memset(st, 0, (size_t)n * 4u); }
static void nop(void) {}
static void stats(uint32_t *a, uint32_t *b) { *a = 0, *b = 0; }
static void ui_info(char *buf, uint32_t n)
{
    if (n)
        buf[0] = 0;
}
static int halted(void) { return 0; }
static int flash_stage(const uint8_t *b, uint32_t n)
{
    memset(flash, 0xFF, sizeof flash);
    if (b)
        memcpy(flash, b, n < sizeof flash ? n : sizeof flash);
    return 1;
}
static uint8_t *flash_ptr(void) { return flash; }
static uint32_t flash_dirty(void) { return 0; }
static int flash_sync(void) { return 1; }

static fm1core_t desc = {
    .abi_version = FM1CORE_ABI,
    .id = "echo",
    .name = "Echo (test)",
    .version = "1",
    .source_url = "",
    .family = "test",
    .license = "GPL-3.0-only",
    .gated = 0,
    .flash_size = sizeof flash,
    .panel = &panel,
    .keymap = keymap,
    .options = options,
    .init = init,
    .tick = tick,
    .frame = frame,
    .idle = idle,
    .audio = audio,
    .midi_in = midi_in,
    .midi_out_take = midi_out_take,
    .dump = nop,
    .stats = stats,
    .ui_info = ui_info,
    .shutdown = nop,
    .halted = halted,
    .flash_stage = flash_stage,
    .flash = flash_ptr,
    .flash_dirty = flash_dirty,
    .flash_sync = flash_sync,
};

FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi)
{
    if (abi != FM1CORE_ABI)
        return 0;
    memset(keymap, -1, sizeof keymap);
    return &desc;
}
