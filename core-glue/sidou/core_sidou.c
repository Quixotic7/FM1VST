/* SPDX-License-Identifier: GPL-3.0-only */
/* SIDOU (https://github.com/dsablic/sidou, the pinned submodule cores/sidou): the core unit. SIDOU is bare-metal
 * firmware on felucca-bsp (platform/fm1/bsp, vendored): its portable instrument (app/, core/) is built as it is,
 * its device layer (platform/fm1) over this folder's hal/ (core_sidou.cmake reads every HAL header as this folder's).
 * This unit is platform/fm1/main.c (the boot, the song's and the tunes' glue, the main loop), unmodified but for two
 * places (core_sidou.cmake's overlay: the main loop's wait for the next interrupt is a yield, and an update request
 * does not run the update session), and the glue to and from the ABI's panel (core-api/fm1core.h):
 *   the main loop  fm1_cstart's boot guard, then sidou_main, on a coroutine (common/coro.h): it never returns, and it
 *                  waits where the device waits for its interrupts (the main loop's `idle`, the register ring's
 *                  producer, fm1_delay_ms, the watchdog's feed in the update protocol's reply wait); frame() and
 *                  idle() resume it FM1CORE_SD_PASSES times each device ms
 *   tick           TIMER5's work: the ms (uptime_ms), the key scan's debounced state (a press at once, a release after
 *                  the device's ~9 ms), a scan frame, the encoders' detents; USB attached and configured
 *   audio          main.c's ALNK0 interrupt: audio_fill, one 128-frame half per ABI block (24-bit left-justified,
 *                  int16 << 8), silent while a flash erase stalls (common/nor.c) or the audio is stopped. The block
 *                  ends at the next device ms's start, where the main loop runs: the store's write window (250 us
 *                  after a block, store_fm1.c) opens in the pass after a block, as on the device
 *   MIDI           sidou_midi.c (usb.c's rings)
 *   LEDs, LCD      fm1_led lit, fm1_led_dim the glow (input.c's planes); sidou_lcd.c's panel RAM
 *   exit()         common/exit.h: a reset (2), UBOOT (3) halt the core
 *   flash          common/nor.c's image, read in place for the store (hal/fm1_xip.h)
 * Not here (hardware): the TRS jack (MIDI comes in over "USB"), USB audio out to a computer (uac.alt stays 0), the
 * SIE, the watchdog, the second core's hold, the power-on UBOOT gesture (OCT- held: input_raw_keys reads pins). */
#include <stdio.h>
#include <stdlib.h>                       /* first: its exit / atexit prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "exit.h"
#include "env.h"
#include "coro.h"
#include "nor.h"
#include "ticks.h"
#include "sidou_glue.h"

/* the firmware's ISR entries (hal/fm1_isr.S on the device): their addresses are handed to the HAL, never called */
void isr_alnk0(void) {}
void isr_timer5(void) {}

static void fm1core_sd_upgrade_refused(void);
#include "fm1_adc.h"                     /* (the shim: MASTER's channel) */
#include "main.c"

#include "exit_end.h"
#include "../common/panel.h"              /* (by its path: app/panel.h is the firmware's "panel.h") */

