/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware side of the Tonitor core: the firmware (tonitor_firmware.h, tonitor_hal.h over the unmodified
 * submodule) and the device's power-on and main loop (firmware/src/main.c fm1_cstart / fm1_main) re-done on the
 * ABI's clock. Tonitor has no emulator, and its tests/host/host.c compiles the DSP, the layers, the UI and the
 * sessions without main.c or usb.c and calls them itself (`host screens`, `host flows`): this file is the main loop
 * that harness lacks, as core-glue/sloop/sloop_fw.c and core-glue/melodee/melodee_fw.c are for theirs, with nothing
 * of the firmware changed.
 *
 * THE MAIN LOOP RUNS ON A COROUTINE (core-glue/common/coro.h), as Melodee's: fm1_main never returns and waits in
 * places the ISRs move on, which on one thread need the host's clock to move (tonitor_hal.h: fm1_wdt_feed and
 * fm1_delay_ms yield there). fm1core_tn_main below is fm1_main from its 700 ms splash on; the power-on before it has
 * no waits and runs in init. The coroutine is resumed once per device ms by frame() and idle(): the loop's body every
 * 15 ms, ui_input in between (main.c's `while (fm1_ms - m < 15u) ui_input();`), and whatever wait is in progress.
 *   init   fm1_cstart's boot guard (bootguard.h's rules), then fm1_main's power-on in its order: the flash (the
 *          JEDEC check, ota_boot_cleanup, ses_scan, setup_load), the splash, layers_init, ui_init, sounds_init,
 *          CPU1 (fm1_core1_boot_allowed: a boot after a failed one renders on one core and says "1 CORE"),
 *          audio_init, USB (up and configured: MIDI out flows); then the coroutine, up to its first wait
 *   frame / idle   the coroutine for this device ms
 *   tick   TIMER5's ms and the matrix scan's debounce (a press at once, a release after ~9 ms open)
 *   audio  the ALNK0 ISR, one 256-frame half per two ABI blocks (common/fifo), silent while a flash erase stalls
 * Built out (hardware): usb_start / usb_retry / usb_poll (the SIE), uart_midi_* (the TRS jack), TIMER5, the guards,
 * the update session (usb.ota_req: M-UPGRADE's F0 22 24 35 7F F7) and the USB UBOOT key (usb.uboot_req: F0 22 24 35
 * 7D F7): both are ignored, the core plays on (core-glue/common/README.md, sysex); the rescue mode (two failed boots
 * in a row, or OCT- held at power-on): a core has no USB updater, a guard's recovery boot halts it (exit 3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "panel.h"                        /* (core-glue/common) */
#include "fifo.h"

#include "tonitor_firmware.h"

/* ------------------------------------------------------------- flash --- */
void fm1core_tn_fw_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (nothing is kept in RAM to save at exit: sessions are saved by hand,
                                           * GLOBAL SETUP as it is left) */
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}

/* the boot guard (bootguard.h; main.c fm1_cstart): a run that died before its 30 s mark leaves pending set in
 * .noinit; the next reset counts it: a boot after a failed one starts without CPU1, the second failed boot in a row
 * enters the rescue mode (the USB updater: recovery_main) instead of the firmware, a reset inside it the ROM's
 * UBOOT. A core has neither: exit(3) there. reason and stage are accepted and ignored (no reset reasons). */
static uint8_t fm1core_tn_guard_set;
int fm1core_tn_fw_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
    bootguard_clear(&bootguard);
    if (fail >= 0) {
        bootguard.failed = (uint32_t)fail;
        bootguard.pending = 1;            /* the last run died within 30 s */
    }
    fm1core_tn_guard_set = 1;
    return 1;
}

/* ----------------------------------------------------------- power-on --- */
#define FM1CORE_TN_RELEASE_MS 9u          /* fm1_input.h: a release needs FM1_DEB_RELEASE frames open (~9 ms) */
static uint32_t fm1core_tn_in_btn, fm1core_tn_in_key;           /* the debounced state the tick delivers */
static uint32_t fm1core_tn_btn_ms[32], fm1core_tn_key_ms[32];   /* ms each went down */
static uint8_t fm1core_tn_looping;        /* the splash is over: the main loop runs */
static uint32_t fm1core_tn_frames;
static fm1coro_t *fm1core_tn_co;          /* the main loop (fm1core_tn_main) */
static fm1fifo_t fm1core_tn_fifo;

static void fm1core_tn_render(void *ctx, void *buf, uint32_t frames)   /* the ALNK0 ISR: one half */
{
    (void)ctx;
    (void)frames;
    fm1core_tn_half ^= 1u;
    fm1_alnk0_irq();
    memcpy(buf, &abuf[fm1core_tn_half * HALF_WORDS], HALF_WORDS * sizeof abuf[0]);
}

