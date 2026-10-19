/* SPDX-License-Identifier: GPL-3.0-only */
/* The plat family's shared core (core-glue/plat/README.md): the ABI's entry points for a firmware built on Charles
 * Vestal's plat.h over Felucca's HAL half (plat_hal.h). The firmware's device main loop (app/main_fm1.c fm1_main, not
 * buildable on the host) is redone here as one pass per device millisecond: MASTER through its IIR, the OCT- + OCT+
 * UBOOT hold, the firmware's own extras, then ui_frame every 16 ms (else ui_input_only).
 *
 * Audio, rendered by the device clock: the firmware's 256-frame stereo int32 (Q23) blocks are rendered in tick(ms),
 * before that ms's UI, each when its first frame falls due, exactly as the firmware's own host simulator and browser
 * emulator run it (x0x_host.c run_ms: "while (audio_due_ms <= now_ms) render"), so the UI reads the engine's state
 * (a string's level, the sequencer's step) as they do. audio() takes the frames from that queue (int16, Q23 >> 8,
 * saturated); a block the queue lacks is rendered on demand. (core-glue/common/fifo.c renders when audio() pulls,
 * 2.9 ms later: the screens would differ from the simulator's.)
 *
 * Included by the firmware unit after the firmware's sources and the HAL half, before exit_end.h (the UBOOT exits
 * below are the redirected exit()). The unit defines first (all static; the names are the glue's):
 *   void fm1core_pl_flash_boot(void)     what the device does with the flash at power-on before the UI (X0X: the
 *                                        user sample slots scanned), after storage is up
 *   void fm1core_pl_app_boot(void)       the app's power-on (engine, the saved project or defaults, applied), after
 *                                        panel_init and led_pos_init
 *   void fm1core_pl_ui_start(void)       the screen cleared and ui_init
 *   void fm1core_pl_render(int32_t *lr, uint32_t n)   one block: n stereo frames, 24-bit (Q23) in int32
 *   void fm1core_pl_master(int32_t knob) MASTER's IIR state (ADC * 16) to the firmware's MASTER (plat_master)
 *   void fm1core_pl_loop(void)           the firmware's own per-pass extras (X0X: the break loops), may be empty
 *   void fm1core_pl_uboot_say(uint32_t left)   the countdown on the screen: left 3..1, 0 = cancelled
 *   int  fm1core_pl_sync(void)           flash_sync: the lazy save now (1 current, 0 deferred by the firmware)
 *   void fm1core_pl_info(char *buf, uint32_t n)   ui_info: the screen showing
 *   FM1CORE_PL_UI_FRAME() / FM1CORE_PL_UI_INPUT()   ui_frame() / ui_input_only() (fm1-base: app_frame / ..)
 *   FM1CORE_PL_DIM_LEVEL                 the dim plane's level (FM1P_LED_DIM; fm1-base: its DIM LO / HI)
 *   FM1CORE_PL_DIM_ON (optional)         whether the dim plane shows (default: fm1_led_dim_div != 0; FiMba's HAL
 *                                        has no divider: always)
 *   FM1CORE_PL_NAME                      for the log lines ("X0X")
 * Optional: FM1CORE_PL_SX (1: the managers' SysEx, plat_sx.h included before the firmware's sources: see
 * there), FM1CORE_PL_TICK_HOOK() (each device ms, after the input: fm1-base's app_tick), FM1CORE_PL_KEY_HOOK(down,
 * up, held) (the note keys that went down / up this ms, and those held now: fm1-base's velocity and pressure), FM1CORE_PL_DEFAULT_MASTER (the pot's position
 * when the host gives none, 0..1023: the firmware's own default MASTER), FM1CORE_PL_PALETTE (the palette set at
 * power-on, default 1).
 *
 * Simulated time only (fm1core.h): nothing here reads the host's clock. */
#pragma once