static const int8_t FM1_KEYMAP_SD[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;   /* hal/fm1_input.h's, the factory wiring */

#define FM1CORE_SD_PASSES 4u              /* main-loop passes a device ms (the device's wake on every interrupt) */
#define FM1CORE_SD_RELEASE_MS 9u          /* hal/fm1_input.h: a release needs FM1_DEB_RELEASE frames open */

/* ---- the state the HAL shims share (sidou_glue.h) ---- */
volatile fm1core_sd_in_t fm1core_sd_in;
uint8_t fm1core_sd_led[FM1CORE_SD_NCOL];
uint8_t fm1core_sd_led_dim[FM1CORE_SD_NCOL];
volatile uint8_t fm1core_sd_alnk_con2;
static uint32_t fm1core_sd_now;
static fm1coro_t *fm1core_sd_co;
static uint8_t fm1core_sd_audio_up;          /* fm1_audio_init ran, and no fm1_audio_stop since */
static int32_t fm1core_sd_master = 1023;  /* the MASTER pot 0..1023: full, unity (app/master_gain.h: -60..0 dB) */
static uint32_t fm1core_sd_btn_held, fm1core_sd_key_held;   /* the debounced state the tick delivers */
static uint32_t fm1core_sd_btn_ms[32], fm1core_sd_key_ms[32];
static uint32_t fm1core_sd_refused;       /* update requests not run */

uint32_t fm1core_sd_ms(void) { return fm1core_sd_now; }
uint32_t fm1core_sd_ticks(void) { return fm1t_ticks24(); }
int fm1core_sd_can_wait(void) { return fm1coro_inside(); }
void fm1core_sd_wait(void)
{
    if (fm1coro_inside())
        fm1coro_yield();
}
void fm1core_sd_exit(int code) { fm1core_exit(code); }
uint8_t *fm1core_sd_nor(void) { return fm1nor_flash(); }
int32_t fm1core_sd_adc(uint32_t ch)
{
    if (ch == FM1_ADC_MASTER)
        return fm1core_sd_master;
    return ch == FM1_ADC_BATT ? 800 : -1;
}
void fm1core_sd_audio_on(int on) { fm1core_sd_audio_up = (uint8_t)(on != 0); }

/* main.c's main loop: an update request (M-UPGRADE's F0 22 24 35 7F F7) is not run (core_sidou.cmake's overlay
 * calls this in the place of upgrade()): a core has no update loader; the identity and the protocol's handshake are
 * still answered (update_poll's ota_service) */
static void fm1core_sd_upgrade_refused(void)
{
    fm1core_sd_refused++;
    printf("%s: M-UPGRADE update request ignored (no update inside a core)\n", FM1CORE_ID);
}

/* ---- the boot guard (boot_options) ---- */
static uint8_t fm1core_sd_guard_set;
static int fm1core_sd_boot_options_fw(int fail)
{
    memset(&boot_guard, 0, sizeof boot_guard);
    if (fail >= 0) {                      /* the last run died before it was stable: this boot counts it */
        boot_guard.magic = BOOTGUARD_MAGIC;
        boot_guard.pending = 1;
        boot_guard.failed = (uint32_t)fail;
        boot_guard.check = boot_guard.magic ^ boot_guard.pending ^ boot_guard.failed ^ BOOTGUARD_CHECK;
    }
    fm1core_sd_guard_set = 1;
    return 1;
}

/* fm1_cstart from its boot guard on (the second-core hold, TIMER4, the watchdog, memory: nothing to do here; the
 * power-on UBOOT gesture reads pins: left out), then sidou_main, which never returns */
static void fm1core_sd_main(void *arg)
{
    (void)arg;
    if (bootguard_boot(&boot_guard)) {
        printf("%s: boot guard: the third boot in a row that died early: UBOOT\n", FM1CORE_ID);
        fm1_enter_uboot();
        return;
    }
    fm1_irq_init();
    fm1_mailbox_clear();
    sidou_main();
}

/* ---- the panel ---- */
fm1panel_t fm1core_sd_panel;
#define FM1CORE_SD_PANEL (&fm1core_sd_panel)
static fm1p_edges_t fm1core_sd_keys_e, fm1core_sd_btn_e;

static void fm1core_sd_out(void)
{
    fm1panel_t *p = FM1CORE_SD_PANEL;
    const uint8_t *planes[2] = {fm1core_sd_led, fm1core_sd_led_dim};
    static const uint8_t levels[2] = {FM1P_LED_LIT, FM1P_LED_DIM};
    fm1p_leds_planes(p, planes, levels, 2, FM1P_PLAY_GREEN_POS);
    if (fm1core_sd_lcd_on)
        fm1p_lcd_out(p, fm1core_sd_lcd, fm1core_sd_lcd_writes);
}

int fm1core_sd_halted(void) { return fm1core_halted(); }
void fm1core_sd_shutdown(void)
{
    fm1core_run_exit();
    if (fm1core_sd_co)
        fm1coro_free(fm1core_sd_co);
    fm1core_sd_co = NULL;
}

void fm1core_sd_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (the store writes through: nothing is kept in RAM to save at exit) */
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}
int fm1core_sd_boot_options(int fail, const char *reason, int stage)
{
    (void)reason;
    (void)stage;
    return fm1core_sd_boot_options_fw(fail);
}

