/* SPDX-License-Identifier: GPL-3.0-only */
/* The ObsSynth FM-1 core (https://github.com/orianbsilva/obssynth-fm1, the pinned, unmodified submodule
 * cores/obssynth-fm1): Orian Beltrame's four-voice synthesizer on Felucca, by way of Hortator (two oscillators per
 * voice, STD / SUPER / WT / GRAIN, a multimode filter, two ADSRs, two LFOs, delay, reverb, chorus). Like Hortator it
 * has no web/emu/felucca_web.c (the unit fw_core.h drives): its browser simulator is its own, tests/sim_core.c over
 * tests/sim_host.h (docs/SIMULATOR.md: the firmware's sources in synth.c's order, the HAL as state the page drives;
 * built as tools/build_sim.sh builds fm1sim.wasm, core_obssynth.cmake). This unit #includes that file, unmodified,
 * and drives it as the page's worklet does, through the same calls and statics (the model: core_hortator.c):
 *
 *   ABI                  the simulator
 *   options / init       hflash (its 1 MiB in RAM): erased, then the staged bytes or a file; the device's boot
 *                        (main.c fm1_main's order, as sim_init: synth_init, presets_boot, settings_boot, ui_init,
 *                        audio_init, a first frame)
 *   frame / idle         the panel into fm1_in / sim_enc_acc / sim_master_raw (as sim_btn, sim_key, sim_enc,
 *                        sim_master), then ONE device millisecond: sim_render of the 44.1 samples due (the clock: the
 *                        audio ISR once per half, the main loop's UI frame every 15 ms of audio, sim_frame), the
 *                        LEDs and the LCD out
 *   audio(128)           the ring sim_render filled (abuf's 24-bit Q15 << OUT_SHIFT) as s16 >> OUT_SHIFT
 *   midi_in              usb.c's ring midi_in_q (as sim_midi: the channel messages and the clock; midi.c drains it in
 *                        the audio ISR), SysEx to the glue's frame buffer (the M-UPGRADE handshake)
 *   midi_out_take        the M-UPGRADE replies only (ObsSynth sends no MIDI of its own)
 *   flash / flash_dirty  hflash / a count of the frames whose erases or programs marked a sector (sim_dirty)
 *   flash_sync           nothing waits: the SAVE page and GLO's settings write at once (presets.c)
 *
 * Added here (what the simulator leaves out of main.c's loop): OCT- + OCT+ held 5 s (UBOOT = exit(3)) and the
 * M-UPGRADE identity: the firmware's own ota.c (common/sysex_ota.h) answering the updater's handshake with
 * FELUCCA_ID "FM-1_627010" (tools/build.py PRODUCT, synth.c's default). Not here: synth.c's info_service (the
 * installer's "Read the FM-1" query, F0 7D 'O' 'B' 'S' 01: synth.c is the device unit, with the hardware; the
 * frame is refused by the filter and dropped), the boot guard (main.c fm1_cstart), USB audio, Bluetooth.
 * The LEDs: the simulator keeps only the lit layer (sim_host.h leds_commit); the backlight's mid layer (GLO LIGHT
 * DIM: a quarter of the scan frames, led_dim.c) is the page's sim_glow / sim_key_glow, drawn here as MID.
 * The version: SYNTH_VERSION "<VERSION.txt>", SYNTH_BUILD the commit (core_obssynth.cmake), as build_sim.sh gives them.
 *
 * Macros and statics here are fm1core_ob_-prefixed (the firmware is in this unit). */
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

#define FELUCCA_ID "FM-1_627010"          /* tools/build.py PRODUCT (synth.c's default): the package identity */

#include "../../cores/obssynth-fm1/tests/sim_core.c"   /* the simulator's core: sim_host.h and the firmware */

/* ---------------------------------------------------- the M-UPGRADE identity --- */
static fm1sx_in_t fm1core_ob_sxi;
static fm1sx_out_t fm1core_ob_sxo;
static int fm1core_ob_sx_accept(const uint8_t *f, uint32_t n) { return fm1sx_is_mupg(f, n); }
#define FM1SX_OTA_C "../../cores/obssynth-fm1/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_ob_sxi
#define FM1SX_OTA_OUT fm1core_ob_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"

#include "exit_end.h"

/* ---- the glue's state (hidden: reached only through the descriptor) ---- */
fm1panel_t fm1core_ob_panel;
#define FM1CORE_OB_PANEL (&fm1core_ob_panel)
#define FM1CORE_OB_RING 4096u             /* frames (a power of two): a millisecond renders 44 or 45 */
static uint8_t fm1core_ob_ready;
static uint32_t fm1core_ob_devms;         /* device milliseconds run */
static uint64_t fm1core_ob_rendered;      /* samples sim_render produced */
static int16_t fm1core_ob_ring[2u * FM1CORE_OB_RING];
static uint32_t fm1core_ob_rw, fm1core_ob_rr;
static uint32_t fm1core_ob_writes;        /* frames in which the firmware erased or programmed */