#ifndef FM1CORE_PL_SX
#define FM1CORE_PL_SX 0
#endif
#if !FM1CORE_PL_SX
static const uint8_t fm1core_pl_sx_held = 0;     /* (no SysEx service: plat_sx.h not included) */
#endif
/* the SysEx service's coroutine (FM1CORE_PL_SX_CORO): one pass of the service each time it is resumed */
#if FM1CORE_PL_SX && FM1CORE_PL_SX_CORO
static fm1coro_t *fm1core_pl_sx_co;
static void fm1core_pl_sx_main(void *arg)
{
    (void)arg;
    for (;;) {
        fm1coro_yield();                         /* (started at init: waits here for the first pass with work) */
        fm1core_pl_sx_held = 1;
        FM1CORE_PL_SX_SERVICE();
        fm1core_pl_sx_held = 0;
    }
}
#endif
#ifndef FM1CORE_PL_DEFAULT_MASTER
#define FM1CORE_PL_DEFAULT_MASTER 724     /* -> master_q12 = 724^2 >> 8 = 2047: plat_fm1.c's 2048 */
#endif
#define FM1CORE_PL_RELEASE_MS 9u          /* Felucca's fm1_input.h: a release needs ~9 ms open (FM1_DEB_RELEASE) */
#define FM1CORE_PL_FRAME_MS 16u           /* main_fm1.c: ui_frame at most every 16 ms, ui_input_only between */
#define FM1CORE_PL_BLOCK 256u             /* the render's block (plat: REND_FRAMES; fm1-base: APP_BLOCK) */
#ifndef FM1CORE_PL_PALETTE
#define FM1CORE_PL_PALETTE 1u             /* fm1_main's palette_set before the splash (fm1-base: APP_PALETTE) */
#endif
#ifndef FM1CORE_PL_DIM_ON
#define FM1CORE_PL_DIM_ON (fm1_led_dim_div != 0)   /* the dim plane shows (X0X's HAL: one frame in div; 0 off) */
#endif

fm1panel_t fm1core_pl_panel;              /* the ABI's panel (the descriptor's) */
#define FM1CORE_PL_P (&fm1core_pl_panel)

/* the host's input as of the last entry point: held state, taps and detents not yet given to the HAL */
static struct {
    uint32_t keys, buttons, keys_tap, buttons_tap;
    int32_t enc[FM1P_NENC];
    int32_t master;
} fm1core_pl_in_st;
static uint32_t fm1core_pl_btn, fm1core_pl_key;   /* the debounced state the tick delivers */
static uint32_t fm1core_pl_btn_ms[32], fm1core_pl_key_ms[32];
static int32_t fm1core_pl_knob = 512 * 16;       /* main_fm1.c fm1_main: knob (MASTER's IIR) */
static uint32_t fm1core_pl_last_frame;
static uint8_t fm1core_pl_framed;                /* the first frame was drawn */
static uint8_t fm1core_pl_up;                    /* init ran (the clock may run) */
static uint32_t fm1core_pl_uboot_t0, fm1core_pl_uboot_shown;
/* the render queue: int16 stereo frames rendered ahead by the device clock, read by audio() */
#define FM1CORE_PL_QN 4096u                       /* frames (a power of two; ~2 blocks are ever queued) */
static int16_t fm1core_pl_q[FM1CORE_PL_QN * 2u];
static uint32_t fm1core_pl_qw, fm1core_pl_qr;    /* frames written / read since power-on */
static uint32_t fm1core_pl_blocks;               /* blocks rendered since power-on */
static int32_t fm1core_pl_blk[FM1CORE_PL_BLOCK * 2u];