void fm1core_sd_init(int demo)
{
    fm1panel_t *p = FM1CORE_SD_PANEL;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    fm1core_exit_park = fm1coro_park;     /* (an exit() on the loop's coroutine: it is never resumed) */
    if (p->master)
        fm1core_sd_master = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    fm1p_panel_default(p);                /* SIDOU's ids are Felucca's (app/panel.h PANEL_BTN_*, PANEL_ENC_*) */
    fm1t_set_ms(0);
    fm1core_sd_now = 0;
    if (!fm1core_sd_guard_set)
        memset(&boot_guard, 0, sizeof boot_guard);   /* (a power-on: .noinit holds noise, which the guard resets) */
    fm1nor_open();                        /* (common/nor.c: erased, then the stage or the file) */
    fm1core_sd_co = fm1coro_new(fm1core_sd_main, NULL, 0);
    if (!fm1core_sd_co) {
        printf("%s: no memory for the main loop's stack\n", FM1CORE_ID);
        exit(1);
        return;
    }
    fm1coro_resume(fm1core_sd_co);        /* (the boot, up to its first wait) */
    fm1core_sd_usb_attached();            /* (usb_start ran in midi_init: a host is there, configured) */
    if (fm1core_halted())
        return;
    if (!p->master)
        p->master = fm1core_sd_master;
    p->ready = 1;
    fm1core_sd_out();
}

/* TIMER5's 1 ms: the key scan's debounced state (a press is an edge at once, a release only after the key was held
 * FM1CORE_SD_RELEASE_MS, a tap shorter than a tick still arrives), one scan frame, the detents */
void fm1core_sd_tick(uint32_t ms)
{
    fm1panel_t *p = FM1CORE_SD_PANEL;
    uint32_t i, b, k, down;
    if (fm1core_halted())
        return;
    fm1t_set_ms(ms);
    fm1core_sd_now = ms;
    uptime_ms = ms;
    timer_irqs += 10u;
    fm1core_sd_master = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    b = fm1p_edges(&fm1core_sd_btn_e, p->buttons, fm1p_take_button_taps(p), NULL, NULL);
    k = fm1p_edges(&fm1core_sd_keys_e, p->keys, fm1p_take_key_taps(p), NULL, NULL);
    down = b & ~fm1core_sd_btn_held;
    for (i = 0; i < FM1P_NBTN; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_sd_btn_ms[i] = ms;
        else if ((fm1core_sd_btn_held & m) && !(b & m) && ms - fm1core_sd_btn_ms[i] < FM1CORE_SD_RELEASE_MS)
            b |= m;
    }
    fm1core_sd_in.released |= fm1core_sd_btn_held & ~b;
    fm1core_sd_in.pressed |= down;
    fm1core_sd_btn_held = b;
    down = k & ~fm1core_sd_key_held;
    for (i = 0; i < FM1P_NKEY; i++) {
        const uint32_t m = 1u << i;
        if (down & m)
            fm1core_sd_key_ms[i] = ms;
        else if ((fm1core_sd_key_held & m) && !(k & m) && ms - fm1core_sd_key_ms[i] < FM1CORE_SD_RELEASE_MS)
            k |= m;
    }
    fm1core_sd_in.notes_pressed |= down;
    fm1core_sd_key_held = k;
    fm1core_sd_in.buttons = fm1core_sd_btn_held;
    fm1core_sd_in.notes = fm1core_sd_key_held;
    for (i = 0; i < FM1P_NENC; i++)
        fm1core_sd_in.enc_steps[i] = (int16_t)(fm1core_sd_in.enc_steps[i] + fm1p_take_enc(p, i));
    fm1core_sd_in.frames++;
    fm1core_sd_usb_attached();
}