/* ------------------------------------------------------------- flash --- */
static char fm1core_ob_path[1024];        /* "" RAM only, FM1CORE_FLASH_STAGED, or the file */
static uint32_t fm1core_ob_file_writes;
static void fm1core_ob_note_writes(void) /* sim_dirty's marks (st_erase / st_prog) into the count, cleared */
{
    uint32_t i, any = 0;
    for (i = 0; i < sizeof sim_dirty / sizeof sim_dirty[0]; i++)
        any |= sim_dirty[i];
    if (any) {
        fm1core_ob_writes++;
        sim_flash_clean();
    }
}
static void fm1core_ob_file_write(void)
{
    FILE *f;
    fm1core_ob_note_writes();
    if (!fm1core_ob_path[0] || !strcmp(fm1core_ob_path, FM1CORE_FLASH_STAGED) ||
        fm1core_ob_file_writes == fm1core_ob_writes)
        return;
    f = fopen(fm1core_ob_path, "wb");
    if (f) {
        fwrite(hflash, 1, sizeof hflash, f);
        fclose(f);
    }
    fm1core_ob_file_writes = fm1core_ob_writes;
}
static void fm1core_ob_flash_open(void)  /* the power-on */
{
    sim_flash_reset();
    if (!strcmp(fm1core_ob_path, FM1CORE_FLASH_STAGED)) {
        uint32_t n = fm1nor_stage_len();
        if (n > sizeof hflash)
            n = sizeof hflash;
        if (n && fm1nor_stage_bytes())
            memcpy(hflash, fm1nor_stage_bytes(), n);
    } else if (fm1core_ob_path[0]) {
        FILE *f = fopen(fm1core_ob_path, "rb");
        if (f) {
            size_t got = fread(hflash, 1, sizeof hflash, f);
            (void)got;                    /* (short: the rest stays erased) */
            fclose(f);
        }
    }
    fm1core_ob_file_writes = fm1core_ob_writes;
}
static void fm1core_ob_cleanup(void)     /* exit / shutdown: the file current, later writes RAM only */
{
    fm1core_ob_file_write();
    fm1core_ob_path[0] = 0;
}

/* -------------------------------------------------------------- UBOOT --- */
/* main.c uboot_poll: OCT- + OCT+ held 5 s; the device resets into the ROM's USB boot, a core halts */
static uint32_t fm1core_ob_uboot_t0;
static int fm1core_ob_uboot(void)        /* 1: UBOOT (the core halts) */
{
    const uint32_t both = panel_btn_bit(B_OCTDN) | panel_btn_bit(B_OCTUP);
    if ((fm1_in.buttons & both) != both) {
        fm1core_ob_uboot_t0 = fm1_ms;
        return 0;
    }
    if (fm1_ms - fm1core_ob_uboot_t0 > 5000u) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 110, 240, &FONT_S, "UBOOT", RGB(80, 120, 255), 1);
        printf("%s: OCT- + OCT+ held 5 s: UBOOT\n", FM1CORE_ID);
        fm1core_exit(3);                  /* (the device: fm1_enter_uboot) */
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------- input --- */
/* the panel into the simulator's input, as sim_btn / sim_key / sim_enc / sim_master take it: held masks with the
 * press edges latched until the UI takes them, the detents by matrix encoder (the panel's enc[] is already by matrix
 * id with each role's direction), MASTER as the ADC */