static void fm1core_pl_render_block(void)
{
    uint32_t i;
    fm1core_pl_render(fm1core_pl_blk, FM1CORE_PL_BLOCK);
    for (i = 0; i < 2u * FM1CORE_PL_BLOCK; i++) {
        const int32_t v = fm1core_pl_blk[i] >> 8;    /* 24-bit (Q23) -> Q15 = 0 dBFS */
        fm1core_pl_q[(2u * fm1core_pl_qw + i) % (2u * FM1CORE_PL_QN)] =
            (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    fm1core_pl_qw += FM1CORE_PL_BLOCK;
    fm1core_pl_blocks++;
}
/* the blocks due by the end of device ms `ms` (the simulator's now_ms = ms + 1): block k when k * 256 frames <=
 * (ms + 1) * 44.1 */
static void fm1core_pl_render_due(uint32_t ms)
{
    while ((uint64_t)fm1core_pl_blocks * FM1CORE_PL_BLOCK * 1000u <= (uint64_t)(ms + 1u) * FM1P_FS) {
        if (fm1core_pl_qw - fm1core_pl_qr > FM1CORE_PL_QN - FM1CORE_PL_BLOCK)
            fm1core_pl_qr = fm1core_pl_qw - (FM1CORE_PL_QN - FM1CORE_PL_BLOCK);   /* (nobody reads: the oldest go) */
        fm1core_pl_render_block();
    }
}

static void fm1core_pl_in(void)                  /* the panel's input, at the top of tick / frame / idle */
{
    fm1panel_t *p = FM1CORE_PL_P;
    uint32_t i;
    fm1core_pl_in_st.keys = p->keys;
    fm1core_pl_in_st.buttons = p->buttons;
    fm1core_pl_in_st.keys_tap |= fm1p_take_key_taps(p);
    fm1core_pl_in_st.buttons_tap |= fm1p_take_button_taps(p);
    for (i = 0; i < FM1P_NENC; i++)
        fm1core_pl_in_st.enc[i] += fm1p_take_enc(p, i);
    fm1core_pl_in_st.master = p->master;
}

static void fm1core_pl_out(void)                 /* the LEDs and the LCD, after init / frame / idle */
{
    const uint8_t lv[2] = {FM1P_LED_LIT, (uint8_t)(FM1CORE_PL_DIM_LEVEL)};
    const uint8_t *const pl[2] = {fm1_led, fm1_led_dim};
    fm1p_leds_planes(FM1CORE_PL_P, pl, lv, (FM1CORE_PL_DIM_ON) ? 2 : 1, FM1P_PLAY_GREEN_POS);
    fm1p_lcd_out(FM1CORE_PL_P, fm1core_pl_lcd, fm1core_pl_lcd_writes);
}

/* ---- the boot guard (main_fm1.c fm1_cstart): a run that died before its 30 s mark leaves bootguard.pending set;
 * the next reset counts it. Two failed boots: the device's SAFE MODE (not emulated: the core boots normally);
 * four: the chip's update mode (UBOOT: halted). fail: the failed boots counted before this reset (< 0: clean) */
static struct {
    uint32_t failed, pending;
} fm1core_pl_guard;
static void fm1core_pl_uboot(const char *why)   /* main_fm1.c enter_uboot: the screen, then the chip's UBOOT */
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 110, 240, &FONT_S, why, C_WHITE, 1);
    fm1core_pl_guard.pending = 0;                /* intentional reset: not a failed boot */
    printf("%s: %s\n", FM1CORE_PL_NAME, why);
    exit(3);
}

/* ---- the entry points (core_plat_desc.h declares them) ---- */
static uint8_t fm1core_pl_save_on_exit;
void fm1core_pl_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    fm1core_exit_cleanup = fm1nor_close;         /* (exit: the flash file closed, later writes RAM only) */
    fm1core_pl_save_on_exit = (uint8_t)(save_on_exit != 0);
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}

int fm1core_pl_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;                                /* (no reset reasons on this platform) */
    (void)stage;
    if (fail >= 0) {
        fm1core_pl_guard.failed = (uint32_t)fail;
        fm1core_pl_guard.pending = 1;
    } else {
        fm1core_pl_guard.failed = fm1core_pl_guard.pending = 0;
    }
    return 1;
}

static void fm1core_pl_exit(void)                /* at shutdown: save_on_exit, the lazy save written */
{
    if (fm1core_pl_save_on_exit && fm1core_pl_up && !fm1core_pl_sx_held)
        fm1core_pl_sync();
}