static void fm1core_sd_run(void)          /* frame and idle: this device ms of the main loop */
{
    uint32_t n;
    if (fm1core_halted())
        return;
    for (n = 0; n < FM1CORE_SD_PASSES && fm1core_sd_co && !fm1core_halted() && !fm1coro_done(fm1core_sd_co); n++)
        fm1coro_resume(fm1core_sd_co);
    if (!fm1core_halted())
        fm1core_sd_out();
}
void fm1core_sd_frame(void) { fm1core_sd_run(); }
void fm1core_sd_idle(void) { fm1core_sd_run(); }

/* main.c's fm1_alnk0_irq, its block's end at the next device ms (see the top) */
void fm1core_sd_audio(int16_t *out, uint32_t frames)
{
    uint32_t f, i;
    for (f = 0; f + FM1P_BLOCK <= frames; f += FM1P_BLOCK) {
        int16_t *o = out + 2u * f;
        if (fm1core_halted() || !fm1core_sd_audio_up || fm1nor_stall_take()) {
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);
            continue;
        }
        in_audio = 1;
        audio_fill(&abuf[0]);
        audio_block_end = fm1t_ticks24() + 24000u;
        in_audio = 0;
        for (i = 0; i < 2u * FM1P_BLOCK; i++) {
            int32_t v = abuf[i] >> 8;
            o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
    if (f < frames)
        memset(out + 2u * f, 0, (size_t)(frames - f) * 2u * sizeof *out);
}
_Static_assert(AUDIO_HALF_FRAMES == FM1P_BLOCK, "SIDOU's block is the ABI's");

int fm1core_sd_midi_in_abi(uint32_t pkt) { return fm1core_halted() ? 1 : fm1core_sd_midi_in(pkt); }
int fm1core_sd_midi_out_take_abi(uint32_t *pkt) { return fm1core_halted() ? 0 : fm1core_sd_midi_out_take(pkt); }

/* ---- diagnostics ---- */
void fm1core_sd_dump(void)
{
    uint32_t c;
    printf("fm1_in: notes %07X buttons %04X  uptime %u ms  frames %u\n", (unsigned)fm1core_sd_in.notes,
           (unsigned)fm1core_sd_in.buttons, (unsigned)uptime_ms, (unsigned)fm1core_sd_in.frames);
    printf("  leds:");
    for (c = 0; c < FM1CORE_SD_NCOL; c++)
        printf(" %02X/%02X", fm1core_sd_led[c], fm1core_sd_led_dim[c]);
    printf("  (lit/glow per column)\n");
    printf("  audio %s, master %d (live %u), queued writes %u, lcd %s (%u windows)\n", fm1core_sd_audio_up ? "on" : "off",
           (int)fm1core_sd_master, (unsigned)instrument.master, (unsigned)audio_queued(), fm1core_sd_lcd_on ? "on" : "off",
           (unsigned)lcd_count.windows);
    fm1core_sd_midi_dump();
    printf("  flash: %s, %u writes, %u erases with the audio stalled (%u blocks); store %s; %u update requests ignored\n",
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty(), (unsigned)fm1nor_stalls(),
           (unsigned)fm1nor_stall_blocks(), store_fm1_active ? "busy" : "idle", (unsigned)fm1core_sd_refused);
    if (fm1core_sd_co)
        printf("  the main loop's coroutine: %u resumes, its stack's peak %u of %u bytes\n",
               (unsigned)fm1coro_resumes(fm1core_sd_co), (unsigned)fm1coro_stack_peak(fm1core_sd_co),
               (unsigned)FM1CORO_STACK);
}
void fm1core_sd_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = 0;
}
void fm1core_sd_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "%s, preset %u%s", sid_tune.state == TUNE_OFF ? "SIDOU" : "a .sid tune",
             (unsigned)instrument.preset, keys_router.song ? ", a song on chip A" : "");
}
const int8_t *fm1core_sd_keymap(void) { return &FM1_KEYMAP_SD[0][0]; }

/* ---- the flash (ABI 2): common/nor.c ---- */
uint8_t *fm1core_sd_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_sd_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_sd_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_sd_settings_current(void);
int fm1core_sd_flash_sync(void)
{
    if (fm1core_halted() || !FM1CORE_SD_PANEL->ready)
        return 1;
    /* the store's work in hand (a save, a SysEx write, maintenance), or the settings' debounced save still to come */
    return !store_fm1_active && fm1core_sd_settings_current();
}