static void fm1core_ob_in(void)
{
    fm1panel_t *p = FM1CORE_OB_PANEL;
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
static void fm1core_ob_out(void)
{
    fm1panel_t *p = FM1CORE_OB_PANEL;
    const uint32_t gb = ui_glow_btns(), gk = ui_glow_keys();
    uint32_t c, l, id;
    for (c = 0; c < FM1_NCOL; c++)
        for (l = 0; l < FM1P_NLINE; l++)
            p->led[c][l] = (fm1_led[c] >> (l + 1u)) & 1u ? FM1P_LED_LIT : FM1P_LED_OFF;
    for (id = 0; id < 41u; id++) {        /* the backlight's mid layer, where an LED is not lit */
        uint32_t on = 0;
        if (id >= 14u)
            on = (gk >> (id - 14u)) & 1u;
        else {
            uint32_t b;
            for (b = 0; b < NB; b++)
                if (panel.btn[b] == id)
                    on = (gb >> b) & 1u;
        }
        if (!on)
            continue;
        for (c = 0; c < FM1_NCOL; c++)
            for (l = 1; l < 5u; l++)
                if (FM1_KEYMAP[l][c] == (int8_t)id && p->led[c][l - 1u] == FM1P_LED_OFF)
                    p->led[c][l - 1u] = FM1P_LED_MID;
    }
    p->led_play_green = p->led[FM1P_PLAY_GREEN_POS >> 3][(FM1P_PLAY_GREEN_POS & 7u) - 1u];
    fm1p_lcd_out_native(p, fb);           /* (fb is native RGB565: lcd_blit swaps the canvas's bytes into it) */
    fm1core_ob_note_writes();
    if (fm1core_ob_file_writes != fm1core_ob_writes)
        fm1core_ob_file_write();
}

/* ------------------------------------------------------- entry points --- */
int fm1core_ob_halted(void) { return fm1core_halted(); }
void fm1core_ob_shutdown(void) { fm1core_run_exit(); }

void fm1core_ob_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    const char *p = no_flash || (headless && !flash_path) ? "" : flash_path ? flash_path : "build/" FM1CORE_ID "/flash.bin";
    (void)save_on_exit;                   /* (every write reaches the image at once; shutdown flushes the file) */
    snprintf(fm1core_ob_path, sizeof fm1core_ob_path, "%s", p);
    fm1core_exit_cleanup = fm1core_ob_cleanup;
}

