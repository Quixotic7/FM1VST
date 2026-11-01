/* SPDX-License-Identifier: GPL-3.0-only */
/* The Hortator core (https://github.com/deadactive/hortator, the pinned, unmodified submodule cores/hortator):
 * DEADACTIVE's drum machine on Felucca (eight drum tracks, the step sequencer, Grids, COMP, RESON, LFOs). Hortator
 * has no web/emu/felucca_web.c (the unit fw_core.h drives): its browser simulator is its own, tests/sim_core.c over
 * tests/sim_host.h (docs/SIMULATOR.md: the firmware's sources, felucca.c's list in its order, with the HAL as state
 * the page drives; built as tools/build_sim.sh builds fm1sim.wasm, core_hortator.cmake). This unit #includes that
 * file, unmodified, and drives it as the page's worklet does, through the same calls and statics:
 *
 *   ABI                  the simulator
 *   options / init       hflash (its 1 MiB in RAM): erased, then the staged bytes or a file; the device's boot
 *                        (main.c fm1_main's order, as sim_init: persist_boot, settings_init, panel_init,
 *                        drum_boot_init, audio_init, a first frame), without sim_init's web-only parts: no first-visit
 *                        demo projects (sim_demo.c) and no "returning visitor" load of slot 1 (the device boots the
 *                        last saved project, project_boot, or its power-on pattern)
 *   frame / idle         the panel into fm1_in / sim_enc_acc / sim_master_raw (as sim_btn, sim_key, sim_enc,
 *                        sim_master), then ONE device millisecond: sim_render of the 44.1 samples due (the clock: the
 *                        audio ISR once per half, the main loop's UI frame every 15 ms of audio, sim_frame), the
 *                        LEDs and the LCD out
 *   audio(128)           the ring sim_render filled (abuf's 24-bit Q15 << OUT_SHIFT), as s16 >> OUT_SHIFT: Q15 =
 *                        0 dBFS, as the felucca_web family (the page plays it 6 dB lower: abuf / 2^23)
 *   midi_in              usb_app8.c midi_in_event (the device's EP1 path: channel messages and clock into the ring
 *                        the audio ISR drains, SysEx bytes to sysex_byte: its UBOOT key)
 *   midi_out_take        usb_app8.c midi_out_q (usb.config = 1: a host is there)
 *   flash / flash_dirty  hflash / a count of the frames whose erases or programs marked a sector (sim_dirty)
 *   flash_sync           settings_poll (a settings save, MIDI LEARN's map or a user preset left for the STOP):
 *                        written as soon as the transport is stopped. Projects are written when SAVEd (Hortator has
 *                        no autosave)
 *
 * Added here (what the simulator leaves out of main.c's loop): OCT- + OCT+ held 5 s (UBOOT = exit(3), its countdown
 * in the top bar as main.c's ui_say), the UBOOT SysEx key (F0 22 24 35 7D F7, usb.uboot_req: ignored, no update
 * inside a core), and the M-UPGRADE identity: the firmware's own ota.c (common/sysex_ota.h; the simulator builds
 * FELUCCA_OTA 0, so usb_app8.c's SysEx frame plumbing is out and the glue's frame buffer stands in), answering the
 * updater's handshake with FELUCCA_ID "FM-1_900", the identity tools/build.py gives every Hortator package
 * (PRODUCT; only a --release X.Y build is FM-1_9XY, and tools/publish.sh builds none). Hortator's web editor is not
 * in the firmware (felucca.c ed_service is a no-op: "the web editor returns in M5"), so there is no editor service.
 * Not here: the boot guard (main.c fm1_cstart; the simulator has no bootguard record), OCT- + OCT+ held at power-on
 * (panel_setup, a busy-wait the simulator leaves out too), USB audio (FELUCCA_UAC 0), Bluetooth (the plain build).
 * The version: FELUCCA_VERSION "DRUM-<VERSION.txt>" (core_hortator.cmake), as build_sim.sh and build.py give it.
 *
 * Macros and statics here are fm1core_hr_-prefixed (the firmware is in this unit). */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"                      /* the panel (before sim_host.h's __attribute__ macro) */
