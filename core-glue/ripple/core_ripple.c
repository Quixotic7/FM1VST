/* SPDX-License-Identifier: GPL-3.0-only */
/* The Ripple core, the first Fantasy CFW (docs/VIRTUAL-CARTS.md section 7): Ripple by Charles Vestal
 * (https://github.com/charlesvestal/fm1-ripple, the pinned, unmodified submodule cores/fm1-ripple at v0.4, MPL-2.0,
 * with its own submodules base = fm1-base and base/bsp = felucca-bsp, MPL-2.0; Lua 5.4.7, Plaits and stmlib, the
 * currents SDK: MIT) built with the fm1-base glue (core-glue/fm1base/README.md: the plat family's HAL and shared core),
 * from FRASIL's glue (core_frasil.c: the same fm1-base generation) without the managers' SysEx. README.md here.
 *
 * Ripple's host path, not the device's: PLAT_HOST and PLAT_WEB, as fm1-base's browser emulator builds it
 * (base/web/emu/emu_web.c, tools/build.py --web): every built-in current compiled in and found by name (flow.c's
 * PLAT_WEB lookup, the registry below), Plaits included; no native .cur files (JieLi code), no page currents (the
 * emulator's env.rp_js_cur_* imports are stand-ins here: none), one core (plat_cpu2_*: off), no USB disk mode.
 *
 * Driven as a step function: plat_glue.h's one pass per device millisecond, no coroutine at all (no SysEx service,
 * FM1CORE_PL_SX 0), so Lua's errors are the only Asyncify user in the .rfm1 (core-glue/common/lj_asyncify.h): nothing
 * can yield while Lua frames are on the stack.
 *
 * Two of Ripple's MPL-2.0 files are read through overlays (core_ripple.cmake writes them into the build folder from
 * the submodule's own, with a few lines changed, their MPL headers kept; a clang VFS overlay has the compiler read
 * them in place of the originals):
 *   app/app.c          the list's "disk mode" entry is "reload folder" (rp_folder_reload); "empty the disk" reloads
 *                      the folder after it
 *   app/lua/luaport.h  Lua's errors on Asyncify in an .rfm1 (lj_asyncify.h), a lower LUAI_MAXSTACK, and the count hook
 *                      at every new state (ripple_lua_hook.c: a callback that runs too long is stopped with an error)
 *
 * The droplets come from the shared RemoteFM1 folder (rfm1_host.h, capability fs1): at power-on and whenever the
 * host's change counter moves (or "reload folder"), a state machine driven from the main loop's passes lists
 * /code/droplets and /code/lib, reads each *.lua (64 KB at most, as the emulator's rp_web_put takes them) and makes
 * the disk's /droplets and /lib match: written where they differ, deleted where the folder has none (the folder is
 * authoritative for those two; README.txt and the rest stay). The disk (FAT12 at 0x97000..0xDC000, /data's .DAT
 * files with it) is in the flash image, so a save carries the droplets' data. No folder (FS_ENOFOLDER): the disk is
 * left as it is (a fresh flash: the built-in droplets only), silently.
 *
 * Its version (APP_VERSION) and package identity (FELUCCA_ID) are build.py --release's, from core_ripple.cmake. */
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
#include "rfm1_host.h"

#ifndef APP_VERSION
#error "APP_VERSION: build.py --release's (core_ripple.cmake)"
#endif
#ifndef FELUCCA_ID
#error "FELUCCA_ID: the package identity (core_ripple.cmake)"
#endif
#if !defined PLAT_HOST || !defined PLAT_WEB
#error "PLAT_HOST and PLAT_WEB: Ripple's emulator path (core_ripple.cmake)"
#endif
#define FM1CORE_FB_MIRROR_DIM 1           /* (fm1-base 9bca715 on, Ripple 0.6.1: mirror_leds with the glow's LEDs; mirror_mid) */
#include "../fm1base/fm1base_hal.h"

