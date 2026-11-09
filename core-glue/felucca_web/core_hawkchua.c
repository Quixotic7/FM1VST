/* SPDX-License-Identifier: GPL-3.0-only */
/* The HAWK CHUA core (https://github.com/Plieuw/hawk-chua, the pinned, unmodified submodule cores/hawk-chua):
 * plieuwski's chaotic synthesizer, each voice a live Chua's circuit (or a noise program) into resonators, waveguides
 * and a shimmer reverb, on a stripped-down Felucca. HAWK CHUA has no web/emu/felucca_web.c (so not fw_core.h) and no
 * browser simulator: this unit is its host build, made as its own host tests make theirs (tests/ui_shot.c: the real
 * gfx.c / ui.c / synth.c / remote.c against a frame buffer and a fake USB), in firmware/src/felucca.c's order, with
 * the device's HAL replaced by the glue's and main.c's boot and loop driven from the ABI. The family's descriptor
 * (fw_desc.h) over the entry points here, as Hortator's and MONO2K's.
 *
 *   ABI                  the device
 *   init                 main.c fm1_main's boot: settings_boot (the flash's settings and saved presets), the
 *                        palette, the splash, keys_init, tape_init, master_boot, the audio, the LED glow and the
 *                        power-on sweep (hal/fm1_led_anim.h's picture); the keys silent for the 600 ms splash
 *   frame                one pass of main.c's loop: master_poll (MASTER's IIR, its square law), the OCT- + OCT+
 *                        countdown (UBOOT at 5 s: the core exits 3), rm_service (remote.c: the screen, the state,
 *                        the panel over SysEx F0 7D 43 48), ota_service (the M-UPGRADE identity), ui_input,
 *                        ui_leds, ui_draw
 *   idle                 main.c's wait between frames: rm_service, ota_service
 *   audio(128)           audio.c fm1_alnk0_irq, the real ISR (keys_block + synth_render per 32 frames), one I2S
 *                        half: abuf (Q15 << OUT_SHIFT) >> OUT_SHIFT as s16; a sector erase is silence (the IRQs
 *                        are off on the device)
 *   midi_in              usb.c midi_in_event (channel messages and the clock into the MIDI ring, as the endpoint
 *                        takes them; the UBOOT and update keys seen and ignored) and the glue's frame buffer
 *                        (sysex.h: remote.c's and the updater's frames)
 *   midi_out_take        SysEx replies first (usb.c ep1_tx), then the notes (midi_out_event; usb.config = 1)
 *   flash                the NOR image (common/nor.c) behind storage.c's st_read / st_erase / st_prog; SAVE and the
 *                        remote's RESET PRESETS write at once (flash_sync has nothing waiting)
 *
 * The identity: FM-1_007 (tools/build.py PRODUCT, every build and release), the version v1.7 (build.py --release
 * 1.7, the tag's; FELUCCA_VERSION from core_hawkchua.cmake). Not here: the update itself (F0 22 24 35 7F F7: no
 * update inside a core), UBOOT over USB (F0 22 24 35 7D F7: ignored), the boot guard, the second core (FELUCCA_CPU2
 * 0: one core renders all, as the host tests), USB audio (FELUCCA_UAC 0), TRS MIDI (FELUCCA_UART 0), the CPU meter
 * (no clock inside the ISR here: 0 %), key velocity (the device's keys have none: KEY_VEL).
 *
 * Statics here are hc_-prefixed (the firmware is in this unit). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) the panel helpers */
#include "nor.h"                          /* (core-glue/common) the NOR image */
#include "version.h"
#include "exit.h"                         /* (halted / shutdown: UBOOT is an exit) */
#define FM1SX_OUT_PKTS 65536u             /* remote.c's SCREEN (31 messages of 4 rows) and PRESETS (512 lines) are
                                           * queued whole: the device's ota_wire_send waits for the USB instead */
#include "sysex.h"                        /* (core-glue/common) the frame buffer and the reply ring */

/* ---- felucca.c's build options, as this host build has them */
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 1                     /* (remote.c and the identity; the update session is never started) */
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0
#define FELUCCA_CPU2 0
#define FELUCCA_ID "FM-1_007"             /* tools/build.py PRODUCT */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "v0.1"
#endif
#define __attribute__(x)                  /* (as the host tests build it: no .pool / .noinit sections) */