#include "panel.h"                        /* (core-glue/common) the panel helpers */
#include "nor.h"                          /* (core-glue/common) the staged bytes (fm1nor_stage) */
#include "version.h"
#include "exit.h"                         /* exit / atexit -> fm1core_exit / fm1core_atexit (common/exit.h) */
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */
#include "sysex.h"                        /* (core-glue/common) the frame buffer and the reply ring */

#define FELUCCA_ID "FM-1_900"             /* tools/build.py PRODUCT (felucca.c's default): the package identity */

#include "../../cores/hortator/tests/sim_core.c"   /* the simulator's core: sim_host.h, the firmware, sim_demo / lab */

/* ---------------------------------------------------- the M-UPGRADE identity --- */
static fm1sx_in_t fm1core_hr_sxi;
static fm1sx_out_t fm1core_hr_sxo;
static int fm1core_hr_sx_accept(const uint8_t *f, uint32_t n) { return fm1sx_is_mupg(f, n); }
#define FM1SX_OTA_C "../../cores/hortator/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_hr_sxi
#define FM1SX_OTA_OUT fm1core_hr_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

#include "exit_end.h"

/* ---- the glue's state (hidden: reached only through the descriptor) ---- */
fm1panel_t fm1core_hr_panel;
#define FM1CORE_HR_PANEL (&fm1core_hr_panel)
#define FM1CORE_HR_RING 4096u             /* frames (a power of two): a millisecond renders 44 or 45 */
static uint8_t fm1core_hr_ready;
static uint32_t fm1core_hr_devms;         /* device milliseconds run */
static uint64_t fm1core_hr_rendered;      /* samples sim_render produced */
static int16_t fm1core_hr_ring[2u * FM1CORE_HR_RING];
static uint32_t fm1core_hr_rw, fm1core_hr_rr;
static uint32_t fm1core_hr_writes;        /* frames in which the firmware erased or programmed */

/* ------------------------------------------------------------- flash --- */
/* the image is the simulator's hflash: erased at power-on, then the staged bytes (ABI 2) or a file, read at power-on
 * and rewritten whole after a millisecond in which the firmware wrote and at shutdown */
static char fm1core_hr_path[1024];        /* "" RAM only, FM1CORE_FLASH_STAGED, or the file */
static uint32_t fm1core_hr_file_writes;
static void fm1core_hr_note_writes(void) /* sim_dirty's marks (st_erase / st_prog) into the count, cleared */
{
    uint32_t i, any = 0;
    for (i = 0; i < sizeof sim_dirty / sizeof sim_dirty[0]; i++)
        any |= sim_dirty[i];
    if (any) {
        fm1core_hr_writes++;
        sim_flash_clean();
    }
}
static void fm1core_hr_file_write(void)
{
    FILE *f;
    fm1core_hr_note_writes();
    if (!fm1core_hr_path[0] || !strcmp(fm1core_hr_path, FM1CORE_FLASH_STAGED) ||
        fm1core_hr_file_writes == fm1core_hr_writes)
        return;
    f = fopen(fm1core_hr_path, "wb");
    if (f) {
        fwrite(hflash, 1, sizeof hflash, f);
        fclose(f);
    }
    fm1core_hr_file_writes = fm1core_hr_writes;
}
static void fm1core_hr_flash_open(void)  /* the power-on */
{
    sim_flash_reset();
    if (!strcmp(fm1core_hr_path, FM1CORE_FLASH_STAGED)) {
        uint32_t n = fm1nor_stage_len();
        if (n > sizeof hflash)
            n = sizeof hflash;
        if (n && fm1nor_stage_bytes())
            memcpy(hflash, fm1nor_stage_bytes(), n);
    } else if (fm1core_hr_path[0]) {
        FILE *f = fopen(fm1core_hr_path, "rb");
        if (f) {
            size_t got = fread(hflash, 1, sizeof hflash, f);
            (void)got;                    /* (short: the rest stays erased) */
            fclose(f);
        }
    }
    fm1core_hr_file_writes = fm1core_hr_writes;
}
static void fm1core_hr_cleanup(void)     /* exit / shutdown: the file current, later writes RAM only */
{
    fm1core_hr_file_write();
    fm1core_hr_path[0] = 0;
}