void rp_lua_budget(void);                 /* ripple_lua_hook.c: a pass's Lua budget, full again */
uint32_t rp_lua_stopped(void);            /* callbacks the hook has stopped since power-on */

/* ---------------------------------------------------------------- the firmware --- */
#define __attribute__(x)                  /* (as the host simulator: .noinit / .pool sections) */
#include "plat.h"
#include "app.h"
#include "touch.h"
#include "gfx.c"
#include "fm1b_config.h"
#include "sys_text.c"                     /* the faces of the platform's own screens (panel.c's) */
#include "storage.c"

/* plat_fm1.c's host reads (plat_host_read, plat_host_patience): felucca-bsp's chip/ota.c reads, over the managers'
 * SysEx, which this core does not answer: nothing read */
static uint32_t ota_try_ms, ota_deadline;
static uint32_t ota_now_ms(void) { return fm1_ms; }
static int ota_read(uint32_t addr, uint8_t *dst, uint32_t n)
{
    (void)addr, (void)dst, (void)n;
    return -1;
}
/* felucca-bsp's usb.c and guard, as plat_fm1.c reads them: the USB link never lost (a host is always there); the
 * guard's code window (plat_code_window: the currents' arena may run code on the device) has no meaning here */
static volatile uint8_t midi_usb_lost;
static void fm1_guard_pc_extend(uint32_t end) { (void)end; }

#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"
#include "announce.c"
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* (no SysEx path: not sent) */
{
    (void)p, (void)n;
    return -1;
}
#include "panel.c"
#include "radio_none.c"
#include "plat_fm1.c"
#include "touch.c"                        /* (PLAT_HOST: its host part, app_host_key / app_host_tick) */
#undef __attribute__

/* fm1.toml cpu2 = true: the second core, off here (one thread runs the device); flow.c renders every current itself */
int plat_cpu2_set(int on) { return on ? -1 : 0; }
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn, (void)arg; return 0; }
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }
int plat_cpu2_gave_up(void) { return 3; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 1; }
uint32_t plat_cpu2_stack(void) { return 0; }
/* fm1.toml msc = true: disk mode, never entered (the list's entry is "reload folder"): no computer, ejected at once */
void plat_disk_mode(const plat_disk_t *d) { (void)d; }
int plat_disk_poll(void) { return PLAT_DISK_EJECTED; }
uint32_t plat_disk_idle_ms(void) { return 0; }

static void rp_folder_reload(void);       /* (the overlay's app.c: the list's "reload folder") */
#include "app.c"

/* ---------------------------------------------------------------- the currents --- */
/* flow.c's PLAT_WEB lookup: every current compiled in, by name (ripple_currents.h: core_ripple.cmake, from
 * currents/ and currents/extra; each one's rc_entry renamed rc_entry_<ident>) */
#define RP_CUR(name, ident) const rc_desc_t *rc_entry_##ident(void);
#include "ripple_currents.h"
#undef RP_CUR
#define RP_CUR(name, ident) {name, rc_entry_##ident},
const struct rc_web rc_web_list[] = {
#include "ripple_currents.h"
    {0, 0}};
#undef RP_CUR

/* the emulator page's own currents (flow.c's env.rp_js_cur_* imports): a host has none */
int rp_js_cur_find(const char *name, int len) { (void)name, (void)len; return 0; }
void rp_js_cur_info(int id, void *info) { (void)id, (void)info; }
int rp_js_cur_new(int id, float sr) { (void)id, (void)sr; return 0; }
void rp_js_cur_set(int slot, int p, float v) { (void)slot, (void)p, (void)v; }
void rp_js_cur_note(int slot, int note, float vel) { (void)slot, (void)note, (void)vel; }
void rp_js_cur_process(int slot, float *l, float *r, int n)
{
    (void)slot;
    memset(l, 0, (size_t)n * sizeof *l);
    memset(r, 0, (size_t)n * sizeof *r);
}
void rp_js_cur_free_all(void) {}

