/* SPDX-License-Identifier: GPL-3.0-only */
/* The Dinghy family glue, part 2 of 2: included by each Dinghy core's unit AFTER the firmware's sources and the
 * unit's own hooks (below). It is the core: the descriptor's entry points (fm1core_dg_*, declared in dinghy_desc.h),
 * the same for every Dinghy app; each unit is its own module, so the names never meet.
 *
 *   the main loop   app_main() on a coroutine (common/coro.h): init runs the boot up to its first poll, each tick(ms)
 *                   one pass of the loop (the shim's platform_poll / emscripten_sleep yield: fm1core_dg_poll /
 *                   fm1core_dg_sleep). exit() inside parks it for good (common/exit.h fm1core_exit_park).
 *   the clock       fm1_ms (the shim's) = the ABI's device ms, set in tick before the pass; common/ticks.h as well
 *   the panel       Dinghy's button ids and encoder ids are the FM-1's matrix ids (platform.c BTN_* OCT- OCT+ FX ..
 *                   REC = FM1P_KEYMAP ids 0..13; ENC_* SELECT ALGO K1..K4 PRESETS = matrix encoders 0..6), so the
 *                   panel table is the factory one (fm1p_panel_default); the input is read once per pass, at the top
 *                   of tick (the app reads its input only in the pass: a tap's release comes on the next pass, as the
 *                   shims' own "btn" taps)
 *   the LEDs        per key id (fm1_led_key(id, on): 0..13 buttons, 14 + k keys), the level from the unit's hook,
 *                   placed through the keymap (fm1p_led_id)
 *   the LCD         the shim's fb[] (native RGB565) byte-swapped into the panel (fm1p_lcd_out_native) when it changed
 *   MASTER          the device's: the pot through platform_poll's IIR, master_q12 = (k10 * k10) >> 8, applied with
 *                   the ISR's clip after app_audio (the shims leave it at unity); 724 at power-on = master_q12 2047,
 *                   platform.c's 2048
 *   the audio       app_audio's 256-frame blocks served as 2 x 128 (common/fifo.h); an erase's stall is silence
 *   the flash       common/nor.c's image (A); each unit copies its app's RAM stores into it in the device's own
 *                   sector format at the device's offsets (and back before the boot), so a unit's dump boots here
 *
 * The unit defines, before including this file:
 *   static void fm1core_dg_hal_in(uint32_t notes, uint32_t held, uint32_t down, uint32_t up, const int32_t *enc7);
 *   static uint8_t fm1core_dg_led_level(uint32_t id);          LED id 0..40 -> 0..255 (lit, its glow layer, ..)
 *   static int fm1core_dg_midi_put(uint8_t st, uint8_t d1, uint8_t d2);   into the app's MIDI in queue; 0: full
 *   static void fm1core_dg_stores_load(void);                  the image -> the app's RAM stores (before the boot)
 *   static void fm1core_dg_stores_poll(void);                  the RAM stores -> the image (after each pass)
 *   static int fm1core_dg_sync(void);                          flash_sync: the app's deferred saves now; 1 current
 *   static void fm1core_dg_info(char *buf, uint32_t n);        ui_info: the page, the program
 * and may define FM1CORE_DG_LED_BG(col) (a packed row-bit backlight plane, FM1P_LED_FAINT) and
 * FM1CORE_DG_SYSEX_IN(p, n) (a received SysEx message, the 7-bit bytes between F0 and F7, handled before the next
 * pass as the device's platform_poll does), and FM1CORE_DG_OTA_C with FELUCCA_ID (the firmware's ota.c and its
 * package identity: the M-UPGRADE handshake, see "the managers' SysEx" below).
 *
 * THE MANAGERS' SYSEX (research/managers-2026-10-19/MANAGERS.md). The device's usb.c keeps every SysEx frame for
 * platform_poll, which hands FM-106's Roland frames (41 ..) to its librarian handler and the rest to ota.c's
 * ota_service (the M-UPGRADE identity, "who are you?"; the installer's update itself starts on the separate key
 * F0 22 24 35 7F F7, and F0 22 24 35 7D F7 is UBOOT: usb.c only sets flags for them). Here midi_in collects frames
 * (up to FM1CORE_DG_SX_N waiting: a new frame's F0 is refused while they are full, midi_in 0 = retry, so a burst of
 * requests is never dropped), each tick hands them to FM1CORE_DG_SYSEX_IN before the pass, and replies leave through
 * the MIDI out queue as USB-MIDI SysEx packets. fm1core_dg_ota_frame(p, n) gives one frame to ota_service; ota.c's
 * update steps are never reached (nothing acts on the update key or UBOOT's: the core runs on) and its flash hooks
 * refuse. */