/* main.c enter_uboot without the hardware (usb_detach, its delays): the screen, then the reset into UBOOT */
static void fm1core_tn_uboot(const char *why)
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 104, 240, &FONT_M, why, RGB(80, 140, 255), 1);
    bootguard.pending = 0;                /* an intended reset: not a failed boot */
    printf("%s: %s\n", FM1CORE_ID, why);
    fm1_enter_uboot();                    /* exit(3): the coroutine is parked, never resumed */
}

/* main.c fm1_main from its 700 ms splash wait on: the loop, its body in main.c's order. Built out with the hardware:
 * fm1_wdt_feed (no watchdog; here it is a wait), usb_retry (USB is up), ota_session (usb.ota_req), enter_uboot's
 * USB detach; usb.uboot_req is ignored (see the top) */
static void fm1core_tn_main(void *arg)
{
    int32_t knob = 512 * 16, batt_acc = 0;
    uint32_t healthy_since, t_both = 0, batt_at = 0;
    (void)arg;
    fm1_delay_ms(700);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    healthy_since = fm1_ms;
    fm1core_tn_looping = 1;
    for (;;) {
        uint32_t m = fm1_ms;
        if ((uint32_t)(m - healthy_since) >= 30000u && bootguard.pending)
            bootguard_clear(&bootguard);  /* 30 s of this boot: a good one */
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                master_q12 = (int32_t)((k10 * k10) >> 8);   /* 0 .. ~4096 */
            }
        }
        if ((uint32_t)(m - batt_at) >= 20u) {             /* the battery's divider (ADC 3), 50 times a second */
            int32_t b = fm1_adc_read(FM1_ADC_BATT);
            batt_at = m;
            if (b > 0) {
                batt_acc = batt_acc ? batt_acc + (b * 32 - batt_acc) / 32 : b * 32;
                batt_raw = batt_acc / 32;
            }
        }
        {   /* OCT- and OCT+ held 5 s: UBOOT, the chip's own updater */
            uint32_t both = (1u << PANEL_BTN[B_OCTDN]) | (1u << PANEL_BTN[B_OCTUP]);
            if ((fm1_in.buttons & both) != both)
                t_both = fm1_ms;
            else if ((uint32_t)(fm1_ms - t_both) > 5000u) {
                fm1core_tn_uboot("UBOOT");
                return;
            }
        }
        ota_service();                    /* the installer's handshake, the backup page's records (backup.c) */
        if (usb.ota_req) {                /* M-UPGRADE's update: a core is not updated that way */
            usb.ota_req = 0;
            printf("%s: M-UPGRADE update request ignored (no update inside a core)\n", FM1CORE_ID);
        }
        if (usb.uboot_req) {              /* the UBOOT key over USB: the device would leave for its ROM loader */
            usb.uboot_req = 0;
            printf("%s: UBOOT key over USB ignored (no ROM loader in a core)\n", FM1CORE_ID);
        }
        ui_input();
        setup_poll();                     /* (GLOBAL SETUP left: its changes written) */
        midi_poll();                      /* (the tracks' CCs sent as they change) */
        ui_leds();
        ui_draw();
        {   /* nothing sounding, no key down, no loop going: the sessions' flash may be blanked a sector */
            uint32_t l, quiet = !ui.keys && !ui.save_down;
            for (l = 0; l < NLAYER && quiet; l++)
                quiet = !layer_sounding(l) && !loop[l].play && loop[l].rec != LR_REC;
            ses_idle((int)quiet);
        }
        fm1core_tn_frames++;
        while ((uint32_t)(fm1_ms - m) < 15u) {   /* ~60 frames a second at most */
            fm1coro_yield();              /* (the next device ms) */
            ui_input();
        }
    }
}