/* -------------------------------------------------------------- UBOOT --- */
/* main.c's loop: OCT- + OCT+ held 5 s (a countdown in the top bar from 2 s, letting go cancels it), and the UBOOT
 * SysEx (usb.uboot_req), which a core does not act on */
static uint32_t fm1core_hr_uboot_t0, fm1core_hr_uboot_shown;
static int fm1core_hr_uboot(void)        /* 1: UBOOT (the core halts) */
{
    const uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    if ((fm1_in.buttons & both) != both) {
        if (fm1core_hr_uboot_shown)
            ui_say("UPDATE MODE ", "CANCELLED");
        fm1core_hr_uboot_shown = 0;
        fm1core_hr_uboot_t0 = fm1_ms;
    } else if (fm1_ms - fm1core_hr_uboot_t0 > 2000u && fm1_ms - fm1core_hr_uboot_t0 <= 5000u) {
        uint32_t left = (5000u - (fm1_ms - fm1core_hr_uboot_t0) + 999u) / 1000u;
        if (left != fm1core_hr_uboot_shown) {
            char d[4] = {(char)('0' + left), '.', '.', 0};
            ui_say("UPDATE MODE IN ", d);
            fm1core_hr_uboot_shown = left;
        }
    } else if (fm1_ms - fm1core_hr_uboot_t0 > 5000u) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 110, 240, &FONT_S, "UBOOT", RGB(80, 120, 255), 1);
        printf("%s: OCT- + OCT+ held 5 s: UBOOT\n", FM1CORE_ID);
        fm1core_exit(3);                  /* (the device: fm1_enter_uboot) */
        return 1;
    }
    if (usb.uboot_req) {                  /* SysEx F0 22 24 35 7D F7: the device resets into the ROM's USB boot (an
                                           * installer's first step); a core is not updated that way: ignored */
        usb.uboot_req = 0;
        printf("%s: UBOOT requested over USB: ignored (no update inside a core)\n", FM1CORE_ID);
    }
    return 0;
}

/* ------------------------------------------------------------- input --- */
/* the panel into the simulator's input, as sim_btn / sim_key / sim_enc / sim_master take it: held masks with the
 * press edges latched until the UI takes them (a tap: pressed now, released at the next ms), the detents by matrix
 * encoder (the panel's enc[] is already by matrix id with each role's direction), MASTER as the ADC */
static void fm1core_hr_in(void)
{
    fm1panel_t *p = FM1CORE_HR_PANEL;
    const uint32_t b = (p->buttons | fm1p_take_button_taps(p)) & 0x3FFFu;
    const uint32_t k = (p->keys | fm1p_take_key_taps(p)) & ((1u << 27) - 1u);
    uint32_t i;
    fm1_in.pressed |= b & ~fm1_in.buttons;
    fm1_in.buttons = b;
    fm1_in.notes_pressed |= k & ~fm1_in.notes;
    fm1_in.notes = k;
    for (i = 0; i < FM1P_NENC; i++)
        sim_enc_acc[i] += fm1p_take_enc(p, i);
    if (p->master > 0)
        sim_master_raw = (uint32_t)(p->master > 1023 ? 1023 : p->master);
}

/* ------------------------------------------------------------- output --- */
/* the LEDs as levels. MENU > BACKLIGHT OFF (led_dim_on 0): fm1_led, lit or dark. Otherwise the UI's four layers per
 * column (led_dim.c led_lay: full, mid, glow, breath), which the device's scan turns into PWM: full LIT, mid (25 % of
 * the frames) MID, the glow DIM (BACKLIGHT DIM HI) or faint (DIM LO), the breath its level now (led_dim.c's curve:
 * dark .. ~60 % of lit and back in LED_BR_FRAMES frames of ~1.1 ms, from its peak) */