#pragma once

fm1panel_t fm1core_dg_panel;
#define FM1CORE_DG_PANEL (&fm1core_dg_panel)

static const int8_t fm1core_dg_keymap[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;
static fm1coro_t *fm1core_dg_co;
static fm1fifo_t fm1core_dg_fifo;
static fm1p_edges_t fm1core_dg_bedge;
static int32_t fm1core_dg_knob = 724 * 16, fm1core_dg_q12 = 2048;
static uint16_t fm1core_dg_fb_seen[FM1P_LCD_W * FM1P_LCD_H];
static uint8_t fm1core_dg_booted;

#ifdef FM1CORE_DG_SYSEX_IN
/* SysEx in: USB-MIDI packets (CIN 4..7) collected into whole messages, handled at the next tick (a frame's F0 while
 * FM1CORE_DG_SX_N wait: refused, midi_in 0, the host offers it again) */
#define FM1CORE_DG_SX_MAX 640u
#define FM1CORE_DG_SX_N 4u
static uint8_t fm1core_dg_sx[FM1CORE_DG_SX_N][FM1CORE_DG_SX_MAX], fm1core_dg_sx_cur[FM1CORE_DG_SX_MAX];
static uint32_t fm1core_dg_sx_len[FM1CORE_DG_SX_N], fm1core_dg_sx_r, fm1core_dg_sx_w, fm1core_dg_sx_n;
static uint8_t fm1core_dg_sx_on;
static void fm1core_dg_sx_byte(uint8_t b)
{
    if (b == 0xF0u) {
        fm1core_dg_sx_on = 1;
        fm1core_dg_sx_n = 0;
    } else if (b == 0xF7u) {
        if (fm1core_dg_sx_on && fm1core_dg_sx_w - fm1core_dg_sx_r < FM1CORE_DG_SX_N) {
            const uint32_t i = fm1core_dg_sx_w++ % FM1CORE_DG_SX_N;
            memcpy(fm1core_dg_sx[i], fm1core_dg_sx_cur, fm1core_dg_sx_n);
            fm1core_dg_sx_len[i] = fm1core_dg_sx_n;
        }
        fm1core_dg_sx_on = 0;
    } else if (b >= 0x80u) {
        fm1core_dg_sx_on = 0;             /* (a status byte cuts it off) */
    } else if (fm1core_dg_sx_on) {
        if (fm1core_dg_sx_n < FM1CORE_DG_SX_MAX)
            fm1core_dg_sx_cur[fm1core_dg_sx_n++] = b;
        else
            fm1core_dg_sx_on = 0;         /* (too long: dropped) */
    }
}
static int fm1core_dg_sx_full(uint32_t pkt)   /* a new frame's F0 and no room for it */
{
    return ((pkt >> 8) & 0xFFu) == 0xF0u && fm1core_dg_sx_w - fm1core_dg_sx_r >= FM1CORE_DG_SX_N;
}
#endif

#ifdef FM1CORE_DG_OTA_C
/* ota.c's hooks on this transport: the frame being handled (fm1core_dg_ota_frame), the replies into the MIDI out
 * queue, the device's ms; the update's flash steps refused (never reached: nothing starts an update here) */
static const uint8_t *fm1core_dg_ota_p;
static uint32_t fm1core_dg_ota_n;
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    fm1core_dg_sysex_out(p, n);
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!fm1core_dg_ota_p)
        return 0;
    *p = fm1core_dg_ota_p;
    *n = fm1core_dg_ota_n;
    return 1;
}
static void ota_frame_done(void) { fm1core_dg_ota_p = NULL; }
static uint32_t ota_now_ms(void) { return fm1t_ms(); }
static void ota_idle(void) {}
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include FM1CORE_DG_OTA_C
static void fm1core_dg_ota_frame(const uint8_t *p, uint32_t n)   /* platform_poll: ota_service() on this frame */
{
    fm1core_dg_ota_p = p;
    fm1core_dg_ota_n = n;
    ota_service();
    fm1core_dg_ota_p = NULL;
}
#endif

static void fm1core_dg_main(void *arg)
{
    (void)arg;
    app_main();                            /* (never returns) */
}

/* the MASTER pot, once a pass: platform_poll's IIR and curve */
static void fm1core_dg_master(void)
{
    int32_t a = FM1CORE_DG_PANEL->master;
    uint32_t k10;
    a = a < 0 ? 0 : a > 1023 ? 1023 : a;
    fm1core_dg_knob += (a * 16 - fm1core_dg_knob) / 8;
    k10 = (uint32_t)(fm1core_dg_knob / 16);
    fm1core_dg_q12 = (int32_t)((k10 * k10) >> 8);
}

