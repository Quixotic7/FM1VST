/* SPDX-License-Identifier: GPL-3.0-only */
/* The Carnac OS core: Carnac OS (https://github.com/OwenKirby/carnac-os, the pinned, unmodified submodule
 * cores/carnac-os), OrbVorgis's wavetable synth on X0X's platform (its own pinned submodule x0x-firmware/), built as
 * one unit with the plat glue (core-glue/plat/README.md). The include list follows the device's unity build
 * (src/hardware/build_fm1.py: X0X's firmware/src/x0x.c cut at its app, then the synth's headers, the UI state
 * machine, the screens and src/hardware/synth_main_fm1.c) with the hardware left out, as the firmware's own browser
 * emulator (src/host/carnac_web.c) does: X0X's gfx.c, plat.h, storage.c (on the NOR image), ota.c, slots.c (the
 * header layout plat_fm1.c reads), panel.c and plat_fm1.c (X0X's plat.h on the HAL: the LED picture, the glow, PLAY's
 * green), the UI, the splash, and synth_panel.c (the settings, the SAVE banks with the preset store and the factory
 * bank, the panel: the same file the device and the browser emulator run). synth_main_fm1.c (the boot, the audio
 * interrupt, the main loop, the editor) is the hardware's: plat_glue.h redoes the loop, and the hooks below hold its
 * statements (the boot's order, the render with MIDI in, MASTER and CLOCK OUT, the editor's commands). The DSP and
 * the sequencers are their own unit (carnacos_dsp.c), as build_fm1.py builds them (synth_dsp.c, -O2).
 *
 * THE WAVES: on a unit the PCM waves (area 0 at 0x97000, area 1 at 0xCE000) and wavetable bank 0, Foundations (area 2
 * at 0xBC000), live in the store flash, uploaded by the installer (pcm_store_upload.py / installer.html, the editor's
 * PCM_* commands). Here the flash image is a unit the installer has run on: an area that holds no valid image at power-on
 * gets the release's own (src/host/pcm_images_host.c and foundations_image_host.c, the bytes of src/dsp/pcm_area*.bin
 * and foundations_bank.bin), then pcm_boot attaches the areas from the image as on the device (PCM_XIP: in place).
 *
 * THE MANAGERS (plat_sx.h): the installer (web/site/installer.html) finds the unit by the M-UPGRADE identity
 * (FM-1_900: build_fm1.py keeps X0X's PRODUCT), asks the editor's INFO (F0 7D 46 4C 01 F7 -> "CARNAC OS <version>")
 * and PCM_ID per area, and uploads the areas that differ. The main loop runs ed_service then ota_service; here the
 * same, with synth_main_fm1.c's editor (INFO, PING, the sample-slot and wavetable commands, refused, wt_store_fm1.c;
 * the PCM store's, pcm_store_fm1.c, compiled from the submodule) redone below: its PERF TEST, STATS and DEBUG
 * commands are the hardware's and get no reply (as a command that is not the firmware's). The update never runs
 * (the update key is not taken): an installer gets the identity and times out.
 *
 * One core renders every voice (synth_set_core2 unset: "the output is the same either way", synth_runtime.h), as
 * the browser emulator. Its version is a build-time define (SYNTH_VERSION, build_fm1.py --release): the hub's
 * version (core_carnacos.cmake). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>                       /* before exit.h: its prototypes must not see the macros */
#include <string.h>
#include "fm1core.h"
#include "version.h"
#include "nor.h"
#include "fifo.h"
#include "ticks.h"
#include "panel.h"
#include "exit.h"
#include "env.h"                          /* getenv -> NULL: no environment in a core (common/env.h) */

#define X0X_HOST 1                        /* plat.h: no .pool section */
#define SYNTH_VERSION FM1CORE_VERSION     /* build_fm1.py --release 0.9.6: "0.9.6" (upper-cased) */
#define FM1CORE_PL_SX 1                   /* the installer's identity and the editor's INFO / PCM_ID (plat_sx.h) */
#include "plat_hal.h"
#include "plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the browser emulator: the device's .pool / .noinit sections) */
#include "gfx.c"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "app/plat.h"
static void audio_silence(void) {}        /* (an erase silences the DMA buffer: fm1nor_stall_take) */
#include "storage.c"
/* ota.c on the glue's transport (common/sysex_ota.h): the M-UPGRADE identity answered, its flash hooks refuse.
 * FELUCCA_ID only around it: in the synth's sources it means the JieLi build (synth_libc.h's own memset ..) */