/* the built-in volume's stand-in .cur headers (ripple_gen.py) told what the compiled current is: its kind (info:
 * 0x8000, 0x100 when it processes the bus, its voices) and its state's size, as currents.py writes a real one
 * (current.list(), current.fits() read them) */
static void rp_builtin_fill(void)
{
    vol_t *v = &vols[BUILTIN_VOL];
    uint32_t i;
    disk_mount();
    for (i = 0; rc_web_list[i].name; i++) {
        const rc_desc_t *d = rc_web_list[i].entry();
        char path[48] = "/currents/";
        disk_file_t f;
        uint8_t *h;
        uint32_t info, lba;
        if (!d || strlen(rc_web_list[i].name) > 24u)
            continue;
        text_cat(path, rc_web_list[i].name);
        text_cat(path, ".cur");
        if (vol_open(v, path, &f) != 0 || f.dir || f.size < sizeof(cur_head_t) || !f.cluster)
            continue;
        lba = v->data_lba + (f.cluster - 2u) * v->spc;
        if (lba >= BUILTIN_SECTORS)
            continue;
        h = BUILTIN_IMAGE + lba * 512u;
        info = 0x8000u | (d->ins ? 0x100u : 0u) | (d->voices & 0xFFu);
        h[6] = (uint8_t)info;
        h[7] = (uint8_t)(info >> 8);
        memcpy(h + offsetof(cur_head_t, state_size), &d->state_size, 4u);
    }
}

/* ---------------------------------------------------------------- the folder --- */
#define RS_MAXF 48                        /* files a folder's sync takes (the list holds 48) */
#define RS_FILE_MAX 65536u                /* a droplet or a library: rp_web_put's limit */
enum { RS_IDLE, RS_LIST, RS_READ };
static struct {
    uint8_t phase, dir, started, want, said;
    int32_t req, seen, err;
    uint32_t list_at;
    int nf, fi;
    char names[RS_MAXF][64];              /* the folder's *.lua in this directory */
    char keep[RS_MAXF][11];               /* their 8.3 names on the disk */
    int nkeep;
    uint32_t written, deleted, skipped, nfiles[2], syncs;
    int32_t last;                         /* the last sync's outcome: 1 done, 0 none yet, < 0 an FS_ error */
} rs;
static uint8_t rs_list_buf[8192];
static uint8_t rs_buf[RS_FILE_MAX + 1u];
static const char *const RS_DIR[2] = {"droplets", "lib"};

/* "MySynth.lua" -> "MYSYNTH LUA": the letters, digits, _ and - of its name, eight at most (web.c's web_83) */
static void rs_83(const char *name, char *n83)
{
    uint32_t i, k = 0;
    memset(n83, ' ', 11u);
    for (i = 0; name[i] && name[i] != '.' && k < 8u; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            n83[k++] = c;
    }
    if (!k)
        n83[k++] = 'X';
    memcpy(n83 + 8, "LUA", 3u);
}

/* the disk's file dir/n83 has exactly these bytes */
static int rs_same(const char *dir, const char *n83, const uint8_t *b, uint32_t n)
{
    char path[32];
    disk_file_t f;
    uint8_t chunk[512];
    uint32_t i, k = 0, at;
    snprintf(path, sizeof path, "/%s/", dir);
    k = (uint32_t)strlen(path);
    for (i = 0; i < 8u && n83[i] != ' '; i++)
        path[k++] = n83[i];
    memcpy(path + k, ".LUA", 5u);
    disk_mount();
    if (vol_open(&vols[DISK_VOL], path, &f) != 0 || f.dir || f.size != n)
        return 0;
    for (at = 0; at < n; at += sizeof chunk) {
        uint32_t m = n - at < sizeof chunk ? n - at : (uint32_t)sizeof chunk;
        if (disk_read(&f, at, chunk, m) != m || memcmp(chunk, b + at, m))
            return 0;
    }
    return 1;
}