/* the panel's input into the shim (once a pass) */
static void fm1core_dg_in(void)
{
    fm1panel_t *p = FM1CORE_DG_PANEL;
    int32_t enc[FM1P_NENC];
    uint32_t down = 0, up = 0, i, keys, held;
    const uint32_t ktap = fm1p_take_key_taps(p), btap = fm1p_take_button_taps(p);
    held = p->buttons & 0x3FFFu;
    fm1p_edges(&fm1core_dg_bedge, held, btap & 0x3FFFu, &down, &up);
    keys = (p->keys | ktap) & ((1u << FM1P_NKEY) - 1u);
    for (i = 0; i < FM1P_NENC; i++)
        enc[i] = fm1p_take_enc(p, i);
    fm1core_dg_hal_in(keys, held, down, up, enc);
    fm1core_dg_master();
}

/* the LEDs and the LCD out (after init / frame / idle) */
static void fm1core_dg_out(void)
{
    fm1panel_t *p = FM1CORE_DG_PANEL;
    uint32_t id;
    for (id = 0; id < FM1P_NB + FM1P_NKEY; id++)
        fm1p_led_id(p, &fm1core_dg_keymap[0][0], (int)id, fm1core_dg_led_level(id));
#ifdef FM1CORE_DG_LED_BG
    {   /* the backlight plane: where no other level is set */
        int c, l;
        for (c = 0; c < FM1P_NCOL; c++)
            for (l = 0; l < FM1P_NLINE; l++)
                if (!p->led[c][l] && fm1core_dg_keymap[l + 1][c] >= 0 && (FM1CORE_DG_LED_BG(c) >> (l + 1) & 1u))
                    p->led[c][l] = FM1P_LED_FAINT;
    }
#endif
    p->led_play_green = 0;                 /* (Dinghy drives PLAY's red LED only: fm1_led_key(BTN_PLAY)) */
    if (memcmp(fm1core_dg_fb_seen, fb, sizeof fm1core_dg_fb_seen)) {
        memcpy(fm1core_dg_fb_seen, fb, sizeof fm1core_dg_fb_seen);
        fm1p_lcd_out_native(p, fb);
    }
}