#define FELUCCA_ID "FM-1_900"             /* the package identity: build_fm1.py (X0X's PRODUCT, its --release dropped) */
#define FM1SX_OTA_C "../../cores/carnac-os/x0x-firmware/firmware/src/ota.c"
#define FM1SX_OTA_IN fm1core_pl_sxi
#define FM1SX_OTA_OUT fm1core_pl_sxo
#define FM1SX_OTA_MS() fm1_ms
#include "sysex_ota.h"
#undef FELUCCA_ID

/* X0X's platform pieces that are not its app (synth_main_fm1.c): the user-slot header layout, the panel map, plat.h
 * over the HAL. What plat_fm1.c needs beyond the plat family's HAL (the second core, the guards, the crash record,
 * the performance counters), as core_x0x.c has it: the second core never starts here (one core renders) */
#define SMP_USER_XIP(k) ((const uint8_t *)fm1nor_image + 0xA0000u + (uint32_t)(k) * 0x14000u)
#include "app/slots.c"
static int fm1_c1_on, fm1_c1_gave_up;
static struct {
    uint32_t bad;
} fm1_c1_mb;
static int fm1_cpu1_start(void)
{
    fm1_c1_on = 1;
    return 0;
}
static void fm1_cpu1_hold(void) { fm1_c1_on = 0; }
static int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1_c1_on)
        return 0;
    fn(arg);
    return 1;
}
static int fm1_cpu1_wait(void) { return 0; }
#define FM1_GUARD_PC 0u
static int fm1_guard_top_locked(void) { return 0; }
static void fm1_guard_unlock_top(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_guard_pc_open(void) {}
static void fm1_guard_enable(uint32_t g) { (void)g; }
#define FM1_C1_BAR_MAGIC 0x43314241u
static struct {
    uint32_t magic, barred;
} fm1_c1_bar;
static int fm1_perf_cycles_run(void) { return 0; }   /* (no CPU cycle counter: plat_cycles is the 24 MHz timer) */
static uint32_t fm1_perf_cycles(void) { return fm1_ticks(); }
static void fm1_perf_stalls(uint32_t s[3]) { s[0] = s[1] = s[2] = 0; }
static void fm1_perf_stalls_enable(int on) { (void)on; }
static uint32_t fm1core_carnac_irq_stack[64];
#define _sstack_lo fm1core_carnac_irq_stack
#define _sstack_top (fm1core_carnac_irq_stack + 64)
#include "app/panel.c"
#include "app/plat_fm1.c"
#undef _sstack_lo
#undef _sstack_top

/* the synth (build_fm1.py's unity: its headers, the canvas pieces X0X's gfx.c lacks, the UI and the screens) */
#include "dsp/synth_runtime.h"
#include "dsp/preset.h"
#include "dsp/wavetables.h"
#include "dsp/flash_record.h"
#include "dsp/midi_clock.h"
#include "dsp/voice_allocator.h"
#include "dsp/synth_voice.h"
#include "dsp/oscillators.h"
#include "dsp/lfo.h"
#include "dsp/pcm_store.h"
#include "UIStateMachine.h"
#include "hardware/fm1_x0x_canvas.h"
#include "UIStateMachine.c"
#include "ui_seq_draw.c"
#include "ui_arp_draw.c"
#include "ui_page_draw.c"
#include "carnac_splash.c"

/* synth_main_fm1.c's pieces that synth_panel.c needs: the moon on a USB host's power, the lock (one thread: the
 * render and the main loop take turns), no PERF TEST (the hardware's) */
static int32_t carnac_battery_moon(void) { return plat_battery() == 4 ? -2 : -1; }
static void syn_lock(void) {}
static void syn_unlock(void) {}
#include "hardware/perf_test.h"
uint8_t pt_state(void) { return PT_IDLE; }
#include "hardware/synth_panel.c"

/* ---------------------------------------------------------------- the editor (synth_main_fm1.c) --- */
/* F0 7D 46 4C cmd args F7: X0X's helpers, INFO, PING, the sample-slot and wavetable commands (refused: the slots are
 * the preset store's, wt_store_fm1.c) and the PCM store's (pcm_store_fm1.c), as synth_main_fm1.c ed_handle /
 * ed_service; the PERF TEST (44), STATS (46) and DEBUG (47) measure the hardware: no reply, as "not ours" */
#define ED_HDR0 0x7D
#define ED_HDR1 0x46
#define ED_HDR2 0x4C
enum { ED_INFO = 1, ED_SMP_BEGIN = 11, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO, ED_SMP_READ, ED_PING = 25 };
static uint8_t ed_out[288];
static uint32_t ed_n;
static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0;
    ed_out[1] = ED_HDR0;
    ed_out[2] = ED_HDR1;
    ed_out[3] = ED_HDR2;
    ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
static void ed_str(const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
    ota_wire_send(ed_out, ed_n);
}
#define PCM_XIP(off) ((const uint8_t *)fm1nor_image + (off))   /* the store read in place (the device: XIP) */
#include "hardware/wt_store_fm1.c"
#include "hardware/pcm_store_fm1.c"
#undef __attribute__

static void ed_handle(const uint8_t *f, uint32_t n)
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    ed_begin(cmd);
    if (ed_wt_handle(cmd, a, na) || ed_pcm_handle(cmd, a, na)) {
        ed_send();
        return;
    }
    switch (cmd) {
    case ED_INFO:
        ed_str("CARNAC OS " SYNTH_VERSION, 24);
        ed_b(0);                          /* no engines / parameters over this protocol */
        ed_b(0);
        ed_b(0);
        ed_b(SEQ_NSTEP);
        ed_b(0);
        ed_b(0);
        ed_b(plat_cpu2_on() ? 2u : 1u);   /* the cores rendering (one here) */
        ed_b((uint32_t)plat_cpu2_gave_up());
        break;
    case ED_PING:
        ed_b(0);
        break;
    case ED_SMP_INFO:                     /* the three slots, empty: they hold the wavetables */
        ed_b(SMP_USER_SLOTS);
        ed_b(SMP_USER_SIZE / 1024u);
        for (i = 0; i < SMP_USER_SLOTS; i++) {
            ed_b(0);
            ed_str("", 8);
            ed_b(0);
        }
        break;
    default:
        return;                           /* not ours (or the hardware's): no reply */
    }
    ed_send();
}
static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;                           /* not an editor frame: ota_service() reads it */
    if ((p[3] >= ED_SMP_BEGIN && p[3] <= ED_SMP_READ) || (p[3] >= ED_WT_BEGIN && p[3] <= ED_PCM_INFO)) {
        ed_handle(p, n);                  /* long frames (data): handled in place */
        ota_frame_done();
        return;
    }
    {
        static uint8_t f[64];
        uint32_t k = n > sizeof f ? sizeof f : n, i;
        for (i = 0; i < k; i++)
            f[i] = p[i];
        ota_frame_done();
        ed_handle(f, k);
    }
}

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
static int fm1core_carnac_sx_accept(const uint8_t *f, uint32_t n)   /* the editor's 7D 46 4C, the M-UPGRADE 00 32 */
{
    return fm1sx_is_mupg(f, n) || fm1core_pl_sx_is(f, n, ED_HDR1, ED_HDR2);
}
static void fm1core_carnac_sx_service(void)   /* fm1_main, each pass (the update key's ota_req: not taken) */
{
    ed_service();                         /* the editor SysEx (PCM wave upload, INFO) */
    ota_service();                        /* M-UPGRADE handshake */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_carnac_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_carnac_sx_service()

/* ---------------------------------------------------------------- the render (synth_main_fm1.c) --- */
/* the packets read before the block into the synth (the device times each into its half: midi_drain; here each
 * block takes what came before it, as the browser emulator) */
static void fm1core_carnac_midi_drain(void)
{
    uint32_t pkt, guard = 64;
    while (guard-- && plat_midi_in(&pkt)) {
        uint32_t cin = pkt & 0x0Fu;
        uint8_t st = (uint8_t)(pkt >> 8);
        if (cin >= 0x8u && cin <= 0xEu && st >= 0x80u && st < 0xF0u) {   /* a channel message */
            uint8_t d2 = (uint8_t)((pkt >> 24) & 0x7Fu);
            if ((st & 0xF0u) == 0x90u)
                d2 = instrument_velocity(d2);   /* GLOBAL > VEL CURVE (0 stays a note-off) */
            synth_midi_3(st, (uint8_t)((pkt >> 16) & 0x7Fu), d2);
        } else if (cin == 0xFu && st >= 0xF8u) {                           /* realtime: clock, start, stop */
            synth_midi_3(st, 0, 0);
        }
    }
}
/* the synth's Q15 to the codec, through MASTER (master_q12 0..4096), gliding over the block: full MASTER puts Q15 full
 * scale at 2^22 (X0X's NORMAL level) */
static int32_t fm1core_carnac_vol = -1;
static void fm1core_carnac_level(int32_t *lr, uint32_t n)
{
    int32_t tgt = (int32_t)master_q12, v = fm1core_carnac_vol < 0 ? tgt : fm1core_carnac_vol;
    const int32_t step = (tgt - v) / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++) {
        v += step;
        lr[2u * i] = (lr[2u * i] * v) >> 5;
        lr[2u * i + 1u] = (lr[2u * i + 1u] * v) >> 5;
    }
    fm1core_carnac_vol = tgt;
}
/* GLOBAL > CLOCK OUT: while the clock is internal, every pulse of it goes out as MIDI clock after the block that made
 * it, and the transport as START / STOP (synth_main_fm1.c clock_out) */