/* the 8.3 entry n83 of the disk's directory dir deleted: its clusters freed, it and the long-name parts before it
 * (in its sector) marked deleted */
static void rs_delete(const char *dir, const char *n83)
{
    disk_file_t d;
    uint32_t k, j;
    char path[16];
    snprintf(path, sizeof path, "/%s", dir);
    disk_mount();
    if (vol_open(&vols[DISK_VOL], path, &d) != 0 || !d.dir)
        return;
    for (k = 0;; k++) {
        uint32_t lba = chain_lba(d.cluster, k), c;
        const uint8_t *s;
        uint8_t sec[512];
        int jj;
        if (!lba || !(s = disk_sector(lba)))
            return;
        for (j = 0; j < 512u; j += 32u) {
            const uint8_t *e = s + j;
            if (e[0] == 0)
                return;
            if (e[0] == 0xE5 || (e[11] & 0x3Fu) == 0x0Fu || (e[11] & 0x18u) || memcmp(e, n83, 11u))
                continue;
            c = rd16(e + 26);
            while (cl_ok(c)) {
                uint32_t nx = fat_next(c);
                fat_set(c, 0);
                c = nx;
            }
            memcpy(sec, disk_sector(lba), 512u);
            sec[j] = 0xE5;
            for (jj = (int)j - 32; jj >= 0 && (sec[jj + 11] & 0x3Fu) == 0x0Fu && sec[jj] != 0xE5; jj -= 32)
                sec[jj] = 0xE5;
            dm_write(lba, sec);
            dm_flush();
            return;
        }
    }
}

/* the disk's *.LUA files in dir that the folder does not have: deleted */
static void rs_prune(const char *dir)
{
    char gone[RS_MAXF][11], path[16];
    int ng = 0, i, k;
    disk_file_t d;
    uint32_t c, j;
    snprintf(path, sizeof path, "/%s", dir);
    disk_mount();
    if (vol_open(&vols[DISK_VOL], path, &d) != 0 || !d.dir)
        return;
    for (c = 0; ng < RS_MAXF; c++) {
        uint32_t lba = chain_lba(d.cluster, c);
        const uint8_t *s;
        int end = 0;
        if (!lba || !(s = disk_sector(lba)))
            break;
        for (j = 0; j < 512u && ng < RS_MAXF; j += 32u) {
            const uint8_t *e = s + j;
            if (e[0] == 0) {
                end = 1;
                break;
            }
            if (e[0] == 0xE5 || e[0] == '.' || (e[11] & 0x3Fu) == 0x0Fu || (e[11] & 0x18u) || memcmp(e + 8, "LUA", 3u))
                continue;
            for (k = 0; k < rs.nkeep && memcmp(rs.keep[k], e, 11u); k++)
                ;
            if (k == rs.nkeep)
                memcpy(gone[ng++], e, 11u);
        }
        if (end)
            break;
    }
    for (i = 0; i < ng; i++) {
        rs_delete(dir, gone[i]);
        rs.deleted++;
    }
}

static int rs_ends_lua(const char *s, uint32_t n)
{
    return n > 4u && s[n - 4] == '.' && (s[n - 3] | 32) == 'l' && (s[n - 2] | 32) == 'u' && (s[n - 1] | 32) == 'a';
}

static void rs_issue(int op, const char *path, int64_t off, void *buf, int32_t len)
{
    int32_t id = rfm1_fs_request(op, path, (int32_t)strlen(path), off, buf, len);
    if (id > 0) {
        rs.req = id;
        return;
    }
    rs.req = 0;
    rs.err = id ? id : FS_EIO;            /* (refused at once: handled as its result next) */
}