#define FM1CORE_HR_GLOW_LO 24u
static uint32_t fm1core_hr_br_t0;
static uint8_t fm1core_hr_br_on;
static uint32_t fm1core_hr_breath(void)
{
    uint32_t c, any = 0, x;
    for (c = 0; c < FM1_NCOL; c++)
        any |= led_lay[c] >> 24;
    if (!led_dim_on || !any) {
        fm1core_hr_br_on = 0;
        return 0;
    }
    if (!fm1core_hr_br_on) {
        fm1core_hr_br_on = 1;
        fm1core_hr_br_t0 = fm1_ms;
    }
    x = (LED_BR_FRAMES / 2u + (fm1_ms - fm1core_hr_br_t0) * 10u / 11u) & (LED_BR_FRAMES - 1u);
    return led_br_level(x) * 255u >> 16;
}
static uint8_t fm1core_hr_led(uint32_t c, uint32_t r, uint32_t breath)
{
    const uint32_t bit = 1u << r;
    uint32_t lay, lv = 0;
    if (!led_dim_on)
        return (fm1_led[c] & bit) ? FM1P_LED_LIT : FM1P_LED_OFF;
    lay = led_lay[c];
    if (lay & bit)
        return FM1P_LED_LIT;
    if ((lay >> 8) & bit)
        lv = FM1P_LED_MID;
    if (((lay >> 16) & bit) && led_glow_w) {
        const uint32_t g = led_glow_w <= LED_GLOW_LO ? FM1CORE_HR_GLOW_LO : FM1P_LED_DIM;
        if (g > lv)
            lv = g;
    }
    if (((lay >> 24) & bit) && breath > lv)
        lv = breath;
    return (uint8_t)(lv > 255u ? 255u : lv);
}
static void fm1core_hr_out(void)
{
    fm1panel_t *p = FM1CORE_HR_PANEL;
    const uint32_t br = fm1core_hr_breath();
    uint32_t c, l;
    for (c = 0; c < FM1_NCOL; c++)
        for (l = 0; l < FM1P_NLINE; l++)
            p->led[c][l] = fm1core_hr_led(c, l + 1u, br);
    p->led_play_green = p->led[FM1P_PLAY_GREEN_POS >> 3][(FM1P_PLAY_GREEN_POS & 7u) - 1u];
    fm1p_lcd_out_native(p, fb);           /* (fb is native RGB565: lcd_blit swaps the canvas's bytes into it) */
    fm1core_hr_note_writes();
    if (fm1core_hr_file_writes != fm1core_hr_writes)
        fm1core_hr_file_write();
}

/* ------------------------------------------------------- entry points --- */
int fm1core_hr_halted(void) { return fm1core_halted(); }
void fm1core_hr_shutdown(void) { fm1core_run_exit(); }

void fm1core_hr_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    const char *p = no_flash || (headless && !flash_path) ? "" : flash_path ? flash_path : "build/" FM1CORE_ID "/flash.bin";
    (void)save_on_exit;                   /* (every write reaches the image at once; shutdown flushes the file) */
    snprintf(fm1core_hr_path, sizeof fm1core_hr_path, "%s", p);
    fm1core_exit_cleanup = fm1core_hr_cleanup;
}

/* at shutdown (the core's exit handlers): what the firmware left for the STOP is written if it is stopped
 * (settings_poll), and the file flushed (fm1core_hr_cleanup) */
static void fm1core_hr_at_exit(void)
{
    if (fm1core_hr_ready && flash_ok)
        settings_poll();
}