static uint32_t fm1core_carnac_clk_sent;
static uint8_t fm1core_carnac_clk_play;
static void fm1core_carnac_clock_out(void)
{
    uint32_t n = midi_clock_pulse_count(), guard = 8;
    uint8_t play;
    if (instrument.clock_out != CLKOUT_USB || midi_clock_is_external()) {
        fm1core_carnac_clk_sent = n;
        fm1core_carnac_clk_play = 0;
        return;
    }
    play = (uint8_t)(maq_playing(synth_seq()) || carp_playing(synth_arp()));
    if (play && !fm1core_carnac_clk_play)
        plat_midi_out(0x0Fu | 0xFAu << 8);
    else if (!play && fm1core_carnac_clk_play)
        plat_midi_out(0x0Fu | 0xFCu << 8);
    fm1core_carnac_clk_play = play;
    while (fm1core_carnac_clk_sent != n && guard--) {
        plat_midi_out(0x0Fu | 0xF8u << 8);
        fm1core_carnac_clk_sent++;
    }
    fm1core_carnac_clk_sent = n;
}

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME "CARNAC OS"
#define FM1CORE_PL_DIM_LEVEL FM1P_LED_DIM /* the glow and the dim keys: one scan frame in GLOW_DIV (or the ARP page's) */
#define SPLASH_MS 1500u                   /* fm1_main: the start-up screen stays at least this long */
static uint8_t fm1core_carnac_running, fm1core_carnac_force = 1;
static uint32_t fm1core_carnac_last_frame;
/* fm1_main's loop after the services: the panel each pass, the frame every 16 ms (plat_glue.h's cadence). Before
 * it, the splash: the device waits SPLASH_MS in fm1_main before its loop (the audio and MIDI run meanwhile) */
