/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-KICK core: FM-KICK 0.5.1 by David Amo (https://github.com/davidamomusic/fm-kick, the pinned, unmodified
 * submodule cores/fm-kick: MPL-2.0. The repository is fm1-base itself with the app in kick/, and its own submodule
 * bsp = felucca-bsp, MPL-2.0) built with the fm1-base glue (core-glue/fm1base/README.md), from ECHOFIELD's glue
 * (core_echofield.c: felucca-bsp's chip/ota.c, backup.c, the service's coroutine) and Mighty Arp's (core_mightyarp.c:
 * its fm1-base b0976f2 is FM-KICK's less plat_out_off). The include list follows fm1-base's device unity build
 * (device/unity.c) with the hardware left out: plat.h, the app's header, gfx.c, fm1-base's defaults (fm1b_config.h),
 * the platform's screen faces (sys_text.c), the store (storage.c on the NOR image, the app's APP_STORE_MAP: the
 * settings, the four pattern objects of bank A, banks B and C, the 32 user kicks, the panel calibration, and the four
 * projects pooled in 0x97000..0xBFFFF), the updater's framing (felucca-bsp's chip/ota.c), the screen reader
 * (announce.c), the panel calibration (panel.c), the build with no radio (radio_none.c, fm1.toml ble = false), the
 * device's plat.h implementation (plat_fm1.c), backup and restore (backup.c, fm1.toml backup = true) and the app
 * (kick/app.c). The DSP (kick/dsp.c: the kick voice; kick/fx.c: the effects) are fm1.toml's "o2" units, compiled
 * apart (fmkick_dsp.c, fmkick_fx.c).
 *
 * Its package identity (FELUCCA_ID) is kick/release.sh's, fixed from 0.5.0 on: FM-1_222 (core_fmkick.cmake). The
 * version string is kick/app.h's own APP_VERSION: "0.5.0" at the tag v0.5.1 (GLO's SYSTEM page, the screen reader).
 *
 * THE SECOND CORE (fm1.toml cpu2 = true: REVERB's hall, fx_begin): each job runs at once, in order, on the host's one
 * thread, as fm1-base's host simulator has it (host/plat_host.c): the hall's output is the block's as on a two-core
 * unit; the SYSTEM page says 2 cores. Never given up.
 * Left out, as in every fm1base core: the splash (APP_SPLASH_RGB: plat_glue.h starts the UI at power-on), the render's
 * own time (the overload guards and the STATS page's load see 0 %), the screen mirror (fm1.toml mirror: FM1B_MIRROR 0,
 * the mirror page and its Render Audio get no answer: the host shows the screen itself), the headphone jack (fm1.toml
 * headphones: FM1B_HEADPHONES 0, plat_headphones -1: the AUDIO page's speaker switch governs), USB audio (the FM-1
 * recorded by a computer; the core's output is the host's audio), Bluetooth.
 * The AUDIO page's outputs (plat_out_off): main_fm1.c's render gives the FM-1's own output (PLAT_OUT_LINE: the speaker
 * and the jack) silence when they are switched off; here the core's output is that output, so the same. PLAT_OUT_USB
 * (the USB recording) has nothing to switch.
 *
 * THE MANAGERS (core-glue/plat/plat_sx.h): fm1-base's main loop (device/main_fm1.c fm1_main) gives a SysEx frame to
 * the screen reader (announce.c, 7D 53 52), the log readout (7D 4C 47) and the remote control (7D 4C 43, this
 * fm1-base's: no lease); there is no app_sysex. Then, every pass, ota_service (the M-UPGRADE handshake) and usb_cmd:
 * backup (0x42, streamed by bu_step) and restore (0x41: ota.c's read requests to the host, then RESTORE FROM USB? SEL /
 * HOME) of the app's objects (backup.c: tools/fm1_backup.py, the mirror page's Projects and backup); the mirror's own
 * commands are not built. Here the same, in that order, from the submodule's files; a restore waits as on the device:
 * the service runs on a coroutine (FM1CORE_PL_SX_CORO) that yields where the device feeds its watchdog (ota.c's reads).
 * The log readout says what an emulated unit has: no crash recorded, then "end". The update key F0 22 24 35 7F F7 and
 * UBOOT's F0 22 24 35 7D F7 are not taken (plat_sx.h): an installer gets the identity, no read requests (it times
 * out), and nothing is written.
 *
 * Saving: app_frame's autosave (1.5 s after the last change, while stopped: the patterns, the user kicks, the
 * settings); projects on PROJECTS' SAVE. flash_sync is that autosave without its waits, deferred while the sequencer
 * runs or a backup holds it. */
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

