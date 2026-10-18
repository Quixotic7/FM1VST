/* SPDX-License-Identifier: GPL-3.0-only */
/* core-glue/common/nor, ticks and panel: the NOR's rules (erase to 0xFF, a program only clears bits, never across a
 * page, out of range -8), the dirty counter, the erase stall, the staged boot (short and empty stages), the file
 * mode's write-through, the stdio redirection (B); the tick adapter's units and its spin clock; the panel helpers
 * (input copy and take, edges, LED planes -> levels, PLAY green, per-id LEDs, the LCD copy). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "nor.h"
#include "panel.h"
#include "ticks.h"

static int fails;
static void check(int c, const char *what)
{
    printf("  %s  %s\n", c ? "ok  " : "FAIL", what);
    fails += !c;
}

static uint32_t erased_at = 0xFFFFFFFFu;
static void on_erase(uint32_t off) { erased_at = off; }

static int all(const uint8_t *p, uint32_t n, uint8_t v)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (p[i] != v)
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *scratch = argc > 1 ? argv[1] : "nor_test.bin";
    uint8_t buf[512];
    /* ---- A: RAM only */
    fm1nor_options(NULL, 0, 1, "unused");
    check(fm1nor_path()[0] == 0, "headless without a path: RAM only");
    fm1nor_open();
    check(all(fm1nor_flash(), FM1NOR_SIZE, 0xFF) && fm1nor_flash() == fm1nor_image, "power-on: erased");
    {
        const uint32_t d0 = fm1nor_dirty();
        uint8_t a[4] = {0x0F, 0xF0, 0x00, 0xAA}, b[4] = {0xFF, 0x0F, 0xFF, 0x0F};
        check(fm1nor_prog(0x1000, a, 4) == 0 && fm1nor_prog(0x1000, b, 4) == 0, "two programs");
        check(fm1nor_image[0x1000] == 0x0F && fm1nor_image[0x1001] == 0x00 && fm1nor_image[0x1003] == 0x0A,
              "a program only clears bits (AND)");
        check(fm1nor_dirty() == d0 + 2, "the dirty counter moves per program");
        check(fm1nor_prog(0x10F0, buf, 0x20) == -8, "a program across a 256-byte page: -8");
        memset(buf, 0x55, sizeof buf);
        check(fm1nor_prog_span(0x20F0, buf, 0x120) == 0 && all(fm1nor_image + 0x20F0, 0x120, 0x55),
              "prog_span: split at the pages");
        check(fm1nor_read(FM1NOR_SIZE - 4, buf, 8) == -8 && fm1nor_read(FM1NOR_SIZE - 4, buf, 4) == 0,
              "a read past the end: -8");
        fm1nor_on_erase(on_erase);
        check(fm1nor_erase(0x1234) == 0 && all(fm1nor_image + 0x1000, 0x1000, 0xFF) && erased_at == 0x1000,
              "an erase: the 4 KiB sector holding the address, 0xFF; the hook called");
        check(fm1nor_erase(FM1NOR_SIZE) == -8, "an erase past the end: -8");
        {   /* the stall: 45 ms = 16 blocks of 128 at 44.1 kHz per erase */
            uint32_t n = 0;
            while (fm1nor_stall_take())
                n++;
            check(n == 16 && fm1nor_stalls() == 1 && fm1nor_stall_blocks() == 16, "an erase stalls 16 audio blocks");
            fm1nor_set_stall_ms(0);
            fm1nor_erase(0x3000);
            check(!fm1nor_stall_take(), "stall 0 ms: none");
            fm1nor_set_stall_ms(FM1NOR_ERASE_MS);
        }
        check(fm1nor_erase_range(0x10000, 0x10000) == 0 && fm1nor_stalls() == 2 + 16, "a 64 KiB range: 16 sectors");
        while (fm1nor_stall_take())
            ;
        check(fm1nor_write(0x5000, "raw", 3) == 0 && !memcmp(fm1nor_image + 0x5000, "raw", 3),
              "a raw write overwrites");
    }
    /* ---- A: the stage */
    {
        static uint8_t img[FM1NOR_SIZE];
        memset(img, 0xFF, sizeof img);
        memcpy(img + 0x8000, "STAGED", 6);
        check(fm1nor_stage(img, 0x9000) == 1 && fm1nor_stage_len() == 0x9000, "a short stage (36 KiB)");
        fm1nor_options(FM1CORE_FLASH_STAGED, 0, 1, NULL);
        fm1nor_open();
        check(!memcmp(fm1nor_image + 0x8000, "STAGED", 6) && all(fm1nor_image + 0x9000, FM1NOR_SIZE - 0x9000, 0xFF),
              "booted from the stage, padded with 0xFF");
        fm1nor_stage(NULL, 123);
        fm1nor_open();
        check(all(fm1nor_image, FM1NOR_SIZE, 0xFF), "an empty stage: a fresh flash");
    }
    /* ---- A: a file, written through */
    {
        FILE *f;
        remove(scratch);
        fm1nor_options(scratch, 0, 1, NULL);
        fm1nor_open();
        fm1nor_prog(0x40, "FILE", 4);
        fm1nor_close();
        f = fopen(scratch, "rb");
        memset(buf, 0, sizeof buf);
        if (f) {
            fseek(f, 0x40, SEEK_SET);
            fread(buf, 1, 4, f);
            fseek(f, 0, SEEK_END);
        }
        check(f && !memcmp(buf, "FILE", 4) && ftell(f) == (long)FM1NOR_SIZE, "a file: padded to 1 MiB, written through");
        if (f)
            fclose(f);
        check(fm1nor_path()[0] == 0, "closed: later writes RAM only");
        fm1nor_options(scratch, 0, 1, NULL);
        fm1nor_open();
        check(!memcmp(fm1nor_image + 0x40, "FILE", 4), "reopened: the file read back");
        fm1nor_close();
        remove(scratch);
    }
    /* ---- B: stdio with the stage */
    {
        static uint8_t img[0x2000];
        FILE *f;
        size_t n;
        fm1nor_stage(NULL, 0);
        check(fm1nor_fopen(FM1CORE_FLASH_STAGED, "r+b") == NULL, "B: nothing staged: fopen(r) is NULL (a new file)");
        f = fm1nor_fopen(FM1CORE_FLASH_STAGED, "w+b");
        check(f && fm1nor_is_stage(f), "B: fopen(w) gives the stage's handle");
        memset(img, 0xA5, sizeof img);
        fm1nor_stage(img, sizeof img);
        f = fm1nor_fopen(FM1CORE_FLASH_STAGED, "r+b");
        n = fm1nor_fread(buf, 1, sizeof buf, f);
        check(n == sizeof buf && buf[0] == 0xA5, "B: fread from the stage");
        fm1nor_fseek(f, 0x1F00, SEEK_SET);
        n = fm1nor_fread(buf, 1, sizeof buf, f);
        check(n == 0x100 && fm1nor_ftell(f) == 0x2000, "B: a short read at the stage's end");
        check(fm1nor_fwrite(buf, 1, 16, f) == 16 && fm1nor_fflush(f) == 0 && fm1nor_fclose(f) == 0,
              "B: writes, flush and close: accepted, dropped");
        f = fm1nor_fopen(scratch, "wb");
        check(f && !fm1nor_is_stage(f) && fm1nor_fwrite("x", 1, 1, f) == 1 && fm1nor_fclose(f) == 0,
              "B: any other path is real stdio");
        remove(scratch);
    }
    /* ---- ticks */
    {
        fm1t_set_ms(10);
        check(fm1t_ms() == 10 && fm1t_us() == 10000 && fm1t_ticks24() == 240000, "ticks: ms 10 = 10000 us = 240000");
        {
            uint32_t t0 = fm1t_ticks24_spin(1), t1 = t0;
            int spins = 0;
            while (t1 - t0 < 50u * 24u && spins < 1000) {
                t1 = fm1t_ticks24_spin(1);
                spins++;
            }
            check(spins == 50, "ticks: a 50 us busy-wait ends after 50 spin reads");
        }
        fm1t_set_ms(10);
        check(fm1t_us() == 10051, "ticks: the next set_ms never moves it back");
        fm1t_set_ms(11);
        check(fm1t_us() == 11000, "ticks: the device ms catches up");
        fm1t_set_ms(200000);
        check(fm1t_ticks24() == (uint32_t)(200000ull * 24000ull), "ticks: 24 MHz wraps as a 32-bit counter");
    }
    /* ---- panel helpers */
    {
        static fm1panel_t p;
        static fm1p_emu_hal_t h;
        static const int8_t km[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;
        uint32_t down, up;
        fm1p_edges_t e = {0};
        memset(&p, 0, sizeof p);
        fm1p_panel_default(&p);
        check(p.btn_id[FM1P_B_PLAY] == 12 && p.btn_id[FM1P_B_OCTDN] == 0 && p.enc_id[FM1P_E_PRESETS] == 6,
              "panel: the factory wiring (PLAY = 12, OCT- = 0, PRESETS = enc 6)");
        p.keys = 0x5;
        p.keys_tap = 0x8;
        p.buttons = 1u << 12;
        p.enc[6] = 3;
        p.master = 300;
        h.keys_tap = 0x10;
        h.enc[6] = 1;
        FM1P_EMU_IN(&p, &h);
        check(h.keys == 0x5 && h.keys_tap == 0x18 && h.enc[6] == 4 && h.master == 300 && h.buttons == (1u << 12),
              "panel: FM1P_EMU_IN copies the held state, ORs the taps, adds the detents");
        check(p.keys_tap == 0 && p.enc[6] == 0, "panel: ... and takes them from the panel");
        p.enc[6] = 2;
        p.enc_dir[FM1P_E_PRESETS] = -1;
        check(fm1p_take_role(&p, FM1P_E_PRESETS) == -2 && p.enc[6] == 0, "panel: a role's detents, its direction");
        fm1p_edges(&e, 0x1, 0x4, &down, &up);
        check(down == 0x5 && up == 0, "edges: a press and a tap");
        fm1p_edges(&e, 0x1, 0, &down, &up);
        check(down == 0 && up == 0x4, "edges: the tap's release on the next call");
        p.key_vel[3] = 0;
        p.key_vel[4] = 90;
        check(fm1p_vel(&p, 3, 100) == 100 && fm1p_vel(&p, 4, 100) == 90, "velocity: 0 = the firmware's default");
        check(fm1p_labels(&p, (1u << 12) | (1u << 0)) == ((1u << FM1P_B_PLAY) | (1u << FM1P_B_OCTDN)) &&
                  fm1p_label_of(&p, 99) == FM1P_NB,
              "labels of matrix ids");
        {
            uint8_t lit[FM1P_NCOL] = {0}, dim[FM1P_NCOL] = {0};
            const uint8_t *pl[2] = {lit, dim};
            const uint8_t lv[2] = {FM1P_LED_LIT, FM1P_LED_DIM};
            lit[3] = 1u << 2;               /* column 3, row bit 2: line 1 */
            dim[3] = (1u << 2) | (1u << 4); /* the same LED dim too (lit wins), and line 3 */
            lit[8] = 1u << 1;               /* PLAY green */
            check(fm1p_leds_planes(&p, pl, lv, 2, FM1P_PLAY_GREEN_POS) == 1, "LEDs: a change reported");
            check(p.led[3][1] == FM1P_LED_LIT && p.led[3][3] == FM1P_LED_DIM && p.led[3][0] == 0 &&
                      p.led_play_green == FM1P_LED_LIT,
                  "LEDs: planes -> levels, the first plane wins, PLAY green from its position");
            check(fm1p_leds_planes(&p, pl, lv, 2, FM1P_PLAY_GREEN_POS) == 0, "LEDs: no change, none reported");
            check(fm1p_led_id(&p, &km[0][0], 14 + 9, FM1P_LED_MID) == 1, "LEDs: by key id (D4)");
            {
                int c, l;
                fm1p_led_pos(&km[0][0], 14 + 9, &c, &l);
                check(p.led[c][l] == FM1P_LED_MID && c == 0 && l == 2, "LEDs: D4 (id 23) is column 0, line 2 (PA7)");
            }
            check(fm1p_led_class(255) == FM1P_LC_LIT && fm1p_led_class(FM1P_LED_MID) == FM1P_LC_MID &&
                      fm1p_led_class(FM1P_LED_DIM) == FM1P_LC_DIM && fm1p_led_class(FM1P_LED_FAINT) == FM1P_LC_FAINT &&
                      fm1p_led_class(0) == FM1P_LC_OFF && fm1p_led_class(175) == FM1P_LC_MID,
                  "LED classes");
        }
        {
            static uint16_t fb[FM1P_LCD_W * FM1P_LCD_H];
            h.lcd[5] = 0x1234;
            h.lcd_writes = 7;
            check(fm1p_lcd_out(&p, h.lcd, h.lcd_writes) == 1 && p.lcd[5] == 0x1234 && p.lcd_writes == 7 &&
                      fm1p_lcd_out(&p, h.lcd, h.lcd_writes) == 0,
                  "LCD: copied when the count moved, not again");
            fb[0] = 0xF800;
            check(fm1p_lcd_out_native(&p, fb) == 1 && p.lcd[0] == 0x00F8 && p.lcd_writes == 8 &&
                      fm1p_lcd_out_native(&p, fb) == 0 && p.lcd_writes == 8,
                  "LCD: a native frame buffer swapped, the count bumped only on a change");
        }
    }
    printf("nor_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