void fm1core_ob_init(int demo)
{
    fm1panel_t *p = FM1CORE_OB_PANEL;
    uint32_t i;
    (void)demo;
    fm1core_exit_cleanup = fm1core_ob_cleanup;
    fm1sx_in_init(&fm1core_ob_sxi, fm1core_ob_sx_accept);
    fm1sx_out_reset(&fm1core_ob_sxo);
    fm1core_ob_flash_open();
    if (p->master > 0)                    /* (0: the simulator's default, 800) */
        sim_master_raw = (uint32_t)(p->master > 1023 ? 1023 : p->master);
    sim_init(0);                          /* main.c fm1_main's order (the flash as loaded above) */
    fm1core_ob_note_writes();
    fm1core_ob_file_writes = fm1core_ob_writes;   /* (the boot's own writes, if any, reach the file at the next one) */
    for (i = 0; i < FM1P_NB; i++)
        p->btn_id[i] = panel.btn[i];
    for (i = 0; i < FM1P_NE - 1u; i++) {
        p->enc_id[i] = panel.enc[i];
        p->enc_dir[i] = (int8_t)(panel.dir[i] < 0 ? -1 : 1);
    }
    p->master = (int32_t)sim_master_raw;
    fm1core_ob_uboot_t0 = fm1_ms;
    fm1core_ob_in();                      /* (held at power-on: in the input from the start) */
    fm1core_ob_ready = 1;
    p->ready = 1;
    fm1core_ob_out();
}
void fm1core_ob_tick(uint32_t ms) { (void)ms; }
static void fm1core_ob_ms_run(void)      /* frame / idle: the input, the device's millisecond, the output */
{
    uint64_t due;
    if (fm1core_halted() || !fm1core_ob_ready)
        return;
    fm1core_ob_in();
    if (fm1core_ob_uboot())
        return;
    fm1core_ob_devms++;
    due = (uint64_t)fm1core_ob_devms * FS / 1000u;
    while (fm1core_ob_rendered < due) {
        uint32_t n = (uint32_t)(due - fm1core_ob_rendered), k;
        if (n > SIM_MAX_FRAMES)
            n = SIM_MAX_FRAMES;
        sim_render(n);
        for (k = 0; k < n; k++) {
            const uint32_t i = (fm1core_ob_rw++ % FM1CORE_OB_RING) * 2u;
            int32_t l = (int32_t)(sim_out[2u * k] * 8388608.0f) >> OUT_SHIFT;
            int32_t r = (int32_t)(sim_out[2u * k + 1u] * 8388608.0f) >> OUT_SHIFT;
            fm1core_ob_ring[i] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            fm1core_ob_ring[i + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
        fm1core_ob_rendered += n;
    }
    if (fm1core_ob_rw - fm1core_ob_rr > FM1CORE_OB_RING)   /* (never in the ABI's order: audio() keeps up) */
        fm1core_ob_rr = fm1core_ob_rw - FM1CORE_OB_RING;
    ota_service();                        /* main.c's loop: the M-UPGRADE handshake */
    fm1core_ob_out();
}
/* ObsSynth's knob row as draw_nums draws it in the top strip (firmware/src/ui.c:484, from draw_top): the shown page
 * (ui_view: the + page while HOME's shift is on), each knob's parameter (page_param), its label (PW1 / PW2: "SHAPE"
 * off SQR) and param_text; a column with no parameter (0xFF): empty. Nothing given on SAVE (the knobs pick the slot,
 * the cursor, the letter: draw_save shows them); INFO's knobs do nothing (four empty columns). */
#include "rd_cols.h"
static void fm1core_obssynth_rd(void)
{
    uint32_t k, v = ui_view();
    rdc_begin();
    if (v == PG_SAVE)
        return;
    for (k = 0; k < 4u; k++) {
        uint32_t id = page_param(v, k);
        const char *lb;
        char b[16];
        if (id >= P_COUNT) {
            rdc_col(k, "", "");
            continue;
        }
        lb = (id == P_PW1 || id == P_PW2) && par[id == P_PW1 ? P_WAVE : P_WAVE2] != W_SQR ? "SHAPE" : PARAMS[id].label;
        param_text(b, id);
        rdc_col(k, lb, b);
    }
}

void fm1core_ob_frame(void) { fm1core_ob_ms_run(); fm1core_obssynth_rd(); }
void fm1core_ob_idle(void) { fm1core_ob_ms_run(); }
void fm1core_ob_audio(int16_t *out, uint32_t frames)
{
    uint32_t k;
    if (fm1core_halted() || !fm1core_ob_ready) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    for (k = 0; k < frames; k++) {
        if (fm1core_ob_rr == fm1core_ob_rw) {
            out[2u * k] = out[2u * k + 1u] = 0;
            continue;
        }
        {
            const uint32_t i = (fm1core_ob_rr++ % FM1CORE_OB_RING) * 2u;
            out[2u * k] = fm1core_ob_ring[i];
            out[2u * k + 1u] = fm1core_ob_ring[i + 1u];
        }
    }
}
int fm1core_ob_midi_in(uint32_t pkt)
{
    const uint32_t cin = pkt & 15u;
    if (fm1core_halted() || !fm1core_ob_ready)
        return 1;                         /* (halted: taken and dropped, never "retry") */
    if (fm1sx_in_would_refuse(&fm1core_ob_sxi, pkt))
        return 0;                         /* (a handshake waits for the main loop: this one after it) */
    if (cin >= 8u) {                      /* (sim_midi: the channel messages and the single bytes, into usb.c's ring) */
        if (mi_w - mi_r >= MQ)
            return 0;
        midi_in_q[mi_w % MQ] = pkt & ~0xF0u;      /* (cable 0, as sim_midi builds it) */
        RING_PUBLISH();
        mi_w++;
    }
    fm1sx_in_pkt(&fm1core_ob_sxi, pkt);
    return 1;
}
int fm1core_ob_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
    return fm1sx_take(&fm1core_ob_sxo, pkt);
}
void fm1core_ob_dump(void)
{
    uint32_t c;
    printf("%s: fm1_ms %u  buttons %04X notes %07X  master %u  voices %u  page %u preset %u%s\n", FM1CORE_ID,
           (unsigned)fm1_ms, (unsigned)fm1_in.buttons, (unsigned)fm1_in.notes, (unsigned)sim_master_raw,
           (unsigned)synth_voices(), (unsigned)ui.page, (unsigned)ui.pre, ui.pre_ed ? "*" : "");
    printf("  leds fm1_led:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X", fm1_led[c]);
    printf("\n  flash: %s, %u writes\n", fm1core_ob_path[0] ? fm1core_ob_path : "RAM only",
           (unsigned)fm1core_ob_writes);
}
void fm1core_ob_ui_info(char *buf, uint32_t n)   /* the screen the last frame drew */
{
    snprintf(buf, n, "page %u preset '%s' voices %u msg '%s'", (unsigned)ui.page, sim_preset_name(),
             (unsigned)synth_voices(), ui.msg ? ui.msg_b : "");
}
void fm1core_ob_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = cpu_q8 * 100u / 256u;
}
const int8_t *fm1core_ob_keymap(void) { return &FM1_KEYMAP[0][0]; }

/* ---- the flash (fm1core.h ABI 2) ---- */
uint8_t *fm1core_ob_flash(void) { return hflash; }
uint32_t fm1core_ob_flash_dirty(void)
{
    fm1core_ob_note_writes();
    return fm1core_ob_writes;
}
int fm1core_ob_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_ob_flash_sync(void)          /* nothing deferred: the saves write at once */
{
    if (fm1core_halted() || !fm1core_ob_ready)
        return 1;
    fm1core_ob_file_write();
    return 1;
}