#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_fmkick.cmake)"
#endif
#define FM1B_BACKUP 1                     /* fm1.toml backup = true (build.py opt_flags) */
#define FM1B_MIRROR 0                     /* (fm1.toml mirror = true: not built, above) */
#define FM1B_CPU2 1                       /* fm1.toml cpu2 = true: the second core, simulated (below) */
#define FM1B_HEADPHONES 0                 /* (fm1.toml headphones = true: no jack here, above) */
#define FM1CORE_PL_SX 1                   /* the managers' SysEx (core-glue/plat/plat_sx.h) */
#define FM1CORE_PL_SX_CORO 1              /* (a restore waits for the host: the service's coroutine) */
#define FM1CORE_FB_MIRROR_DIM 1           /* (this fm1-base's mirror_leds: the glow's LEDs too; mirror_mid) */
#include "fm1base_hal.h"
#include "../plat/plat_sx.h"

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"
#include "storage.c"

/* felucca-bsp's chip/ota.c on the glue's transport (plat_sx.h), with unity.c's hooks on the core's HAL: the device's
 * ms, the wait (the watchdog fed: the coroutine yields), the update's staging area (OTA_AREA; backup.c keeps its
 * journal record there) on the NOR image. The update's own steps (ota_session: its screen, its commit) are never
 * reached here */
static int ota_wire_send(const uint8_t *p, uint32_t n) { return fm1core_pl_sx_send(p, n); }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { return fm1sx_frame(&fm1core_pl_sxi, p, n); }
static void ota_frame_done(void) { fm1sx_done(&fm1core_pl_sxi); }
#include "../../cores/fm-kick/bsp/chip/ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int fm1core_fk_ota_in(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    if (!fm1core_fk_ota_in(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fm1nor_erase(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!fm1core_fk_ota_in(off, n))
        return -8;
    return fm1nor_prog_span(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    (void)step;
    (void)code;
}
static void ota_commit(const uint8_t *parm) { (void)parm; }