/* ------------------------------------------------------------- libc --- */
#define memset felucca_memset             /* (tests/ui_shot.c: the firmware's own, under other names) */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "../../cores/hawk-chua/firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp

/* ------------------------------------------------------------- the LCD --- */
/* lcd.c: the panel's frame, RGB565 as sent (high byte first): lcd_fill's colour swapped, lcd_blit's pixels already */
static uint16_t hc_fb[240 * 240];
static uint32_t hc_draws;
static uint8_t lcd_fresh = 1;             /* (lcd.c: gfx.c's cv_blit sends only the bands that changed) */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    const uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    lcd_fresh = 1;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            hc_fb[j * 240u + i] = sw;
    hc_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            hc_fb[(y + j) * 240u + x + i] = px[j * w + i];
    hc_draws++;
}
static void lcd_sync(void) {}
static void lcd_wake_now(void) {}
static void lcd_init(void) {}
#include "../../cores/hawk-chua/firmware/src/gfx.c"

#include "../../cores/hawk-chua/firmware/src/core.h"

/* ------------------------------------------------- usb.c, its MIDI side --- */
/* the device's usb.c is the SIE driver; what the rest of the firmware reads of it, with the same rules */
static struct {
    uint8_t up, config;
    volatile uint8_t suspended;
    uint8_t sysex[16];
    uint8_t sx_len, sx_on;
    volatile uint8_t uboot_req, ota_req;
} usb;
#define MQ 64u
static uint32_t midi_in_q[MQ];
static uint8_t midi_in_source[MQ];        /* 1 USB, 2 TRS */
static volatile uint32_t mi_w, mi_r;
static volatile uint8_t midi_in_overflow;
#define HC_OQ 1024u                       /* (the host drains it every block: the device's 64 and its endpoint) */
static uint32_t hc_mo[HC_OQ];
static uint32_t hc_mo_w, hc_mo_r;

static int midi_enqueue(uint32_t pkt, uint32_t source)
{
    if (midi_in_overflow)
        return 0;
    if (mi_w - mi_r >= MQ) {
        midi_in_overflow = 1;
        return 0;
    }
    midi_in_q[mi_w % MQ] = pkt;
    midi_in_source[mi_w % MQ] = (uint8_t)source;
    mi_w++;
    return 1;
}
static void midi_out_event(uint32_t pkt)  /* from the audio ISR */
{
    if (usb.config && hc_mo_w - hc_mo_r < HC_OQ)
        hc_mo[hc_mo_w++ % HC_OQ] = pkt;
}
static void sysex_byte(uint8_t b)         /* usb.c: only the two keys (the frames: sysex.h) */
{
    static const uint8_t UBOOT_KEY[6] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
    if (b >= 0xF8u)
        return;
    if ((b & 0x80u) && b != 0xF0u && b != 0xF7u) {
        usb.sx_on = 0;
        return;
    }
    if (b == 0xF0u) {
        usb.sx_on = 1;
        usb.sx_len = 0;
    }
    if (!usb.sx_on)
        return;
    if (usb.sx_len < sizeof usb.sysex)
        usb.sysex[usb.sx_len++] = b;
    if (b == 0xF7u) {
        uint32_t i, ok = usb.sx_len == 6u;
        for (i = 0; ok && i < 6u; i++)
            ok = usb.sysex[i] == UBOOT_KEY[i];
        if (ok)
            usb.uboot_req = 1;
        else if (usb.sx_len == 6u && usb.sysex[1] == 0x22 && usb.sysex[2] == 0x24 && usb.sysex[3] == 0x35 &&
                 usb.sysex[4] == 0x7F)
            usb.ota_req = 1;
        usb.sx_on = 0;
    }
}
static void midi_in_event(uint32_t pkt)   /* one USB-MIDI event packet (usb.c) */
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (cin >= 4u && cin <= 7u) {
        uint32_t k, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (k = 0; k < nb; k++)
            sysex_byte((uint8_t)(pkt >> (8u * (k + 1u))));
        return;
    }
    if (st >= 0x80u && st < 0xF8u)
        usb.sx_on = 0;
    if (cin == 0xFu && (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)) {
        midi_enqueue(0xFu | st << 8, 1u);
        return;
    }
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && !((pkt >> 16) & 0x80u) &&
        ((cin == 0xCu || cin == 0xDu) || !(pkt & 0x80000000u)))
        midi_enqueue(pkt, 1u);
}

