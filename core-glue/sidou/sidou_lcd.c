/* SPDX-License-Identifier: GPL-3.0-only */
/* The SIDOU core's LCD: platform/fm1/lcd.h's API in the place of platform/fm1/lcd.c (the ST7789's SPI driver, its
 * pins and its DMA). A window goes into the panel's RAM at once (fm1core_sd_lcd, big-endian RGB565 as sent: lcd.h's
 * LCD_RGB is already the panel's byte order), so no transfer is ever running (lcd_busy 0). lcd_init runs its two
 * callbacks in the panel's reset and wake-up waits, as lcd.c does, without the waits (the boot's timeline is not
 * the device's: nothing in SIDOU measures it). The screen shows black until lcd_on (DISPON), as the panel does. */
#include <stdint.h>
#include <string.h>
#include "lcd.h"
#include "sidou_glue.h"

uint16_t fm1core_sd_lcd[LCD_W * LCD_H];
uint32_t fm1core_sd_lcd_writes;
uint8_t fm1core_sd_lcd_on;
lcd_counters lcd_count;
static uint16_t fm1core_sd_lcd_buf[2][LCD_BUFFER_PIXELS];
static uint8_t fm1core_sd_lcd_next;

void lcd_init(void (*during)(void), void (*while_waking)(void))
{
    memset(fm1core_sd_lcd, 0, sizeof fm1core_sd_lcd);
    fm1core_sd_lcd_on = 0;
    fm1core_sd_lcd_writes++;
    lcd_count.bytes++;                    /* SWRESET */
    if (during)
        during();
    lcd_count.bytes++;                    /* SLPOUT */
    if (while_waking)
        while_waking();
    lcd_count.bytes += 4u + 2u;           /* COLMOD, MADCTL, INVON, NORON */
}

void lcd_on(void)
{
    lcd_count.bytes++;
    fm1core_sd_lcd_on = 1;
    fm1core_sd_lcd_writes++;
}

int lcd_busy(void) { return 0; }
void lcd_sync(void) {}
uint16_t *lcd_buffer(void) { return fm1core_sd_lcd_buf[fm1core_sd_lcd_next]; }

void lcd_send(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    const uint16_t *src = fm1core_sd_lcd_buf[fm1core_sd_lcd_next];
    uint32_t i, j;
    if (w * h > LCD_BUFFER_PIXELS)
        h = w ? LCD_BUFFER_PIXELS / w : 0;
    for (j = 0; j < h; j++)               /* (the panel's window: what falls past the edge is dropped) */
        for (i = 0; i < w; i++)
            if (x + i < LCD_W && y + j < LCD_H)
                fm1core_sd_lcd[(y + j) * LCD_W + x + i] = src[j * w + i];
    fm1core_sd_lcd_next ^= 1u;
    lcd_count.windows++;
    lcd_count.bytes += LCD_WINDOW_COMMAND_BYTES + w * h * 2u;
    fm1core_sd_lcd_writes++;
}