static void rs_finish(int32_t outcome)
{
    rs.phase = RS_IDLE;
    rs.last = outcome;
    if (outcome < 0) {
        if (outcome != FS_ENOFOLDER || rs.said) {   /* (no folder: silent, unless asked from the list) */
            char e[24];
            snprintf(e, sizeof e, outcome == FS_ENOFOLDER ? "NONE" : "ERROR %d", (int)outcome);
            app_say("FOLDER", e);
        }
        rs.said = 0;
        return;
    }
    rs.syncs++;
    if (rs.written || rs.deleted || rs.said) {
        char m[24];
        snprintf(m, sizeof m, "%u DROPLETS", (unsigned)rs.nfiles[0]);
        if (ui.mode == MODE_LIST)
            list_scan();                   /* (a droplet running keeps running; the list shows them when back) */
        app_say("FOLDER", m);
    }
    rs.said = 0;
}

/* the next request of the sync, or its end */
static void rs_next(void)
{
    char path[96];
    for (;;) {
        if (rs.phase == RS_LIST) {
            snprintf(path, sizeof path, "/code/%s", RS_DIR[rs.dir]);
            rs_issue(FS_LIST, path, (int64_t)rs.list_at, rs_list_buf, (int32_t)sizeof rs_list_buf);
            return;
        }
        if (rs.fi < rs.nf) {               /* RS_READ */
            snprintf(path, sizeof path, "/code/%s/%s", RS_DIR[rs.dir], rs.names[rs.fi]);
            rs_issue(FS_READ, path, 0, rs_buf, (int32_t)sizeof rs_buf);
            return;
        }
        rs_prune(RS_DIR[rs.dir]);          /* this directory read: what the folder lacks goes */
        rs.nfiles[rs.dir] = (uint32_t)rs.nkeep;
        if (++rs.dir >= 2u) {
            rs_finish(1);
            return;
        }
        rs.phase = RS_LIST;
        rs.list_at = 0;
        rs.nf = rs.fi = rs.nkeep = 0;
    }
}

/* one request's result */
static void rs_result(int32_t r)
{
    if (rs.phase == RS_LIST) {
        if (r == FS_ENOENT)
            r = 0;                         /* (no such directory in the folder: none there) */
        if (r < 0) {
            rs_finish(r);
            return;
        }
        if (r == 0) {
            rs.phase = RS_READ;
            rs.fi = 0;
        } else {
            uint32_t at = 0, i;
            for (i = 0; i < (uint32_t)r && at + 2u <= sizeof rs_list_buf; i++) {
                const uint32_t dir = rs_list_buf[at], nl = rs_list_buf[at + 1u];
                const char *nm = (const char *)rs_list_buf + at + 2u;
                if (at + 2u + nl > sizeof rs_list_buf)
                    break;
                if (!dir && nl < 64u && rs_ends_lua(nm, nl) && rs.nf < RS_MAXF) {
                    memcpy(rs.names[rs.nf], nm, nl);
                    rs.names[rs.nf++][nl] = 0;
                }
                at += 2u + nl;
            }
            rs.list_at += (uint32_t)r;
        }
    } else {                               /* RS_READ: the file's bytes */
        if (r == FS_ENOFOLDER) {
            rs_finish(r);
            return;
        }
        if (r >= 0 && (uint32_t)r <= RS_FILE_MAX) {
            char n83[11];
            rs_83(rs.names[rs.fi], n83);
            if (!rs_same(RS_DIR[rs.dir], n83, rs_buf, (uint32_t)r)) {
                char dir[12];
                int rc;
                snprintf(dir, sizeof dir, "/%s", RS_DIR[rs.dir]);
                rc = disk_write_file(dir, n83, rs_buf, (uint32_t)r);
                if (rc == 0)
                    rs.written++;
                else
                    rs.skipped++;          /* (the disk or its directory is full: -2, -3) */
            }
            if (rs.nkeep < RS_MAXF)
                memcpy(rs.keep[rs.nkeep++], n83, 11u);
        } else {
            rs.skipped++;                  /* (too big, or not readable: as if not there) */
        }
        rs.fi++;
    }
}