/* ---------------------------------------------- hal/fm1_input.h: the panel --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = FM1P_KEYMAP_INIT;   /* (hal/fm1_input.h's: the same table) */
static struct { volatile uint32_t notes, buttons; } fm1_in;
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL];
static uint32_t hc_pressed;
static int32_t hc_enc_steps[7];
static int32_t fm1_enc_take(uint32_t e) { int32_t s = hc_enc_steps[e % 7u]; hc_enc_steps[e % 7u] = 0; return s; }
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t p = hc_pressed; (void)released; hc_pressed = 0; return p; }
/* the power-on sweep (hal/fm1_input.h fm1_led_anim_start over hal/fm1_led_anim.h's picture): its frame from the
 * clock (a scan frame 1.1 ms); at its end, or a key or a button down, the glow (MENU-less: always on here, as
 * main.c starts it with 1) and the UI's LEDs from the next frame */
#include "../../cores/hawk-chua/firmware/hal/fm1_led_anim.h"
static uint32_t hc_anim_t0;
static uint8_t hc_anim_run, hc_anim_glow;
static uint32_t hc_anim_frame(void) { return (fm1_ms - hc_anim_t0) * 10u / 11u; }
static void fm1_led_anim_start(uint32_t glow)
{
    memset(fm1_led, 0, sizeof fm1_led);
    memset(fm1_led_dim, 0, sizeof fm1_led_dim);
    hc_anim_t0 = fm1_ms;
    hc_anim_glow = glow != 0u;
    hc_anim_run = 1;
}
static int fm1_led_anim_on(void)
{
    if (hc_anim_run && (hc_anim_frame() >= FM1_ANIM_FRAMES || fm1_in.notes || fm1_in.buttons)) {
        uint32_t c, r;
        hc_anim_run = 0;
        for (c = 0; c < FM1_NCOL; c++)
            for (r = 1; r < 5u; r++)
                if (hc_anim_glow && FM1_KEYMAP[r][c] >= 0)
                    fm1_led_dim[c] |= (uint8_t)(1u << r);
    }
    return hc_anim_run;
}

#include "../../cores/hawk-chua/firmware/src/panel.c"

/* ----------------------------------------------------------- the sound --- */
#include "../../cores/hawk-chua/firmware/src/synth.c"   /* (C1_ON 0: synth.c's own one-core defaults) */
#include "../../cores/hawk-chua/firmware/src/keys.c"

/* audio.c over hal/fm1_audio.h: a half due each call (hc_half the one the DMA has free) */
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return 0; }   /* (no clock inside the ISR: its load reads 0) */
#define FM1_AUDIO_HALF 1u
static uint32_t hc_half;
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return hc_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_fence(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t words, void (*isr)(void), uint32_t prio)
{
    (void)buf; (void)words; (void)isr; (void)prio;
}
void isr_alnk0(void) {}
#include "../../cores/hawk-chua/firmware/src/audio.c"

/* ------------------------------------------------------------ the flash --- */
/* storage_hw.c's hooks on the glue's NOR image (the store's A/B sectors at 0x97000.. and 0xFC000) */
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n) { return fm1nor_read(off, dst, n); }
static int st_erase(uint32_t off) { return fm1nor_erase(off); }
static int st_prog(uint32_t off, const void *src, uint32_t n) { return fm1nor_prog_span(off, src, n); }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's part (ui.c settings_boot) */
/* 2.0.5's fm1_flash.h, as its own web/emu/emu.c has them: either FM-1 flash chip, and a host build is not the
 * chip-level emulated FM-1 (fm1-emu) that fl_emulated lightens the load for */
static int fl_id_ok(uint32_t id) { return id == 0x856014u || id == 0xB36014u; }
static int fl_emulated(void) { return 0; }
#define FL_FAR(fn) (fn)
#include "../../cores/hawk-chua/firmware/src/storage.c"

/* ------------------------------------------- the SysEx: remote.c, ota.c --- */
static fm1sx_in_t hc_sxi;
static fm1sx_out_t hc_sxo;
static int hc_sx_accept(const uint8_t *f, uint32_t n)   /* remote.c's 7D 43 48, the updater's 00 32 */
{
    static const uint8_t RM[3] = {0x7Du, 0x43u, 0x48u};
    return fm1sx_is_mupg(f, n) || (n >= 1u && !memcmp(f, RM, n < 3u ? n : 3u));
}
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1sx_send(&hc_sxo, p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&hc_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&hc_sxi); }
#define FM1SX_OTA_TRANSPORT_DONE 1
#define FM1SX_OTA_C "../../cores/hawk-chua/firmware/src/ota.c"
#define FM1SX_OTA_IN hc_sxi
#define FM1SX_OTA_OUT hc_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

