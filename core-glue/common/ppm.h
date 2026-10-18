/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel's screen as a picture, for the tests and the golden checks (C and C++, header-only): the LCD
 * (fm1panel_t lcd[], RGB565 big-endian as sent) as 8-bit RGB, scaled as the firmwares' host sims write theirs
 * (c * 255 / 31, c * 255 / 63: hostsim / host.c / ui_render / x0x_host.c), and 240 x 240 binary PPM (P6, maxval
 * 255) out and in. (ChoralRoot's emu_img.c replicates the high bits instead, (r << 3) | (r >> 2): crf_golden_test
 * keeps its own conversion for that reference.) */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "fm1core.h"

#define FM1G_PIXELS ((uint32_t)FM1P_LCD_W * FM1P_LCD_H)
#define FM1G_RGB_BYTES (FM1G_PIXELS * 3u)

/* one big-endian RGB565 pixel as 8-bit R, G, B */
static inline void fm1g_rgb_of(uint16_t be, uint8_t *rgb)
{
    const uint16_t c = (uint16_t)((be >> 8) | (be << 8));
    rgb[0] = (uint8_t)(((c >> 11) & 31u) * 255u / 31u);
    rgb[1] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u);
    rgb[2] = (uint8_t)((c & 31u) * 255u / 31u);
}
/* the whole LCD into rgb (FM1G_RGB_BYTES) */
static inline void fm1g_lcd_rgb(const uint16_t *lcd_be, uint8_t *rgb)
{
    uint32_t i;
    for (i = 0; i < FM1G_PIXELS; i++)
        fm1g_rgb_of(lcd_be[i], rgb + 3u * i);
}
/* 8-bit RGB (FM1G_RGB_BYTES) as a P6 file; 1 written */
static inline int fm1g_write_ppm(const char *path, const uint8_t *rgb)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f)
        return 0;
    fprintf(f, "P6\n%d %d\n255\n", FM1P_LCD_W, FM1P_LCD_H);
    ok = fwrite(rgb, 1, FM1G_RGB_BYTES, f) == FM1G_RGB_BYTES;
    return fclose(f) == 0 && ok;
}
/* the LCD as a P6 file; 1 written */
static inline int fm1g_write_lcd_ppm(const char *path, const uint16_t *lcd_be)
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    int ok = 1;
    if (!f)
        return 0;
    fprintf(f, "P6\n%d %d\n255\n", FM1P_LCD_W, FM1P_LCD_H);
    for (i = 0; i < FM1G_PIXELS && ok; i++) {
        uint8_t px[3];
        fm1g_rgb_of(lcd_be[i], px);
        ok = fwrite(px, 1, 3, f) == 3;
    }
    return fclose(f) == 0 && ok;
}
/* a 240 x 240 P6 file (maxval 255, any whitespace in the header) into rgb (FM1G_RGB_BYTES); 1 read */
static inline int fm1g_read_ppm(const char *path, uint8_t *rgb)
{
    FILE *f = fopen(path, "rb");
    int w = 0, h = 0, mx = 0, ok;
    if (!f)
        return 0;
    ok = fscanf(f, "P6 %d %d %d", &w, &h, &mx) == 3 && w == FM1P_LCD_W && h == FM1P_LCD_H && mx == 255 &&
         fgetc(f) != EOF && fread(rgb, 1, FM1G_RGB_BYTES, f) == FM1G_RGB_BYTES;
    fclose(f);
    return ok;
}