static int fm1core_carnac_up(void)
{
    if (fm1core_carnac_running)
        return 1;
    if (fm1_ms < SPLASH_MS)
        return 0;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    btn_prev = plat_buttons();            /* what is held at the start is no press */
    keys_prev = plat_keys();
    fm1core_carnac_running = 1;
    fm1core_carnac_force = 1;
    fm1core_carnac_last_frame = fm1_ms - 16u;
    return 1;
}
static void fm1core_carnac_pass(int frame)
{
    if (!fm1core_carnac_up())
        return;
    panel_input();
    if (frame) {
        const uint32_t ms = fm1_ms - fm1core_carnac_last_frame;
        fm1core_carnac_last_frame = fm1_ms;
        ui_sm_tick(&sm, (uint16_t)(ms > 1000u ? 1000u : ms));   /* the popups' timeouts */
        bend_spring();
        ui_screen_draw(&sm, synth_seq(), synth_arp(), fm1core_carnac_force);
        fm1core_carnac_force = 0;
        panel_leds();
    }
}
#define FM1CORE_PL_UI_FRAME() fm1core_carnac_pass(1)
#define FM1CORE_PL_UI_INPUT() fm1core_carnac_pass(0)

/* the release's waves into an area that holds none (a unit the installer has run on), before pcm_boot */
static void fm1core_carnac_seed(uint32_t area, const uint8_t *img, uint32_t len)
{
    uint32_t off;
    if (pcm_area_check(area) >= 0 || len > PCM_AREA[area].size)
        return;
    for (off = 0; off < len; off += PCM_SECTOR)
        fm1nor_erase(PCM_AREA[area].base + off);
    fm1nor_prog_span(PCM_AREA[area].base, img, len);
}
static void fm1core_pl_flash_boot(void)
{
    extern const uint8_t pcm_image_host0[], pcm_image_host1[], foundations_image_host[];
    extern const uint32_t pcm_image_host_len0, pcm_image_host_len1, foundations_image_host_len;
    fm1core_carnac_seed(0, pcm_image_host0, pcm_image_host_len0);
    fm1core_carnac_seed(1, pcm_image_host1, pcm_image_host_len1);
    fm1core_carnac_seed(PCM_BANK_AREA, foundations_image_host, foundations_image_host_len);
}
static void fm1core_pl_app_boot(void)    /* fm1_main, after panel_init and led_pos_init */
{
    uint32_t k;
    plat_perf_init();
    synth_init();
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], seq_lanes, &arp_state);
    ui_sm_attach_seq(&sm, synth_seq());
    ui_sm_attach_arp(&sm, synth_arp());
    ui_sm_attach_store(&sm, syn_slot_query, 0);
    ui_sm_attach_instrument(&sm, &instrument);
    seq_palette_set(SQPAL_ORANGE);
    settings_load();                      /* the THEME of the last session */
    factory_install();                    /* the factory bank (slots 0..31), under whatever is saved */
    ps_load();                            /* the saved patches, performances, sequences, arps */
    pcm_boot();                           /* the PCM waves and Foundations, from the store flash */
    syn_boot_perf();                      /* performance 0: what the first key plays */
    for (k = 0; k < NKEYS; k++)
        key_note[k] = NOTE_NONE;
    fm1core_carnac_running = 0;
    fm1core_carnac_force = 1;
}
static void fm1core_pl_ui_start(void)    /* the splash in the last session's THEME (the UI after SPLASH_MS) */
{
    carnac_splash(SYNTH_VERSION, settings_theme());
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* synth_main_fm1.c render_block, one 256-frame part */
{
    fm1core_carnac_midi_drain();
    synth_render_q15(lr, n);
    fm1core_carnac_level(lr, n);
    fm1core_carnac_clock_out();
}
static void fm1core_pl_master(int32_t knob)
{
    const uint32_t k10 = (uint32_t)(knob / 16);
    master_q12 = (k10 * k10) >> 8;        /* 0 .. ~4096 */
}
static void fm1core_pl_loop(void)        /* fm1_main: the lazy saves and a performance load, each pass */
{
    if (!fm1core_carnac_running)
        return;
    settings_tick();
    ps_tick();
    syn_perf_tick();                      /* (after ps_tick: a SAVE before it is in flash) */
}
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[24] = "UPDATE MODE IN 5..";
        d[15] = (char)('0' + left);
        draw_text_box(0, 110, 240, &FONT_S, d, C_WHITE, 1);
    } else {
        fm1core_carnac_force = 1;         /* the countdown covered the screen */
    }
}
/* flash_sync: a SAVE waiting for the main loop written, the settings' change written now (settings_tick waits
 * SET_SAVE_MS after the last one) */
static int fm1core_pl_sync(void)
{
    if (!fm1core_carnac_running)
        return 1;
    ps_tick();
    settings_tick();                      /* (a change: pending from now) */
    if (set_pending) {
        set_changed_ms = fm1_ms - SET_SAVE_MS;
        settings_tick();                  /* written */
        settings_tick();                  /* (and seen written: no longer pending) */
    }
    return !ps_dirty && !set_pending;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "page %u running %u seq %u arp %u theme %u waves %u", (unsigned)sm.page,
             (unsigned)fm1core_carnac_running, (unsigned)maq_playing(synth_seq()),
             (unsigned)carp_playing(synth_arp()), (unsigned)settings_theme(), (unsigned)pcm_waves_state);
}

#include "plat_glue.h"
#include "exit_end.h"