/* ------------------------------------------------------------- the UI --- */
#include "../../cores/hawk-chua/firmware/src/ui.c"
#include "../../cores/hawk-chua/firmware/src/remote.c"

#undef __attribute__
#include "exit_end.h"

/* ---------------------------------------------- main.c's parts, as there --- */
static int32_t hc_master_adc = 700;      /* MASTER as the ADC reads it, 0..1023 */
static int32_t master_knob = 512 * 16;
static uint32_t master_of_pot(uint32_t k10) { return (k10 * k10) >> 8; }
static void master_poll(void)
{
    master_knob += (hc_master_adc * 16 - master_knob) / 8;
    sys.master_q12 = (uint16_t)master_of_pot((uint32_t)(master_knob / 16));
}
static void master_boot(void)
{
    master_knob = hc_master_adc * 16;
    sys.master_q12 = (uint16_t)master_of_pot((uint32_t)hc_master_adc);
}
/* OCT- + OCT+ held: a countdown from 2 s, UBOOT at 5 s (the core exits 3, as the device leaves for the ROM's loader);
 * letting go cancels it. 1: UBOOT */
static uint32_t hc_uboot_t0, hc_uboot_shown;
static int hc_uboot_combo(void)
{
    uint32_t both = btn_bit(B_OCTDN) | btn_bit(B_OCTUP), left;
    if ((fm1_in.buttons & both) != both) {
        if (hc_uboot_shown) {
            hc_uboot_shown = 0;
            ui_force = 1;
            ui_say("UPDATE MODE CANCELLED");
        }
        hc_uboot_t0 = fm1_ms;
        return 0;
    }
    if (fm1_ms - hc_uboot_t0 > 5000u) {
        lcd_fill(0, 0, 240, 240, T_BG);
        draw_text_box(0, 110, 240, &AF_M, "UBOOT", T_THEME, 1);
        printf("%s: OCT- + OCT+ held 5 s: UBOOT\n", FM1CORE_ID);
        fm1core_exit(3);
        return 1;
    }
    if (fm1_ms - hc_uboot_t0 <= 2000u)
        return 0;
    left = (5000u - (fm1_ms - hc_uboot_t0) + 999u) / 1000u;
    if (left != hc_uboot_shown) {
        char d[24] = "UPDATE MODE IN  ";
        d[16] = (char)('0' + left);
        d[17] = 0;
        hc_uboot_shown = left;
        lcd_fill(0, 0, 240, 240, T_BG);
        draw_text_box(0, 100, 240, &AF_M, d, T_THEME, 1);
        draw_text_box(0, 130, 240, &AF_S, "LET GO TO CANCEL", T_MID, 1);
    }
    return 0;
}

/* ---- the glue's state (hidden: reached only through the descriptor) */
fm1panel_t fm1core_hc_panel;
#define HC_PANEL (&fm1core_hc_panel)
static uint8_t hc_ready;
static uint32_t hc_loop_ms;               /* fm1_ms when main.c's loop starts (the splash's 600 ms over) */
static uint8_t hc_looping;

/* ------------------------------------------------------------- input --- */
/* the panel into the HAL: held masks (a tap: pressed now, released at the next call) with the edges ui_input takes,
 * the detents by matrix encoder (the panel's enc[] is by matrix id already), MASTER as the ADC */
static void hc_in(void)
{
    fm1panel_t *p = HC_PANEL;
    const uint32_t b = (p->buttons | fm1p_take_button_taps(p)) & 0x3FFFu;
    const uint32_t k = (p->keys | fm1p_take_key_taps(p)) & ((1u << NKEYS) - 1u);
    uint32_t i;
    hc_pressed |= b & ~fm1_in.buttons;
    fm1_in.buttons = b;
    fm1_in.notes = k;
    for (i = 0; i < FM1P_NENC; i++)
        hc_enc_steps[i] += fm1p_take_enc(p, i);
    hc_master_adc = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
}