void fm1core_pl_init(int demo)
{
    fm1panel_t *p = FM1CORE_PL_P;
    uint32_t i;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
#if FM1CORE_PL_SX
    fm1sx_in_init(&fm1core_pl_sxi, FM1CORE_PL_SX_ACCEPT);
    fm1sx_out_reset(&fm1core_pl_sxo);
#if FM1CORE_PL_SX_CORO
    if (fm1core_pl_sx_co && fm1core_pl_sx_held) {   /* (powered on again while a service waited: a fresh one) */
        fm1coro_free(fm1core_pl_sx_co);
        fm1core_pl_sx_co = NULL;
    }
    fm1core_pl_sx_held = 0;
    if (!fm1core_pl_sx_co)
        fm1core_pl_sx_co = fm1coro_new(fm1core_pl_sx_main, NULL, 0);
    if (!fm1core_pl_sx_co) {
        printf("%s: no memory for the SysEx service's stack: halted\n", FM1CORE_PL_NAME);
        exit(1);
        return;
    }
    fm1coro_resume(fm1core_pl_sx_co);           /* (its stack set up now, not on the first request) */
#endif
#endif
    fm1core_pl_in();                             /* (ready = 2: simulated time, always here) */
    /* fm1_cstart: the guard */
    if (fm1core_pl_guard.pending)
        fm1core_pl_guard.failed++;
    fm1core_pl_guard.pending = 1;
    if (fm1core_pl_guard.failed >= 4u) {
        fm1core_pl_guard.failed = fm1core_pl_guard.pending = 0;
        printf("%s: four failed boots in a row: UBOOT\n", FM1CORE_PL_NAME);
        exit(3);                                 /* (the device: fm1_enter_uboot) */
        return;
    }
    /* fm1_main's order: the flash, the palette, the panel table, the LED map, the app; the UI. (The splash and its
     * 330..430 ms of delays, and the HARDWARE CALIBRATION at power-on with OCT- + OCT+ held, are left out: the UI
     * starts at power-on, as in the firmware's own host simulator and browser emulator.) */
    fm1nor_open();
    flash_ok = 1;                                /* (JEDEC 0x856014: the FM-1's part) */
    fm1core_pl_flash_boot();
    palette_set(FM1CORE_PL_PALETTE);
    panel_init();
    led_pos_init();
    fm1core_pl_app_boot();
    fm1core_pl_qw = fm1core_pl_qr = fm1core_pl_blocks = 0;
    atexit(fm1core_pl_exit);
    fm1core_pl_ui_start();
    /* the panel table back to the host (label -> matrix button, role -> matrix encoder and its direction) */
    for (i = 0; i < FM1P_NB; i++)
        p->btn_id[i] = panel.btn[i];
    for (i = 0; i < FM1P_NE - 1u; i++) {
        p->enc_id[i] = panel.enc[i];
        p->enc_dir[i] = panel.dir[i] < 0 ? -1 : 1;
    }
    if (!p->master)
        p->master = FM1CORE_PL_DEFAULT_MASTER;   /* the pot where the firmware's default puts it */
    fm1core_pl_in_st.master = p->master;
    fm1core_pl_knob = p->master * 16;
    fm1core_pl_master(fm1core_pl_knob);
    fm1core_pl_framed = 0;
    fm1core_pl_up = 1;
    p->ready = 1;
    fm1core_pl_out();
}

/* the 1 ms timer (fm1_timer5_irq / fm1_input_tick): a press is an edge at once, a release only after the key was
 * held FM1CORE_PL_RELEASE_MS (the debounce's time), a tap shorter than a tick still arrives */
void fm1core_pl_tick(uint32_t ms)
{
    uint32_t b, k, i, down, kdown;
    if (fm1core_halted() || !fm1core_pl_up)
        return;
    fm1core_pl_in();
    fm1t_set_ms(ms);
    fm1_ms = ms;
    b = fm1core_pl_in_st.buttons | fm1core_pl_in_st.buttons_tap;
    fm1core_pl_in_st.buttons_tap = 0;
    k = fm1core_pl_in_st.keys | fm1core_pl_in_st.keys_tap;
    fm1core_pl_in_st.keys_tap = 0;
    down = b & ~fm1core_pl_btn;
    for (i = 0; i < FM1P_NBTN; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_pl_btn_ms[i] = ms;
        else if ((fm1core_pl_btn & m) && !(b & m) && ms - fm1core_pl_btn_ms[i] < FM1CORE_PL_RELEASE_MS)
            b |= m;                              /* (held at least the debounce time) */
    }
    fm1core_pl_btn = b;
    fm1core_pl_pressed |= down;
    kdown = k & ~fm1core_pl_key;
    for (i = 0; i < FM1P_NKEY; i++) {
        const uint32_t m = 1u << i;
        if (kdown & m)
            fm1core_pl_key_ms[i] = ms;
        else if ((fm1core_pl_key & m) && !(k & m) && ms - fm1core_pl_key_ms[i] < FM1CORE_PL_RELEASE_MS)
            k |= m;
    }
#ifdef FM1CORE_PL_KEY_HOOK
    FM1CORE_PL_KEY_HOOK(kdown, fm1core_pl_key & ~k, k);
#endif
    fm1core_pl_key = k;
    fm1_in.buttons = fm1core_pl_btn;
    fm1_in.notes = fm1core_pl_key;
    for (i = 0; i < FM1P_NENC; i++) {
        fm1core_pl_enc_acc[i] += fm1core_pl_in_st.enc[i];
        fm1core_pl_in_st.enc[i] = 0;
    }
#ifdef FM1CORE_PL_TICK_HOOK
    FM1CORE_PL_TICK_HOOK();
#endif
    fm1core_pl_render_due(ms);                   /* the audio "interrupt": before this ms's UI */
}