void fm1core_hr_init(int demo)
{
    fm1panel_t *p = FM1CORE_HR_PANEL;
    uint32_t i;
    (void)demo;
    fm1core_exit_cleanup = fm1core_hr_cleanup;
    fm1sx_in_init(&fm1core_hr_sxi, fm1core_hr_sx_accept);
    fm1sx_out_reset(&fm1core_hr_sxo);
    fm1core_hr_flash_open();
    if (p->master > 0)                    /* (0: the simulator's default, 800) */
        sim_master_raw = (uint32_t)(p->master > 1023 ? 1023 : p->master);
    /* the power-on: main.c fm1_main's order, as sim_init (fresh 0) */
    persist_boot();
    settings_init();
    memset(&felucca_dbg, 0, sizeof felucca_dbg);
    felucca_dbg.magic = DBG_MAGIC;
    felucca_dbg.boots = 1;
    panel_init();
    drum_boot_init();                     /* (main.c felucca_init: the last saved project, project_boot) */
    audio_init();
    usb.up = 1;                           /* a host is there: MIDI out flows (usb_app8.c midi_out_event) */
    usb.config = 1;
    sim_knob = (int32_t)sim_master_raw * 16;
    sim_frame();                          /* settles MASTER and draws the first screen */
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.force = 1;
    sim_next_frame = sim_samples + SIM_FRAME_SAMPLES;
    fm1core_hr_note_writes();
    fm1core_hr_file_writes = fm1core_hr_writes;   /* (the boot's own writes, if any, reach the file at the next one) */
    fm1core_atexit(fm1core_hr_at_exit);
    for (i = 0; i < FM1P_NB; i++)
        p->btn_id[i] = panel.btn[i];
    for (i = 0; i < FM1P_NE - 1u; i++) {
        p->enc_id[i] = panel.enc[i];
        p->enc_dir[i] = (int8_t)(panel.dir[i] < 0 ? -1 : 1);
    }
    p->master = (int32_t)sim_master_raw;
    fm1core_hr_uboot_t0 = fm1_ms;
    fm1core_hr_in();                      /* (held at power-on: in the input from the start) */
    fm1core_hr_ready = 1;
    p->ready = 1;
    fm1core_hr_out();
}
void fm1core_hr_tick(uint32_t ms) { (void)ms; }
static void fm1core_hr_ms_run(void)      /* frame / idle: the input, the device's millisecond, the output */
{
    uint64_t due;
    if (fm1core_halted() || !fm1core_hr_ready)
        return;
    fm1core_hr_in();
    if (fm1core_hr_uboot())
        return;
    fm1core_hr_devms++;
    due = (uint64_t)fm1core_hr_devms * FS / 1000u;
    while (fm1core_hr_rendered < due) {
        uint32_t n = (uint32_t)(due - fm1core_hr_rendered), k;
        if (n > SIM_MAX_FRAMES)
            n = SIM_MAX_FRAMES;
        sim_render(n);
        for (k = 0; k < n; k++) {
            const uint32_t i = (fm1core_hr_rw++ % FM1CORE_HR_RING) * 2u;
            int32_t l = (int32_t)(sim_out[2u * k] * 8388608.0f) >> OUT_SHIFT;
            int32_t r = (int32_t)(sim_out[2u * k + 1u] * 8388608.0f) >> OUT_SHIFT;
            fm1core_hr_ring[i] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            fm1core_hr_ring[i + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
        fm1core_hr_rendered += n;
    }
    if (fm1core_hr_rw - fm1core_hr_rr > FM1CORE_HR_RING)   /* (never in the ABI's order: audio() keeps up) */
        fm1core_hr_rr = fm1core_hr_rw - FM1CORE_HR_RING;
    ota_service();                        /* main.c's loop: the M-UPGRADE handshake */
    fm1core_hr_out();
}
void fm1core_hr_frame(void) { fm1core_hr_ms_run(); }
void fm1core_hr_idle(void) { fm1core_hr_ms_run(); }
void fm1core_hr_audio(int16_t *out, uint32_t frames)
{
    uint32_t k;
    if (fm1core_halted() || !fm1core_hr_ready) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    for (k = 0; k < frames; k++) {
        if (fm1core_hr_rr == fm1core_hr_rw) {
            out[2u * k] = out[2u * k + 1u] = 0;
            continue;
        }
        {
            const uint32_t i = (fm1core_hr_rr++ % FM1CORE_HR_RING) * 2u;
            out[2u * k] = fm1core_hr_ring[i];
            out[2u * k + 1u] = fm1core_hr_ring[i + 1u];
        }
    }
}
int fm1core_hr_midi_in(uint32_t pkt)
{
    if (fm1core_halted() || !fm1core_hr_ready)
        return 1;                         /* (halted: taken and dropped, never "retry") */
    if (MQ - (mi_w - mi_r) < 8u)          /* (usb_app8.c ep1_take: room kept for TRS MIDI) */
        return 0;
    if (fm1sx_in_would_refuse(&fm1core_hr_sxi, pkt))
        return 0;                         /* (a handshake waits for the main loop: this one after it) */
    midi_in_event(pkt);                   /* (as the device's endpoint takes it) */
    fm1sx_in_pkt(&fm1core_hr_sxi, pkt);
    return 1;
}
int fm1core_hr_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
    if (fm1sx_take(&fm1core_hr_sxo, pkt))
        return 1;                         /* (usb_app8.c ep1_tx: SysEx first, never split by notes) */
    if (mo_r == mo_w)
        return 0;
    *pkt = midi_out_q[mo_r % MQ];
    mo_r++;
    return 1;
}
void fm1core_hr_dump(void)
{
    uint32_t c, i;
    printf("%s: fm1_ms %u  buttons %04X notes %07X  master %u  led_dim %u\n", FM1CORE_ID, (unsigned)fm1_ms,
           (unsigned)fm1_in.buttons, (unsigned)fm1_in.notes, (unsigned)sim_master_raw, (unsigned)led_dim_on);
    printf("  leds fm1_led/led_lay:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%08X", fm1_led[c], (unsigned)led_lay[c]);
    printf("\n");
    for (i = 0; i < NTRK; i++)
        printf("  track %u%s: model %s, level %d\n", (unsigned)i + 1u, i == song.sel ? "*" : "",
               N_MODEL[(uint32_t)trk[i].p[P_MODEL] % NMODELS], (int)trk[i].p[P_LEVEL]);
    printf("  playing %u page %u home %u master_q12 %u cpu %u%% sheds %u  midi in %u out %u\n",
           (unsigned)song.playing, (unsigned)ui.page, (unsigned)ui.home, (unsigned)song.master_q12,
           (unsigned)(song.cpu_q8 * 100u / 256u), (unsigned)song.sheds, (unsigned)(mi_w - mi_r),
           (unsigned)(mo_w - mo_r));
    printf("  flash: %s, %u writes%s\n", fm1core_hr_path[0] ? fm1core_hr_path : "RAM only",
           (unsigned)fm1core_hr_writes, ui.persist_pending ? ", a settings save pending" : "");
}
void fm1core_hr_ui_info(char *buf, uint32_t n)   /* the screen the last frame drew */
{
    snprintf(buf, n, "%s page %u '%s' track %u menu %u msg '%s' playing %u", ui.home ? "home" : "page",
             (unsigned)ui.page, ui.home ? "HOME" : cur_page()->title, (unsigned)song.sel + 1u, (unsigned)ui.menu,
             ui.msg_t ? ui.msg : "", (unsigned)song.playing);
}
void fm1core_hr_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = song.sheds;
    *cpu_pct = song.cpu_q8 * 100u / 256u;
}
const int8_t *fm1core_hr_keymap(void) { return &FM1_KEYMAP[0][0]; }

/* ---- the flash (fm1core.h ABI 2) ---- */
uint8_t *fm1core_hr_flash(void) { return hflash; }
uint32_t fm1core_hr_flash_dirty(void)
{
    fm1core_hr_note_writes();
    return fm1core_hr_writes;
}
int fm1core_hr_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
/* make the image current: what settings_poll writes once the transport stops (a settings save asked for while
 * playing, MIDI LEARN's map, a user preset). 1: current; 0: the firmware defers a write (the transport runs) */
int fm1core_hr_flash_sync(void)
{
    int ok;
    if (fm1core_halted() || !fm1core_hr_ready || !flash_ok)
        return 1;
    settings_poll();
    ok = !ui.persist_pending && !ui.ml.save && !upr_pending;
    fm1core_hr_file_write();
    return ok;
}