/* ------------------------------------------------------------- output --- */
/* the LEDs: lit (fm1_led) full, the glow (fm1_led_dim) DIM; the power-on sweep while it runs (its level per LED,
 * 0..64 / 64 of lit, the glow under it); PLAY's green from its place (column 8, row bit 1) */
static void hc_out(void)
{
    fm1panel_t *p = HC_PANEL;
    uint32_t c, l;
    const int anim = hc_anim_run != 0;
    const uint32_t f = anim ? hc_anim_frame() : 0;
    for (c = 0; c < FM1_NCOL; c++)
        for (l = 0; l < FM1P_NLINE; l++) {
            const uint32_t bit = 1u << (l + 1u);
            uint32_t lv;
            if (anim && FM1_KEYMAP[l + 1u][c] >= 0) {
                const uint32_t a = fm1_anim_level(f, (uint32_t)FM1_KEYMAP[l + 1u][c], hc_anim_glow);
                lv = (a & 0x7Fu) * 255u / 64u;
                if ((a & 0x80u) && lv < FM1P_LED_DIM)
                    lv = FM1P_LED_DIM;
                if (lv > 255u)
                    lv = 255u;
            } else {
                lv = fm1_led[c] & bit ? FM1P_LED_LIT : fm1_led_dim[c] & bit ? FM1P_LED_DIM : FM1P_LED_OFF;
            }
            p->led[c][l] = (uint8_t)lv;
        }
    p->led_play_green = p->led[LED_PLAY_GREEN_COL][LED_PLAY_GREEN_ROW - 1u];
    fm1p_lcd_out(p, hc_fb, hc_draws);
}

/* ------------------------------------------------------- the main loop --- */
static void hc_services(void)            /* main.c: rm_service, ota_service (the update and UBOOT keys: ignored) */
{
    rm_service();
    ota_service();
    if (usb.ota_req) {
        usb.ota_req = 0;
        printf("%s: the update requested over USB: ignored (no update inside a core)\n", FM1CORE_ID);
    }
    if (usb.uboot_req) {
        usb.uboot_req = 0;
        printf("%s: UBOOT requested over USB: ignored (no update inside a core)\n", FM1CORE_ID);
    }
}
static void hc_frame(void)                /* one pass of main.c's loop */
{
    master_poll();
    if (!hc_looping) {                    /* the splash: MASTER polled, MIDI IN plays */
        if ((int32_t)(fm1_ms - hc_loop_ms) < 0)
            return;
        hc_looping = 1;
        kb_boot_hold = 0;
        ui_force = 1;
        hc_uboot_t0 = fm1_ms;
    }
    if (hc_uboot_combo())
        return;
    hc_services();
    ui_input();
    ui_leds();
    if (!(fm1_in.buttons & btn_bit(B_OCTDN) && fm1_in.buttons & btn_bit(B_OCTUP)))
        ui_draw();
}

/* ------------------------------------------------------- entry points --- */
int fm1core_hc_halted(void) { return fm1core_halted(); }
static void hc_cleanup(void) { fm1nor_close(); }
void fm1core_hc_shutdown(void) { fm1core_run_exit(); }

void fm1core_hc_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (every write reaches the image at once; shutdown closes the file) */
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
    fm1core_exit_cleanup = hc_cleanup;
}