/* once a pass, from the main loop (fm1core_pl_loop): the request's result if it came, then the next request; a sync
 * starts at power-on, when the host's change counter moves, or when the list asks */
static void rs_step(void)
{
    if (rs.req) {
        const int32_t r = rfm1_fs_poll(rs.req);
        if (r == FS_PENDING)
            return;
        rs.req = 0;
        rs_result(r);
        if (rs.phase != RS_IDLE)
            rs_next();
    } else if (rs.err) {                   /* a request refused at once */
        const int32_t r = rs.err;
        rs.err = 0;
        rs_result(r);
        if (rs.phase != RS_IDLE)
            rs_next();
    } else if (rs.phase == RS_IDLE) {
        const int32_t c = rfm1_fs_changed();
        if (rs.started && c == rs.seen && !rs.want)
            return;
        rs.started = 1;
        rs.seen = c;
        rs.want = 0;
        rs.phase = RS_LIST;
        rs.dir = 0;
        rs.list_at = 0;
        rs.nf = rs.fi = rs.nkeep = 0;
        rs.written = rs.deleted = rs.skipped = 0;
        rs_next();
    }
}

static void rp_folder_reload(void)
{
    rs.want = 1;
    rs.said = 1;                           /* (asked: the outcome is said, no folder too) */
    app_say("FOLDER", "READING");
}

/* ---------------------------------------------------------------- the hooks (plat_glue.h) --- */
#define FM1CORE_PL_NAME APP_NAME
#define FM1CORE_PL_UI_FRAME() app_frame()
#define FM1CORE_PL_UI_INPUT() app_input_only()
#define FM1CORE_PL_DIM_LEVEL (fm1core_fb_dim_lo ? FM1P_LED_FAINT : FM1P_LED_DIM)   /* the glow: DIM LO / HI */
#define FM1CORE_PL_DIM_ON 1               /* (plat_lights fills the dim plane only while glowing) */
#define FM1CORE_PL_MID_PLANE fm1_led_mid  /* plat_lights' mid level */
#define FM1CORE_PL_PALETTE APP_PALETTE
#define FM1CORE_PL_DEFAULT_MASTER 512     /* -> master_pos 2048, plat_fm1.c's (the gain APP_GAIN(2048)) */

static void fm1core_fb_sense_tick(void)  /* the keys' readings, while plat_sense is on (the converter is the scan's) */
{
#ifdef APP_KEY_SENSE
    if (fm1_an.con)
        app_host_tick();
#endif
}
#ifdef APP_KEY_SENSE
#define FM1CORE_PL_TICK_HOOK() fm1core_fb_sense_tick()
/* the keys' velocity and pressure (APP_TOUCH): the panel's key_vel / key_lean become the readings the scan would hand
 * the app, through lib/touch.c's own host model (as core_frasil.c) */
extern fm1panel_t fm1core_pl_panel;
static uint8_t fm1core_fb_vel[NKEYS], fm1core_fb_lean[NKEYS];
static void fm1core_fb_keys(uint32_t down, uint32_t up, uint32_t held)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        const uint8_t lean = fm1p_lean(&fm1core_pl_panel, k);
        if ((down >> k) & 1u) {
            fm1core_fb_vel[k] = fm1p_vel(&fm1core_pl_panel, k, 100);   /* (none given: 100, the emulator's) */
            fm1core_fb_lean[k] = lean;
            app_host_key(k, fm1core_fb_vel[k], lean);
        } else if ((up >> k) & 1u) {
            app_host_key(k, 0, 0);
        } else if (((held >> k) & 1u) && lean != fm1core_fb_lean[k]) {
            fm1core_fb_lean[k] = lean;
            app_host_key(k, fm1core_fb_vel[k], lean);
        }
    }
}
#define FM1CORE_PL_KEY_HOOK(down, up, held) fm1core_fb_keys(down, up, held)
#endif

