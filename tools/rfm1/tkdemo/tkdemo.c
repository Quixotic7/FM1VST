/* SPDX-License-Identifier: GPL-3.0-only */
/* tkdemo, the FM-1 side: what any firmware has (a screen, a sound for the keys, the panel's ids), small and
 * deterministic. Its LCD shows "TKDEMO" and the number of keys held; each held key plays a triangle at its pitch. The
 * Move side is tkdemo_move.c. */
#include <string.h>

#include "tkdemo.h"
#include "tkdemo_font.h"

fm1panel_t tkd_panel;
const int8_t tkd_keymap[6 * FM1P_NCOL] = {[0 ... 6 * FM1P_NCOL - 1] = -1};
uint8_t tkd_flash[4096];
static uint32_t keys_drawn = 0xFFFFFFFFu;
static uint32_t phase[FM1P_NKEY], step[FM1P_NKEY];

/* RGB565 big-endian, as the panel's RAM holds it */
#define BE565(r, g, b) ((uint16_t)((((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3)) >> 8 | \
                                    (((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3)) << 8))

static void lcd_text(int x, int y, int sc, const char *s, uint16_t fg)
{
    for (; *s; s++, x += 6 * sc) {
        const uint8_t *g = tkd_glyph(*s);
        for (int c = 0; c < 5; c++)
            for (int r = 0; r < 7; r++)
                if (g[c] >> r & 1)
                    for (int dy = 0; dy < sc; dy++)
                        for (int dx = 0; dx < sc; dx++) {
                            const int px = x + c * sc + dx, py = y + r * sc + dy;
                            if (px >= 0 && px < FM1P_LCD_W && py >= 0 && py < FM1P_LCD_H)
                                tkd_panel.lcd[py * FM1P_LCD_W + px] = fg;
                        }
    }
}
static int nkeys(uint32_t k)
{
    int n = 0;
    for (; k; k &= k - 1)
        n++;
    return n;
}
static void lcd_draw(void)
{
    char t[16];
    const uint16_t bg = BE565(16, 24, 48);
    for (int i = 0; i < FM1P_LCD_W * FM1P_LCD_H; i++)
        tkd_panel.lcd[i] = bg;
    lcd_text(12, 40, 6, "TK", BE565(255, 160, 0));
    lcd_text(12, 100, 4, "DEMO", BE565(255, 255, 255));
    tkd_num(tkd_cat(t, "KEYS "), nkeys(tkd_panel.keys));
    lcd_text(12, 180, 3, t, BE565(120, 220, 255));
    keys_drawn = tkd_panel.keys;
    tkd_panel.lcd_writes++;
}

void tkd_options(const char *f, int nf, int s, int h) { (void)f, (void)nf, (void)s, (void)h; }
void tkd_init(int demo)
{
    (void)demo;
    for (int i = 0; i < FM1P_NB; i++)
        tkd_panel.btn_id[i] = (uint8_t)i;
    for (int i = 0; i < FM1P_NE - 1; i++)
        tkd_panel.enc_id[i] = (uint8_t)i, tkd_panel.enc_dir[i] = 1;
    /* the step of each key's triangle: 2^32 * f / 44100, f = 440 * 2^((note - 69) / 12), from a semitone table */
    static const uint32_t F3_OCT[12] = {   /* F3 .. E4 (notes 53..64), x 2^32 / 44100 */
        17005939u, 18017165u, 19088521u, 20223584u, 21426141u, 22700205u,
        24050030u, 25480119u, 26995246u, 28600467u, 30301139u, 32102938u,
    };
    for (int k = 0; k < FM1P_NKEY; k++)
        step[k] = F3_OCT[k % 12] << (k / 12);
    memset(phase, 0, sizeof phase);
    keys_drawn = 0xFFFFFFFFu;
    lcd_draw();
    tkd_move_init();
}
void tkd_tick(uint32_t ms)
{
    (void)ms;
    tkd_panel.keys_tap = 0, tkd_panel.buttons_tap = 0;
    for (int e = 0; e < FM1P_NENC; e++)
        tkd_panel.enc[e] = 0;
}
void tkd_frame(void)
{
    if (tkd_panel.keys != keys_drawn)
        lcd_draw();
}
void tkd_idle(void) {}
void tkd_audio(int16_t *st, uint32_t n)
{
    const uint32_t keys = tkd_panel.keys;
    for (uint32_t i = 0; i < n; i++) {
        int32_t v = 0;
        for (int k = 0; k < FM1P_NKEY; k++)
            if (keys >> k & 1u) {
                const uint32_t p = phase[k] += step[k];
                const int32_t tri = (int32_t)((p >> 31) ? ~p : p) >> 15;   /* 0..65535 */
                v += (tri - 32768) / 12;
            }
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        st[2 * i] = st[2 * i + 1] = (int16_t)v;
    }
}
int tkd_midi_in(uint32_t pkt) { (void)pkt; return 1; }
int tkd_midi_out_take(uint32_t *pkt) { (void)pkt; return 0; }
void tkd_nop(void) {}
void tkd_stats(uint32_t *a, uint32_t *b) { *a = 0, *b = 0; }
void tkd_ui_info(char *buf, uint32_t n)
{
    char t[24];
    if (!n)
        return;
    tkd_num(tkd_cat(t, "tkdemo keys "), nkeys(tkd_panel.keys));
    strncpy(buf, t, n - 1);
    buf[n - 1] = 0;
}
int tkd_halted(void) { return 0; }
int tkd_flash_stage(const uint8_t *b, uint32_t n)
{
    memset(tkd_flash, 0xFF, sizeof tkd_flash);
    if (b)
        memcpy(tkd_flash, b, n < sizeof tkd_flash ? n : sizeof tkd_flash);
    return 1;
}
uint8_t *tkd_flash_ptr(void) { return tkd_flash; }
uint32_t tkd_flash_dirty(void) { return 0; }
int tkd_flash_sync(void) { return 1; }