void fm1core_hc_init(int demo)
{
    fm1panel_t *p = HC_PANEL;
    uint32_t i;
    (void)demo;
    fm1core_exit_cleanup = hc_cleanup;
    fm1sx_in_init(&hc_sxi, hc_sx_accept);
    fm1sx_out_reset(&hc_sxo);
    fm1nor_open();
    if (p->master > 0)                    /* (0: the host gave none; the ADC's 700) */
        hc_master_adc = p->master > 1023 ? 1023 : p->master;
    /* main.c fm1_main's boot */
    settings_boot();
    palette_set(UI_PALETTE);
    lcd_init();
    draw_splash();
    memset(&felucca_dbg, 0, sizeof felucca_dbg);
    felucca_dbg.magic = DBG_MAGIC;
    keys_init();
    tape_init();
    master_boot();
    kb_boot_hold = 1;                     /* no notes from the keys under the splash */
    audio_init();
    usb.up = usb.config = 1;              /* a host is there: MIDI out flows */
    ui_leds_init();
    fm1_led_anim_start(1);
    hc_loop_ms = fm1_ms + 600u;           /* main.c: the splash's 600 ms, then the loop */
    for (i = 0; i < FM1P_NB; i++)
        p->btn_id[i] = panel.btn[i];
    for (i = 0; i < FM1P_NE - 1u; i++) {
        p->enc_id[i] = panel.enc[i];
        p->enc_dir[i] = (int8_t)(panel.dir[i] < 0 ? -1 : 1);
    }
    p->master = hc_master_adc;
    hc_in();
    hc_ready = 1;
    p->ready = 1;
    hc_out();
}
void fm1core_hc_tick(uint32_t ms) { fm1_ms = ms; }
void fm1core_hc_frame(void)
{
    if (fm1core_halted() || !hc_ready)
        return;
    hc_in();
    hc_frame();
    if (fm1core_halted())
        return;
    hc_out();
}
void fm1core_hc_idle(void)
{
    if (fm1core_halted() || !hc_ready)
        return;
    hc_in();
    if (hc_looping)
        hc_services();                    /* (main.c's wait loop) */
    hc_out();
}
void fm1core_hc_audio(int16_t *out, uint32_t frames)
{
    uint32_t f, k;
    if (fm1core_halted() || !hc_ready) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    for (f = 0; f < frames; f += FM1P_BLOCK) {
        int16_t *o = out + 2u * f;
        const int32_t *a;
        if (fm1nor_stall_take()) {        /* an erase: the IRQs are off, the DMA loops a silenced buffer */
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);
            continue;
        }
        fm1_alnk0_irq();                  /* audio.c: one half (keys_block, synth_render) */
        a = &abuf[hc_half * HALF_WORDS];
        for (k = 0; k < FM1P_BLOCK * 2u; k++) {
            const int32_t v = a[k] >> OUT_SHIFT;
            o[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        hc_half ^= 1u;
    }
}
int fm1core_hc_midi_in(uint32_t pkt)      /* usb.c ep1_take / midi_in_event */
{
    if (fm1core_halted() || !hc_ready)
        return 1;
    if (MQ - (mi_w - mi_r) < 8u)
        return 0;                         /* (the host keeps the packet: the ring has room for TRS kept) */
    if (fm1sx_in_would_refuse(&hc_sxi, pkt))
        return 0;                         /* (a frame waits for the loop: this one after it) */
    pkt &= ~0xF0u;                        /* cable 0 */
    midi_in_event(pkt);
    fm1sx_in_pkt(&hc_sxi, pkt);
    return 1;
}
int fm1core_hc_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
    if (fm1sx_take(&hc_sxo, pkt))
        return 1;                         /* (usb.c ep1_tx: SysEx first) */
    if (hc_mo_r == hc_mo_w)
        return 0;
    *pkt = hc_mo[hc_mo_r++ % HC_OQ];
    return 1;
}
void fm1core_hc_dump(void)
{
    printf("%s: fm1_ms %u  buttons %04X notes %07X  master %d (q12 %u)  loop %u  page %u preset %u osc %u voices %u\n",
           FM1CORE_ID, (unsigned)fm1_ms, (unsigned)fm1_in.buttons, (unsigned)fm1_in.notes, (int)hc_master_adc,
           (unsigned)sys.master_q12, (unsigned)hc_looping, (unsigned)sys.page, (unsigned)sys.preset,
           (unsigned)sys.osc, (unsigned)sys.voices);
    printf("  midi in %u out %u  sysex in %u frames out %u frames (%u lost)  flash: %s\n", (unsigned)(mi_w - mi_r),
           (unsigned)(hc_mo_w - hc_mo_r), (unsigned)hc_sxi.frames, (unsigned)hc_sxo.frames, (unsigned)hc_sxo.lost,
           fm1nor_path()[0] ? fm1nor_path() : "RAM only");
}
void fm1core_hc_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u preset %u '%s' view %u osc %u", (unsigned)sys.page, (unsigned)sys.preset,
             PRESETS[sys.preset % NPRESETS].name, (unsigned)sys.view, (unsigned)sys.osc);
}
void fm1core_hc_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = sys.cpu_q8 * 100u / 256u;
}
const int8_t *fm1core_hc_keymap(void) { return &FM1_KEYMAP[0][0]; }

/* ---- the flash (fm1core.h ABI 2) */
uint8_t *fm1core_hc_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_hc_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_hc_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_hc_flash_sync(void) { return 1; }   /* (SAVE and RESET PRESETS write at once: nothing waits) */