/* ---- the managers' SysEx (FM1CORE_PL_SX, plat_sx.h): the service at its place in main_fm1.c's loop, after the
 * UBOOT hold and before the app's extras and the frame. With FM1CORE_PL_SX_CORO it runs on a coroutine: a service
 * that waits (X0X's restore) holds the main loop as on the device: the passes after it only resume the service, no
 * frame is drawn and nothing else of the loop runs until it returns; the tick (the input, the audio) goes on */
static int fm1core_pl_sx_run(void)              /* 1: the main loop stays in the service (this pass ends here) */
{
#if FM1CORE_PL_SX && FM1CORE_PL_SX_CORO
    if (!fm1core_pl_sx_held && !(FM1CORE_PL_SX_WORK()))
        return 0;
    if (fm1coro_resume(fm1core_pl_sx_co) < 0)
        return 0;
    return fm1core_pl_sx_held;
#elif FM1CORE_PL_SX
    FM1CORE_PL_SX_SERVICE();
    return 0;
#else
    return 0;
#endif
}

/* one pass of main_fm1.c's loop */
static void fm1core_pl_pass(void)
{
    if (fm1core_pl_sx_held) {                    /* (inside the service since an earlier pass: only it runs) */
        if (fm1core_pl_sx_run())
            return;
        goto served;
    }
    {   /* the 30 s mark: this boot did not fail */
        if (fm1_ms > 30000u && fm1core_pl_guard.pending)
            fm1core_pl_guard.pending = fm1core_pl_guard.failed = 0;
    }
    {   /* MASTER: the ADC (0..1023) through the IIR */
        int32_t a = fm1core_pl_in_st.master;
        a = a < 0 ? 0 : a > 1023 ? 1023 : a;
        fm1core_pl_knob += (a * 16 - fm1core_pl_knob) / 8;
        fm1core_pl_master(fm1core_pl_knob);
    }
    {   /* OCT- + OCT+ held 5 s: update mode; the countdown from 2 s, letting go cancels it */
        const uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
        if ((fm1_in.buttons & both) != both) {
            if (fm1core_pl_uboot_shown)
                fm1core_pl_uboot_say(0);
            fm1core_pl_uboot_shown = 0;
            fm1core_pl_uboot_t0 = fm1_ms;
        } else if (fm1_ms - fm1core_pl_uboot_t0 > 2000u && fm1_ms - fm1core_pl_uboot_t0 <= 5000u) {
            const uint32_t left = (5000u - (fm1_ms - fm1core_pl_uboot_t0) + 999u) / 1000u;
            if (left != fm1core_pl_uboot_shown) {
                fm1core_pl_uboot_say(left);
                fm1core_pl_uboot_shown = left;
            }
        } else if (fm1_ms - fm1core_pl_uboot_t0 > 5000u) {
            fm1core_pl_uboot("UBOOT");
            return;
        }
    }
    if (fm1core_pl_sx_run())
        return;
served:
    fm1core_pl_loop();
    if (!fm1core_pl_framed || fm1_ms - fm1core_pl_last_frame >= FM1CORE_PL_FRAME_MS) {
        fm1core_pl_last_frame = fm1_ms;
        fm1core_pl_framed = 1;
        FM1CORE_PL_UI_FRAME();
    } else {
        FM1CORE_PL_UI_INPUT();
    }
}

void fm1core_pl_frame(void)
{
    if (fm1core_halted() || !fm1core_pl_up)
        return;
    fm1core_pl_in();
    fm1core_pl_pass();
    if (!fm1core_halted())
        fm1core_pl_out();
}
void fm1core_pl_idle(void) { fm1core_pl_frame(); }   /* (the same pass: the loop decides frame or input) */

void fm1core_pl_audio(int16_t *stereo, uint32_t frames)
{
    uint32_t f;
    if (fm1core_halted() || !fm1core_pl_up) {
        memset(stereo, 0, (size_t)frames * 2u * sizeof *stereo);
        return;
    }
    for (f = 0; f < frames; f += FM1P_BLOCK) {
        const uint32_t n = frames - f < FM1P_BLOCK ? frames - f : FM1P_BLOCK;
        const int stall = fm1nor_stall_take();   /* a flash erase: IRQs off, the DMA buffer silenced */
        uint32_t i;
        while (fm1core_pl_qw - fm1core_pl_qr < n)
            fm1core_pl_render_block();           /* (the clock did not render it: on demand) */
        for (i = 0; i < n; i++, fm1core_pl_qr++) {
            const uint32_t j = 2u * (fm1core_pl_qr % FM1CORE_PL_QN);
            stereo[2u * (f + i)] = stall ? 0 : fm1core_pl_q[j];
            stereo[2u * (f + i) + 1u] = stall ? 0 : fm1core_pl_q[j + 1u];
        }
    }
}