static void fm1core_pl_flash_boot(void) {}   /* (no backup and restore in Ripple: nothing to finish) */
static void fm1core_pl_app_boot(void)    /* base/device/main_fm1.c fm1_main */
{
    rp_builtin_fill();
    memset(&rs, 0, sizeof rs);            /* (a sync at power-on: the folder read again) */
    app_init();                           /* the settings; the disk checked (an empty one if it does not read) */
    radio_boot_note();
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                      /* (no radio in this build: nothing) */
#ifdef APP_BOOT
    app_boot();
#endif
}
static void fm1core_pl_ui_start(void)
{
    lcd_fill(0, 0, 240, 240, 0);
    app_ui_init();
}
static void fm1core_pl_render(int32_t *lr, uint32_t n)   /* main_fm1.c render_block (no USB audio) */
{
    app_render(lr, n, APP_GAIN(master_pos));
}
static void fm1core_pl_master(int32_t knob)   /* main_fm1.c: MASTER's position 0..4092 */
{
    fm1core_fb_master_adc = knob / 16;
    if (!fm1_an.con)
        fm1_an.master = (uint16_t)fm1core_fb_master_adc;
    master_pos = remote.master >= 0 ? (uint32_t)remote.master : (uint32_t)(knob / 4);
}
static void fm1core_pl_loop(void)        /* each pass, before the frame: the app's loop, Lua's budget, the folder */
{
    ble_poll();
    app_loop();
    rp_lua_budget();
    rs_step();
}
static void fm1core_pl_uboot_say(uint32_t left)
{
    if (left) {
        char d[4] = {(char)('0' + left), '.', '.', 0};
        app_say("UPDATE MODE IN", d);
    } else {
        app_say("UPDATE MODE", "CANCELLED");
    }
}
/* the lazy save: nothing waits (the settings are stored when a droplet starts, the disk's files when written) */
static int fm1core_pl_sync(void) { return 1; }
/* ui_info: the screen in words (the hosts' and rfm1_run's evidence): the mode, the droplet, the list (yours marked
 * '*'), the folder's last sync, the console's last line, the error */
static void fm1core_pl_info(char *buf, uint32_t n)
{
    static const char *const MODE[] = {"list", "run", "error", "disk"};
    uint32_t at;
    int i, mine = 0;
    char con_last[44];
    for (i = 0; i < ui.n; i++)
        mine += ui.mine[i];
    {   /* the newest line printed (a line still open, else the one before) */
        const char *l = con[con_n % CON_LINES];
        if (!l[0] && con_n)
            l = con[(con_n - 1u) % CON_LINES];
        snprintf(con_last, sizeof con_last, "%s", l);
    }
    at = (uint32_t)snprintf(buf, n, "mode %s%s%s; folder %s (%u syncs, %u+%u droplets/lib, %u written, %u deleted, "
                            "%u skipped); lua stopped %u; con \"%s\"; err \"%s\"; list %d (%d yours):",
                            MODE[ui.mode & 3u], ui.mode == MODE_RUN ? ":" : "", ui.mode == MODE_RUN ? run_name : "",
                            rs.last == 1 ? "ok" : rs.last == 0 ? "-" : rs.last == FS_ENOFOLDER ? "none" : "error",
                            (unsigned)rs.syncs, (unsigned)rs.nfiles[0], (unsigned)rs.nfiles[1], (unsigned)rs.written,
                            (unsigned)rs.deleted, (unsigned)rs.skipped, (unsigned)rp_lua_stopped(), con_last,
                            lr_error(), ui.n, mine);
    for (i = 0; i < ui.n && at + 2u < n; i++)
        at += (uint32_t)snprintf(buf + at, n - at, " %.*s%s", (int)strlen(ui.names[i]) - 4, ui.names[i],
                                 ui.mine[i] ? "*" : "");
}

#include "../plat/plat_glue.h"
#include "exit_end.h"