/* the ISR: one 256-frame block, MASTER and the clip as platform.c's fm1_alnk0_irq */
static void fm1core_dg_render(void *ctx, void *buf, uint32_t frames)
{
    int32_t *o = (int32_t *)buf;
    uint32_t i;
    (void)ctx;
    app_audio(o, frames);
    for (i = 0; i < 2u * frames; i++) {
        const int32_t v = (int32_t)(((int64_t)o[i] * fm1core_dg_q12) >> 12);
        o[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}

static int fm1core_dg_running(void) { return fm1core_dg_booted && !fm1core_halted() && !fm1coro_done(fm1core_dg_co); }

/* one pass of the main loop */
static void fm1core_dg_pass(void)
{
    if (fm1coro_resume(fm1core_dg_co) <= 0 && !fm1core_halted()) {
        printf("fm1core %s: the main loop returned: halted\n", FM1CORE_ID);
        fm1core_exit(0);
    }
    if (!fm1core_halted())
        fm1core_dg_stores_poll();
}

/* ------------------------------------------------------------ the entry points --- */
int fm1core_dg_halted(void) { return fm1core_halted(); }
void fm1core_dg_shutdown(void)
{
    if (fm1core_dg_booted && !fm1core_halted())
        fm1core_dg_sync();
    if (!fm1core_halted())
        fm1core_dg_stores_poll();
    fm1core_run_exit();
}
void fm1core_dg_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                    /* (every save reaches the image at once) */
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_options(flash_path, no_flash, headless, NULL);
}
void fm1core_dg_init(int demo)
{
    fm1panel_t *p = FM1CORE_DG_PANEL;
    (void)demo;
    if (fm1core_dg_booted)
        return;
    fm1core_exit_cleanup = fm1nor_close;
    fm1core_exit_park = fm1coro_park;
    fm1nor_open();
    fm1core_dg_stores_load();
    fm1p_panel_default(p);
    if (!p->master)
        p->master = 724;                   /* (the host gave none: platform.c's master_q12 2048) */
    fm1core_dg_knob = p->master * 16;
    fm1core_dg_master();
    fm1t_set_ms(0);
    fm1_ms = 0;
    fm1fifo_init(&fm1core_dg_fifo, 256u, 2, FM1FIFO_S32, 0, fm1core_dg_render, NULL);
    fm1core_dg_co = fm1coro_new(fm1core_dg_main, NULL, 0);
    if (!fm1core_dg_co) {
        printf("fm1core %s: no memory for the main loop's stack: halted\n", FM1CORE_ID);
        fm1core_exit(1);
        return;
    }
    fm1core_dg_booted = 1;
    fm1core_dg_in();                       /* (a key held at power-on) */
    fm1core_dg_pass();                     /* the boot, up to the first poll */
    fm1core_dg_out();
}
void fm1core_dg_tick(uint32_t ms)
{
    if (!fm1core_dg_running())
        return;
    fm1t_set_ms(ms);
    fm1_ms = ms;
    fm1core_dg_in();
#ifdef FM1CORE_DG_SYSEX_IN
    while (fm1core_dg_sx_r != fm1core_dg_sx_w) {
        const uint32_t i = fm1core_dg_sx_r++ % FM1CORE_DG_SX_N;
        FM1CORE_DG_SYSEX_IN(fm1core_dg_sx[i], fm1core_dg_sx_len[i]);
    }
#endif
    fm1core_dg_pass();
}
void fm1core_dg_frame(void)
{
    if (fm1core_dg_running())
        fm1core_dg_out();
}
void fm1core_dg_idle(void)
{
    if (fm1core_dg_running())
        fm1core_dg_out();
}
void fm1core_dg_audio(int16_t *stereo, uint32_t frames)
{
    while (frames) {
        const uint32_t k = frames < FM1P_BLOCK ? frames : FM1P_BLOCK;
        if (!fm1core_dg_running() || fm1nor_stall_take())   /* (an erase: the ISR does not run) */
            memset(stereo, 0, (size_t)k * 2u * sizeof *stereo);
        else
            fm1fifo_pull(&fm1core_dg_fifo, stereo, k);
        stereo += 2u * k;
        frames -= k;
    }
}
int fm1core_dg_midi_in(uint32_t pkt)
{
    const uint32_t cin = pkt & 0xFu, st = (pkt >> 8) & 0xFFu;
    if (!fm1core_dg_running())
        return 1;                          /* (halted: taken and dropped) */
#ifdef FM1CORE_DG_SYSEX_IN
    if (fm1core_dg_sx_full(pkt))
        return 0;                          /* (the frames waiting first: this one after the next tick) */
    if (cin >= 0x4u && cin <= 0x7u) {
        static const uint8_t NB[4] = {3, 1, 2, 3};
        uint32_t i;
        for (i = 0; i < NB[cin - 4u]; i++)
            fm1core_dg_sx_byte((uint8_t)(pkt >> (8u * (i + 1u))));
        return 1;
    }
#endif
    if (cin < 0x8u || st < 0x80u || st == 0xF0u || st == 0xF7u)
        return 1;                          /* (SysEx and the system common bytes: not the apps' MIDI in) */
    return fm1core_dg_midi_put((uint8_t)st, (uint8_t)((pkt >> 16) & 0x7Fu), (uint8_t)((pkt >> 24) & 0x7Fu));
}
int fm1core_dg_midi_out_take(uint32_t *pkt)
{
    if (fm1core_dg_mo_r == fm1core_dg_mo_w)
        return 0;
    *pkt = fm1core_dg_mo[fm1core_dg_mo_r++ % FM1CORE_DG_MQ];
    return 1;
}
void fm1core_dg_dump(void)
{
    char b[160];
    fm1core_dg_info(b, sizeof b);
    printf("fm1core %s %s: ms %u, %s; flash writes %u, erases %u; main loop stack %u B, %u passes\n", FM1CORE_ID,
           FM1CORE_VERSION, (unsigned)fm1t_ms(), b, (unsigned)fm1nor_dirty(), (unsigned)fm1nor_stalls(),
           (unsigned)fm1coro_stack_peak(fm1core_dg_co), (unsigned)fm1coro_resumes(fm1core_dg_co));
}
void fm1core_dg_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    if (shed)
        *shed = 0;
    if (cpu_pct)
        *cpu_pct = 0;
}
void fm1core_dg_ui_info(char *buf, uint32_t n)
{
    if (!buf || !n)
        return;
    if (!fm1core_dg_booted) {
        snprintf(buf, n, "off");
        return;
    }
    fm1core_dg_info(buf, n);
}
const int8_t *fm1core_dg_keymap_get(void) { return &fm1core_dg_keymap[0][0]; }
uint8_t *fm1core_dg_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_dg_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_dg_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_dg_flash_sync(void)
{
    int ok;
    if (!fm1core_dg_running())
        return 1;
    ok = fm1core_dg_sync();
    fm1core_dg_stores_poll();
    return ok;
}

/* the module's unload: the main loop's stack */
__attribute__((destructor)) static void fm1core_dg_fini(void)
{
    if (fm1core_dg_co && !fm1coro_inside()) {
        fm1coro_free(fm1core_dg_co);
        fm1core_dg_co = NULL;
    }
}