#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static uint8_t backup_streaming(void);
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), a backup
                                                          * streaming: not sent (unity.c's) */
{
    if (backup_streaming())
        return -1;
    return fm1core_pl_sx_send(p, n);
}
#include "panel.c"
#include "radio_none.c"
static volatile uint8_t midi_usb_lost;   /* (usb.c's: the host's link never drops here; plat_usb_lost says 0) */
static void fm1_guard_pc_extend(uint32_t end) { (void)end; }   /* (the guard's code window, plat_code_window: no meaning here) */
/* felucca-bsp's chip/fm1_clock.h, as plat_fm1.c's plat_cpu_khz reads it (APP_CPU_KHZ: the SYSTEM page's clock): no
 * PLL here, so 0 kHz, "none", as fm1-base's host simulator and browser emulator say */
static int fm1_clock_is_stock(void) { return 0; }
#include "plat_fm1.c"
/* the second core (cpu2.c, not built): simulated as fm1-base's host simulator has it (host/plat_host.c): a job runs at
 * once, in order, on the one thread; the same sums as the device's split. Never given up */
static int fm1core_fk_cpu2_on;
int plat_cpu2_set(int on) { fm1core_fk_cpu2_on = on; return 0; }
int plat_cpu2_on(void) { return fm1core_fk_cpu2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!fm1core_fk_cpu2_on)
        return 0;
    fn(arg);
    return 1;
}
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }
#include "backup.c"
/* unity.c's hooks for backup.c (fm1-base's "backup and restore" section, on the core's HAL: the same statements) */
static uint8_t backup_streaming(void) { return bu.on; }
static int rs_fetch(uint32_t addr, uint8_t *dst, uint32_t n) { return ota_read(addr, dst, n); }
static int rs_note(uint32_t addr)                   /* (past the question: no session deadline, 3 tries) */
{
    uint8_t b[8];
    ota_deadline = 0;
    return ota_read(addr, b, 8);
}
static int rs_playing(void) { return app_backup_playing() != 0; }
static void rs_screen(const char *a, const char *b)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 92, 240, SYS_S, a, C_WHITE, 1);
    if (b)
        sys_box(0, 124, 240, SYS_S, b, C_HI, 1);
}
static void rs_begin(void)                          /* a backup is coming: patient now (5 s a read, 2 min in all) */
{
    app_panic();
    ota_try_ms = 5000u;
    ota_deadline = ota_now_ms() + 120000u;
}
static void rs_show(uint32_t pct)
{
    char b[8];
    uint32_t i = 0;
    if (pct >= 100u)
        b[i++] = '1';
    if (pct >= 10u)
        b[i++] = (char)('0' + pct / 10u % 10u);
    b[i++] = (char)('0' + pct % 10u);
    b[i++] = '%';
    b[i] = 0;
    if (!pct)
        lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 92, 240, SYS_S, "RESTORE", C_WHITE, 1);
    sys_box(0, 124, 240, SYS_S, b, C_HI, 1);
}
static int rs_ask(void)                             /* SEL yes, HOME no; playing or 60 s: no. The hand's buttons only */
{
    uint32_t t0 = fm1_ms, was = plat_buttons() & ~mirror_in_buttons() & ~remote.btn;
    rs_screen("RESTORE FROM USB?", "SEL yes   HOME no");
    sys_box(0, 156, 240, SYS_S, APP_BACKUP_WHAT, C_GRAY, 1);
    ann_say("Restore from USB? It " APP_BACKUP_WHAT ". SEL: yes. HOME: no.", 0, ANN_NOW | ANN_KEEP);
    while (fm1_ms - t0 < 60000u) {
        uint32_t b = plat_buttons() & ~mirror_in_buttons() & ~remote.btn, down = b & ~was;
        fm1_wdt_feed();
        was = b;
        if (down >> B_SEL & 1u)
            return 1;
        if ((down >> B_HOME & 1u) || rs_playing())
            return 0;
    }
    return 0;
}
static void rs_reload(void)
{
    app_panic();
    app_backup_restored();
}
static int rs_rec_erase(void) { return ota_erase(RS_REC); }
static int rs_rec_prog(uint32_t off, const void *src, uint32_t n) { return ota_prog(RS_REC + off, src, n); }
static int rs_rec_read(uint32_t off, void *dst, uint32_t n) { return ota_fread(RS_REC + off, dst, n); }
static int bu_room(void)                            /* the SysEx ring empty: a frame goes in whole (a host is there) */
{
    return !fm1sx_out_queued(&fm1core_pl_sxo);
}
static int bu_send(const uint8_t *p, uint32_t n) { return ota_wire_send(p, n); }
static void bu_flush(void) { app_backup_flush(); }
static void bu_hold(int on) { app_backup_hold(on); }
#include "app.c"
#undef __attribute__

/* ---------------------------------------------------------------- the managers' SysEx (plat_sx.h) --- */
/* main_fm1.c log_frame's answer on a unit that never crashed: one frame a line, F0 7D 4C 47 <ASCII> F7, "end" last */
static void fm1core_fk_log_line(const char *t)
{
    uint8_t f[48];
    uint32_t n = 0;
    f[n++] = 0xF0;
    f[n++] = 0x7D;
    f[n++] = 0x4C;
    f[n++] = 0x47;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    fm1core_pl_sx_send(f, n);
}
static int fm1core_fk_log_frame(const uint8_t *p, uint32_t n)
{
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    fm1core_fk_log_line("no crash recorded");
    fm1core_fk_log_line("end");
    return 1;
}
/* main_fm1.c usb_cmd: a frame in the update's framing that ota_service did not take: backup and restore (the mirror's
 * commands are not built) */
static void fm1core_fk_usb_cmd(uint32_t d)
{
    if (d < 7u || ota_dec[2] == 0x11u)
        return;
    if (d == 11u && ota_dec[2] == BU_REQ && bu_rd32(ota_dec + 6) == BU_MAGIC && flash_ok) {
        bu_start(app_backup_playing());   /* a backup to the host: streamed by bu_step, playing on */
        return;
    }
    if (d == 15u && ota_dec[2] == RS_REQ && flash_ok) {   /* a restore from the host */
        uint32_t len = ota_rd32(ota_dec + 6), crc = ota_rd32(ota_dec + 10);
        int begun = 0, rc;
        if (bu.on)
            bu_stop();                    /* (a backup streaming: given up) */
        ota_try_ms = 300u;                /* its head first: a host that does not answer costs a second */
        ota_deadline = ota_now_ms() + 3000u;
        rc = restore_rx(len, crc, &begun);
        ota_try_ms = 5000u;
        ota_deadline = 0;
        if (begun)
            app_ui_init();                /* the screen again (restored: everything reloaded) */
        app_say(rc == RS_E_PLAYING ? "STOP FIRST" : "RESTORE FROM USB",
                !rc ? "RESTORED" : rc == RS_E_PLAYING ? "then restore" : rc == RS_E_NO ? "NOTHING CHANGED" : "NOT RESTORED");
        return;
    }
}
#include "fm1base_remote.h"           /* the remote control, 7D 4C 43 (this fm1-base's: no lease) */
static int fm1core_fk_sx_accept(const uint8_t *f, uint32_t n)   /* the frames a service of fm1_main reads */
{
    return fm1sx_is_mupg(f, n) || FM1CORE_PL_SX_READER(f, n) || fm1core_pl_sx_is(f, n, 0x4Cu, 0x47u) ||
           fm1core_pl_sx_is(f, n, 0x4Cu, 0x43u);
}
/* main_fm1.c fm1_main, each pass: a frame the screen reader, the log readout or the remote control took is done; then
 * ota_service (the M-UPGRADE handshake) and usb_cmd, then the backup streaming */