int fm1core_pl_midi_in(uint32_t pkt)            /* usb.c: into midi_in_q (the render takes them: plat_midi_in) */
{
    if (fm1core_halted())
        return 1;                                /* (halted: taken and dropped, never "retry") */
#if FM1CORE_PL_SX
    {   /* usb.c ep1_rx: SysEx to the frame collector only (sysex_byte), never into the app's queue */
        const uint32_t cin = pkt & 15u;
        if (fm1sx_in_would_refuse(&fm1core_pl_sxi, pkt))
            return 0;                            /* (a request waits for the main loop: this one after it) */
        if (cin >= 4u && cin <= 7u)
            return fm1sx_in_pkt(&fm1core_pl_sxi, pkt);
    }
#endif
    if (MQ - (mi_w - mi_r) < 8u)                 /* (room kept for the TRS jack, as usb.c's ep1_take) */
        return 0;
    midi_in_q[mi_w % MQ] = pkt;
    RING_PUBLISH();
    mi_w++;
    return 1;
}
int fm1core_pl_midi_out_take(uint32_t *pkt)
{
    if (fm1core_halted())
        return 0;
#if FM1CORE_PL_SX
    if (fm1sx_take(&fm1core_pl_sxo, pkt))
        return 1;                                /* (usb.c ep1_tx: SysEx first, never split by notes) */
#endif
    if (fm1core_pl_mo_r == fm1core_pl_mo_w)
        return 0;
    *pkt = fm1core_pl_mo_q[fm1core_pl_mo_r % MQ];
    fm1core_pl_mo_r++;
    return 1;
}

void fm1core_pl_dump(void)
{
    uint32_t c;
    printf("%s: ms %u buttons %04X notes %07X master %d  midi in %u out %u\n", FM1CORE_PL_NAME, (unsigned)fm1_ms,
           (unsigned)fm1_in.buttons, (unsigned)fm1_in.notes, (int)fm1core_pl_in_st.master, (unsigned)(mi_w - mi_r),
           (unsigned)(fm1core_pl_mo_w - fm1core_pl_mo_r));
    printf("  leds:");
    for (c = 0; c < FM1P_NCOL; c++)
        printf(" %02X/%02X", fm1_led[c], fm1_led_dim[c]);
    printf("  (lit/dim per column; dim duty 1/%u)\n", (unsigned)fm1_led_dim_div);
    printf("  flash: %s, %u writes, %u erases with the audio stalled (%u blocks)\n",
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty(), (unsigned)fm1nor_stalls(),
           (unsigned)fm1nor_stall_blocks());
}
void fm1core_pl_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;                                   /* (simulated time: no render load is measured) */
    *cpu_pct = 0;
}
void fm1core_pl_ui_info(char *buf, uint32_t n)
{
    if (!buf || !n)
        return;
    if (fm1core_halted())
        snprintf(buf, n, "halted 0x%X", (unsigned)fm1core_halted());
    else
        fm1core_pl_info(buf, n);
}
int fm1core_pl_halted(void) { return fm1core_halted(); }
void fm1core_pl_shutdown(void) { fm1core_run_exit(); }
const int8_t *fm1core_pl_keymap(void) { return &FM1_KEYMAP[0][0]; }
uint8_t *fm1core_pl_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_pl_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_pl_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_pl_flash_sync(void)
{
    if (fm1core_halted() || !fm1core_pl_up)
        return 1;
    if (fm1core_pl_sx_held)
        return 0;                                /* (a restore owns the store: the device saves nothing meanwhile) */
    return fm1core_pl_sync();
}

#if FM1CORE_PL_SX && FM1CORE_PL_SX_CORO
/* the module's unload: the service's stack */
__attribute__((destructor)) static void fm1core_pl_sx_fini(void)
{
    if (fm1core_pl_sx_co && !fm1coro_inside()) {
        fm1coro_free(fm1core_pl_sx_co);
        fm1core_pl_sx_co = NULL;
    }
}
#endif