void fm1core_tn_fw_init(int demo)
{
    uint32_t mode;
    (void)demo;
    fm1t_set_ms(0);
    fm1_ms = 0;
    fm1core_exit_park = fm1coro_park;     /* (an exit() on the loop's coroutine: it is never resumed) */
    /* main.c fm1_cstart: the guard (a power-on clears it: none staged by boot_options) */
    if (!fm1core_tn_guard_set)
        bootguard_clear(&bootguard);
    mode = bootguard_begin(&bootguard);
    if (mode != BOOT_NORMAL) {
        printf("%s: boot guard: %s\n", FM1CORE_ID,
               mode == BOOT_ROM ? "ROM (UBOOT)" : "two failed boots in a row: the rescue mode (no USB updater here)");
        exit(3);
        return;
    }
    fm1_power_ready = (uint8_t)fm1_power_init();
    /* main.c fm1_main's power-on */
    fm1nor_open();                        /* (common/nor.c: erased, then the stage or the file) */
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    if (flash_ok)
        ota_boot_cleanup();               /* a staging area left by an update */
    ses_scan(flash_ok);                   /* the sessions in flash */
    setup_load();                         /* GLOBAL SETUP's settings (MIDI) */
    lcd_init();
    splash();
    fm1_input_init();
    fm1_adc_init();
    layers_init();
    ui_init();
    sounds_init();                        /* a sound of each engine for every layer */
    if (fm1_core1_boot_allowed(fm1_power_ready, bootguard.failed))
        fm1_multicore_start();
    if (!audio_worker_online)
        say("1 CORE", 4000);
    audio_init();
    usb.up = 1;                           /* usb_start: a host is there, configured (MIDI out flows, the battery */
    usb.config = 1;                       /* corner shows USB power) */
    fm1fifo_init(&fm1core_tn_fifo, HALF_FRAMES, 2, FM1FIFO_S32, OUT_SHIFT, fm1core_tn_render, NULL);
    fm1core_tn_looping = 0;
    fm1core_tn_frames = 0;
    fm1core_tn_co = fm1coro_new(fm1core_tn_main, NULL, 0);
    if (!fm1core_tn_co) {
        printf("%s: no memory for the main loop's stack\n", FM1CORE_ID);
        exit(1);
        return;
    }
    fm1coro_resume(fm1core_tn_co);        /* (up to its first wait: the splash's 700 ms) */
}

/* the panel table: ui.c's PANEL_BTN / PANEL_ENC (fixed in the firmware; no direction table: every knob +1) */
void fm1core_tn_fw_panel_table(uint8_t *btn_id, uint8_t *enc_id, int8_t *enc_dir)
{
    uint32_t i;
    for (i = 0; i < NB; i++)
        btn_id[i] = PANEL_BTN[i];
    for (i = 0; i < NE; i++) {
        enc_id[i] = PANEL_ENC[i];
        enc_dir[i] = 1;
    }
}

/* ------------------------------------------------------- the 1 ms timer --- */
/* what TIMER5 (fm1_input_tick, the debounce) delivers to the main loop: a press is an edge at once, a release only
 * after the key was held FM1CORE_TN_RELEASE_MS (the debounce's time), a tap shorter than a tick still arrives */
void fm1core_tn_fw_tick(uint32_t ms, uint32_t buttons, uint32_t keys, const int32_t *enc)
{
    uint32_t b = buttons, k = keys, i, down;
    fm1t_set_ms(ms);
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    down = b & ~fm1core_tn_in_btn;
    for (i = 0; i < FM1P_NBTN; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_tn_btn_ms[i] = ms;
        else if ((fm1core_tn_in_btn & m) && !(b & m) && ms - fm1core_tn_btn_ms[i] < FM1CORE_TN_RELEASE_MS)
            b |= m;                       /* (held at least the debounce time) */
    }
    fm1_in.released |= fm1core_tn_in_btn & ~b;
    fm1core_tn_in_btn = b;
    fm1_in.pressed |= down;
    down = k & ~fm1core_tn_in_key;
    for (i = 0; i < FM1P_NKEY; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_tn_key_ms[i] = ms;
        else if ((fm1core_tn_in_key & m) && !(k & m) && ms - fm1core_tn_key_ms[i] < FM1CORE_TN_RELEASE_MS)
            k |= m;
    }
    fm1core_tn_in_key = k;
    fm1_in.notes_pressed |= down;
    fm1_in.buttons = fm1core_tn_in_btn;
    fm1_in.notes = fm1core_tn_in_key;
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_steps[i] = (int16_t)(fm1_in.enc_steps[i] + enc[i]);
    fm1_in.frames++;
}

/* ------------------------------------------------------- the main loop --- */
void fm1core_tn_fw_run(void)              /* frame / idle: this device ms of the main loop */
{
    if (fm1core_tn_co && !fm1core_halted() && !fm1coro_done(fm1core_tn_co))
        fm1coro_resume(fm1core_tn_co);
}
void fm1core_tn_fw_free(void)             /* (shutdown: the loop's stack) */
{
    if (fm1core_tn_co)
        fm1coro_free(fm1core_tn_co);
    fm1core_tn_co = NULL;
}

void fm1core_tn_fw_master(int32_t m) { fm1core_tn_master = m < 0 ? 0 : m > 1023 ? 1023 : m; }

/* ---------------------------------------------------------------- audio --- */
void fm1core_tn_fw_audio(int16_t *out, uint32_t frames)
{
    uint32_t f;
    for (f = 0; f + FM1P_BLOCK <= frames; f += FM1P_BLOCK) {
        if (fm1nor_stall_take() || !fm1core_tn_audio_on) {   /* a flash erase: IRQs off, the buffer silent */
            memset(out + 2u * f, 0, FM1P_BLOCK * 2u * sizeof *out);
            continue;
        }
        fm1fifo_pull(&fm1core_tn_fifo, out + 2u * f, FM1P_BLOCK);
    }
}