static void fm1core_fk_sx_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && (ann_frame(p, n, fm1_ms) || fm1core_fk_log_frame(p, n) || fm1core_fb_remote_frame(p, n)))
        ota_frame_done();
    fm1core_fk_usb_cmd(ota_service());
    bu_step();                            /* a backup streaming to the host */
}
#define FM1CORE_PL_SX_ACCEPT fm1core_fk_sx_accept
#define FM1CORE_PL_SX_SERVICE() fm1core_fk_sx_service()
#define FM1CORE_PL_SX_WORK() (fm1core_pl_sxi.ready || bu.on)

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights_mid's steady mid level */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_pl_flash_boot(void)  /* fm1_main: a restore's commit cut short by a power loss finished */
{
    rs_boot();
    ota_boot_cleanup();
}
static void fm1core_pl_app_boot(void)    /* device/main_fm1.c fm1_main */
{
    app_init();                           /* the settings, the patterns, the user kicks, the engine, the second core */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block (no USB audio) */
{
    app_render(lr, n, APP_GAIN(master_pos));
    if (out_off & PLAT_OUT_LINE)          /* the speaker and the jack switched off (plat_out_off) */
        memset(lr, 0, 2u * n * sizeof *lr);
}
static void fm1core_pl_master(int32_t knob)   /* main_fm1.c: MASTER's position 0..4092 */
{
    fm1core_fb_master_adc = knob / 16;
    if (!fm1_an.con)
        fm1_an.master = (uint16_t)fm1core_fb_master_adc;
    master_pos = remote.master >= 0 ? (uint32_t)remote.master : (uint32_t)(knob / 4);
}
static void fm1core_pl_loop(void) { ble_poll(); }
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        app_say("UPDATE MODE IN", d);
    } else {
        app_say("UPDATE MODE", "CANCELLED");
    }
}
/* the lazy save now: app_frame's autosave (the patterns, the user kicks, the settings, one object a frame, 1.5 s after
 * the last change while stopped) without its waits; while the sequencer runs or a backup holds it: deferred */
static int fm1core_pl_sync(void)
{
    uint32_t i;
    if (!ui.pat_dirty && !ui.user_dirty && !ui.set_dirty)
        return 1;
    if (sq.running || bk_hold)
        return 0;
    for (i = 0; i < 4u; i++)
        if ((ui.pat_dirty & 1u << i) && plat_store_save(PAT_OBJ(i), &pk[i], sizeof pk[i]) == 0)
            ui.pat_dirty &= (uint8_t)~(1u << i);
    if (ui.user_dirty && plat_store_save(OBJ_USER, &us, sizeof us) == 0)
        ui.user_dirty = 0;
    if (ui.set_dirty && plat_store_save(OBJ_SET, &set, sizeof set) == 0)
        ui.set_dirty = 0;
    return !ui.pat_dirty && !ui.user_dirty && !ui.set_dirty;
}
static void fm1core_pl_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "mode %u page %u pattern %c%02u play %u bpm %u.%u dirty %u", (unsigned)ui.mode,
             (unsigned)ui.page[ui.mode], (char)('A' + PBANK), (unsigned)sq.cur + 1u, (unsigned)sq.running,
             (unsigned)set.bpm10 / 10u, (unsigned)set.bpm10 % 10u,
             (unsigned)((ui.pat_dirty != 0) | ui.set_dirty << 1 | ui.user_dirty << 2));
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
