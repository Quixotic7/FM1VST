/* SPDX-License-Identifier: GPL-3.0-only */
/* Modular Koi's own files, as felucca.c includes them under MODULAR_KOI (core_modularkoi.c; fw_core.h includes this
 * right after the editor, inside its flash stand-ins): the controls, the tape's songs and sketches, the pond.
 * koi_draw.c's ring() (a ripple) is renamed: the web unit's output buffer is called ring. */
#include "../../cores/modular-koi/firmware/src/koi_ui.c"
#define fm1_xip_ptr(off) ((const uint8_t *)nor + (off))   /* (the flash, XIP: the web unit's NOR, as SMP_USER_XIP) */
#include "../../cores/modular-koi/firmware/src/m1_book.c"
#undef fm1_xip_ptr
#define ring koi_ring
#include "../../cores/modular-koi/firmware/src/koi_draw.c"
#undef ring

/* web_ms with main.c's MODULAR_ONE loop (the unit's web_frame is Felucca's ui_input / ui_leds / ui_draw): after the
 * splash m1_init and m1_boot (the songs and sketches kept), then every 15 ms m1_input, m1_service, settings_poll,
 * m1_leds, m1_draw, and m1_input in between (main.c). (The unit's power-on still runs autosave_boot, which main.c
 * leaves out under MODULAR_ONE; m1_init then starts the drum clean, as on the device.) */
static void koi_web_frame(void)
{
    int32_t b = fm1_adc_read(FM1_ADC_BATT);
    if (b > 0)
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    master_poll();
    felucca_dbg.ui_frames++;
    felucca_dbg.page = ui.page;
    felucca_dbg.home = ui.home;
    m1_input();
    m1_service();
    settings_poll();
    m1_leds();
    m1_draw();
}
static void koi_web_ms(void)
{
    fm1_ms++;
    web_ticks += 1000u * FM1_TICKS_PER_US;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {
        uint32_t k;
        const int32_t *o = &abuf[web_half * HALF_WORDS];
        web_samples_due -= 1000u * HALF_FRAMES;
        fm1_alnk0_irq();
        for (k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)o[2u * k] / 8388608.0f;
            ring[i + 1] = (float)o[2u * k + 1u] / 8388608.0f;
        }
        web_half ^= 1u;
    }
    if (!web_booted || (int32_t)(fm1_ms - web_boot_ms) < 0)
        return;
    if (web_boot_ms) {                    /* main.c: the splash goes, m1_init, m1_boot, the loop starts */
        web_boot_ms = 0;
        kb_boot_hold = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        scrn.idle = fm1_ms;
        m1_init();
        m1_boot();
        web_last_frame = fm1_ms - 15u;
    }
    if (fm1_ms - web_last_frame >= 15u) {
        web_last_frame = fm1_ms;
        koi_web_frame();
    } else {
        m1_input();
    }
}
