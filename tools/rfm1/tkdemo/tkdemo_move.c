/* SPDX-License-Identifier: GPL-3.0-only */
/* tkdemo, the Move side (docs/RFM1-TAKEOVER.md, tk1): only a build with FM1_HOST_TK (fm1_add_rfm1 WANTS tk1) has it;
 * elsewhere tkd_move_init does nothing, as on the FM-1.
 *
 *   "TKDEMO", claiming everything but TRACKS and VOLUME (0x5F); Shift (CC 49) toggles between that and ALL (0xFF),
 *   its white LED lit while ALL is claimed.
 *   The screen: "TKDEMO" at the top, the last input in the middle at twice the size ("PAD 5", "CC 50", "K1 +3",
 *   "NOTE 16"), the claims at the bottom ("CLAIMS 5F" / "CLAIMS ALL").
 *   A pad lights (255, 120, 0) while pressed and goes dark on release; a pad press says "pad N" (interrupting).
 * Deterministic: nothing depends on time, everything on the inputs. */
#include "tkdemo.h"

#ifdef FM1_HOST_TK
#include <string.h>

#include "rfm1_move.h"
#include "tkdemo_font.h"

#define TKD_BASE (RFM1_TK_ALL & ~(RFM1_TK_TRACKS | RFM1_TK_VOLUME))

static unsigned claims = TKD_BASE;
static char last[24] = "READY";

static void text(uint8_t *px, int x, int y, int sc, const char *s)
{
    for (; *s; s++, x += 6 * sc) {
        const uint8_t *g = tkd_glyph(*s);
        for (int c = 0; c < 5; c++)
            for (int r = 0; r < 7; r++)
                if (g[c] >> r & 1)
                    for (int dy = 0; dy < sc; dy++)
                        for (int dx = 0; dx < sc; dx++) {
                            const int X = x + c * sc + dx, Y = y + r * sc + dy;
                            if (X >= 0 && X < RFM1_TK_W && Y >= 0 && Y < RFM1_TK_H)
                                px[(Y >> 3) * RFM1_TK_W + X] |= (uint8_t)(1u << (Y & 7));
                        }
    }
}
static void draw(void)
{
    uint8_t *px = rfm1_tk_screen();
    if (!px)
        return;
    memset(px, 0, RFM1_TK_SCREEN_BYTES);
    text(px, 0, 0, 1, "TKDEMO");
    for (int x = 0; x < RFM1_TK_W; x++)            /* a rule under the title */
        px[1 * RFM1_TK_W + x] |= 1u << 1;          /* (y = 9) */
    text(px, 0, 24, 2, last);
    text(px, 0, 56, 1, claims == RFM1_TK_ALL ? "CLAIMS ALL" : "CLAIMS 5F");
    rfm1_tk_screen_done();
}

static void input(const uint8_t p[4])
{
    const unsigned st = p[1] & 0xF0u, d = p[2], v = p[3];
    char *e;
    if (st == 0x90u || st == 0x80u) {
        const int on = st == 0x90u && v;
        if (d >= RFM1_TK_NOTE_PAD0 && d < RFM1_TK_NOTE_PAD0 + 32u) {
            const int n = (int)(d - RFM1_TK_NOTE_PAD0);
            if (on) {
                char say[16];
                rfm1_tk_led(RFM1_TK_L_PAD0 + n, 255, 120, 0);
                tkd_num(tkd_cat(last, "PAD "), n);
                tkd_num(tkd_cat(say, "pad "), n);
                rfm1_tk_say(say, 1);
            } else {
                rfm1_tk_led(RFM1_TK_L_PAD0 + n, 0, 0, 0);
                return;                            /* (a release: the screen keeps the press) */
            }
        } else if (on)
            tkd_num(tkd_cat(last, "NOTE "), (long)d);
        else
            return;
    } else if (st == 0xB0u) {
        if (d >= RFM1_TK_CC_KNOB1 && d < RFM1_TK_CC_KNOB1 + 8u) {
            e = tkd_num(tkd_cat(last, "K"), (long)(d - RFM1_TK_CC_KNOB1 + 1u));
            e = tkd_cat(e, RFM1_TK_REL(v) >= 0 ? " +" : " ");
            tkd_num(e, RFM1_TK_REL(v));
        } else if (d == RFM1_TK_CC_JOG) {
            tkd_num(tkd_cat(last, RFM1_TK_REL(v) >= 0 ? "JOG +" : "JOG "), RFM1_TK_REL(v));
        } else if (v) {
            tkd_num(tkd_cat(last, "CC "), (long)d);
            if (d == RFM1_TK_CC_SHIFT) {
                claims = claims == RFM1_TK_ALL ? TKD_BASE : RFM1_TK_ALL;
                rfm1_tk_claims(claims);
                rfm1_tk_white(RFM1_TK_L_SHIFT, claims == RFM1_TK_ALL ? 255 : 0);
            }
        } else
            return;                                /* (a button's release) */
    } else
        return;                                    /* (pressure: nothing to show) */
    draw();
}
static void state(int on, unsigned granted, unsigned flags)
{
    (void)granted, (void)flags;
    if (on)
        draw();
}
static const rfm1_tk_ops_t OPS = {input, state};

void tkd_move_init(void)
{
    claims = TKD_BASE;
    tkd_cat(last, "READY");
    rfm1_tk_register("TKDEMO", claims, &OPS);
    rfm1_tk_white(RFM1_TK_L_SHIFT, 0);
    draw();
}
#else
void tkd_move_init(void) {}
#endif