/* ----------------------------------------------------------------- MIDI --- */
/* usb.c's EP1 OUT (ep1_take, midi_in_event): refused (0, "retry") while the ring is short of room (8 slots kept for
 * the TRS jack, as ep1_take), or while a SysEx frame waits for the main loop and this packet starts another (usb.c
 * would drop it: sysex_byte collects only into a free frame; here the host retries it after its next render, so
 * nothing is lost) */
int fm1core_tn_fw_midi_in(uint32_t pkt)
{
    const uint32_t cin = pkt & 15u;
    if (MQ - (mi_w - mi_r) < EP1_ROOM - 16u)
        return 0;
    if (cin >= 4u && cin <= 7u && ((pkt >> 8) & 0xFFu) == 0xF0u && sx_ready)
        return 0;
    midi_in_event(pkt);
    return 1;
}
/* usb.c's EP1 IN (ep1_tx): the SysEx replies first, never split by notes; no notes while a reply is being queued */
int fm1core_tn_fw_midi_out_take(uint32_t *pkt)
{
    if (so_r != so_w) {
        *pkt = sx_out_q[so_r % SXQ];
        RING_PUBLISH();
        so_r++;
        return 1;
    }
    if (sx_busy || mo_r == mo_w)
        return 0;
    *pkt = midi_out_q[mo_r % MQ];
    RING_PUBLISH();
    mo_r++;
    return 1;
}

/* ------------------------------------------------------------- the flash --- */
/* the image is current but for GLOBAL SETUP's changes while its page is up (setup.c writes them as it is left) */
int fm1core_tn_fw_flash_sync(void) { return !(ui.set_page && set.dirty); }

/* ------------------------------------------------------------ diagnostics --- */
void fm1core_tn_fw_dump(void)
{
    uint32_t c, l;
    printf("fm1_in: notes %07X buttons %04X  fm1_ms %u  loop frames %u%s\n", (unsigned)fm1_in.notes,
           (unsigned)fm1_in.buttons, (unsigned)fm1_ms, (unsigned)fm1core_tn_frames,
           fm1core_tn_looping ? "" : " (splash)");
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X", fm1_led[c], fm1_led_dim[c]);
    printf("  (lit/glow per column)\n");
    for (l = 0; l < NLAYER; l++)
        printf("  track %u%s: engine %u, level %d, pan %d, send %d, %s%s\n", (unsigned)l + 1u, l == ui.sel ? "*" : "",
               (unsigned)lay[l].engine, (int)ui.level[l], (int)ui.pan[l], (int)ui.send[l],
               layer_sounding(l) ? "sounding" : "quiet", loop[l].play ? ", looping" : "");
    printf("  master_q12 %d  cpu1 %s, %u jobs  midi in %u out %u  sysex out %u%s\n", (int)master_q12,
           audio_worker_online ? "online (inline)" : "off", (unsigned)fm1core_tn_aw.jobs, (unsigned)(mi_w - mi_r),
           (unsigned)(mo_w - mo_r), (unsigned)(so_w - so_r), sx_ready ? ", a frame waiting" : "");
    printf("  flash: %s, %u writes, %u erases with the audio stalled (%u blocks); sessions %s\n",
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty(), (unsigned)fm1nor_stalls(),
           (unsigned)fm1nor_stall_blocks(), ses.ok ? "ok" : "none");
    printf("  the main loop's coroutine: %u resumes, its stack's peak %u of %u bytes\n",
           (unsigned)fm1coro_resumes(fm1core_tn_co), (unsigned)fm1coro_stack_peak(fm1core_tn_co),
           (unsigned)FM1CORO_STACK);
}

void fm1core_tn_fw_ui_info(char *buf, uint32_t n)
{
    const char *page = !fm1core_tn_looping ? "splash" : ui.set_page ? "GLOBAL SETUP" : ui.ses_page ? "SESSIONS"
                     : ui.pre_page ? "PRESETS" : ui.mod_page ? "MOD" : ui.rv_page ? (ui.fx ? "DELAY" : "REVERB")
                     : ui.env_page ? "ENVELOPE" : ui.bb_page ? "BALLS" : ui.bl_page ? "BRICKLAYER" : "track";
    snprintf(buf, n, "%s, track %u, engine %u", page, (unsigned)ui.sel + 1u, (unsigned)lay[ui.sel].engine);
}

void fm1core_tn_fw_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = dbg.cpu_q8 * 100u / 256u;
}
