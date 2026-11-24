/* SPDX-License-Identifier: GPL-3.0-only */
/* rfm1_run: an .rfm1 file (docs/RFM1-FORMAT.md, version 1) booted and played in wasm3 v0.5.0 (third_party/wasm3), as a
 * host would run it: the imports of section 3, the checks of section 7, a fresh flash, the native ABI's device clock
 * (fm1core.h: tick once per device ms, frame every 15 ms else idle, audio per 128-frame block due at 44.1 kHz).
 *
 *   rfm1_run [options] <file.rfm1>
 *     --seconds S    the chord held for S device seconds (default 5)
 *     --settle MS    device ms after init before the chord (default 4000)
 *     --keys K,K,..  the chord's key numbers (default 9,13,16: D4 F#4 A4, as core_bench / core_conformance); "none":
 *                    no key at all (the run keeps its timing; a takeover's scenario: with --min-rms 0)
 *     --vel V        the chord's velocity, key_vel (default 100; 0: none given, the core's default)
 *     --lean L       the chord's aftertouch, key_lean 0..100 % (default 0)
 *     --tap B,B,..   buttons tapped 1 s before the chord, by label (fm1core.h FM1P_B_*: PLAY 10): a firmware that
 *                    starts silent until PLAY (Niagara Drift's drones)
 *     --sysex FILE   a .syx file sent over midi_in from settle - 800 ms (a sample bank, a preset); the chord waits
 *                    until it is all in, then 300 ms
 *     --wav FILE     the whole run's audio as a 44.1 kHz 16-bit stereo WAV
 *     --min-rms X    the chord's RMS below this (full scale 1.0) fails as silent (default 0.0005)
 *     --verbose      the firmware's stdout / stderr through
 *     --midi-out F   the core's MIDI out (midi_out_take drained every device ms, SysEx replies included) as raw MIDI
 *                    bytes in F (a .syx for SysEx); without it the replies are drained only while --sysex sends
 *     --folder DIR   a Fantasy CFW's shared folder (docs/VIRTUAL-CARTS.md 3-4): rfm1_host_1 served from DIR, a RemoteFM1
 *                    root (/audio -> DIR/audio, /code -> DIR/code/<id>, /data -> DIR/data/<id>), as the manifest's
 *                    "folders" allow; "..", absolute escapes and symlinks out refused, writes only in /data; each
 *                    request completed one device ms later (between two calls, as a host does: the cart polls).
 *                    Without it the imports answer FS_ENOFOLDER
 *     --press B@MS[+D]  button B (its label: PLAY, OCT+, HOME ..) held from device ms MS for D ms (default 60);
 *                    repeat for more
 *     --info-at MS   ui_info printed at device ms MS (repeat)
 *     --turn R@MS[:N] knob R (SELECT ALGO PRESETS K1..K4) turned N detents (default 1, - anticlockwise) at device ms MS
 *     --readouts     each new readout (rd1, docs/RFM1.md "Readouts (rd1)": seq, control, kind, bar, title, value) and
 *                    the knobs' live labels when their epoch moves, after each frame
 *     --expect-readout S  (implies --readouts) fail unless a readout, as `ctl C title = value`, contains S, each
 *                    '_' in S a space (repeat; e.g. ctl_3_E5_CUT_=_412_Hz)
 *     --expect-ui S  fail unless the final ui_info contains S (repeat)
 *     --flash-in F   boot from this flash image (a save) instead of a fresh flash
 *     --flash-out F  the flash image after the run (flash_sync first): a save
 *     --screen F     the screen at the end as a PPM image
 *     --profile      the time spent in each export (tick, frame, idle, audio, ..): its share of the run, the mean and
 *                    the worst single call, and the worst device ms (tick + frame or idle + its audio blocks)
 *     --fprofile N   (a build with -DRFM1_FPROF and the shadow-stack patch, tools/rfm1_run/wasm3_fprof.patch) the N
 *                    hottest wasm functions by samples of a 0.1 ms profiling timer, self and inclusive: an .rfm1 built
 *                    with -DFM1_RFM1_NAMES=ON shows their names
 *     --helpers      docs/HOST-HELPERS.md 3: a module export named as a helper (strlen) runs natively here, bound
 *                    before the module is compiled (the module's own code is the fallback of a host without this)
 *     --frame-ms N   frame every N device ms (default 15; RemoteFM1's iOS app draws an .rfm1 at 30 fps: 33)
 *     --describe     no run: the descriptor as JSON (stdout, or --out FILE); works on a bare module (no manifest):
 *                    fm1_add_rfm1 makes the manifest from it
 *     --labels       (with --describe) also the live labels before init (docs/DYNAMIC-LABELS.md: rfm1_labels_epoch,
 *                    then the 49 rfm1_label slots, null = not given): "labels_epoch", "labels"
 *     --audio-in F   a stand-in for a host's live input (docs/VIRTUAL-CARTS.md 8, ain1): the WAV F (PCM 16 / 24 /
 *                    32-bit or float, mono arrives as L = R; played at 44.1 kHz whatever its rate) fed through
 *                    rfm1_audio_in immediately before each rfm1_audio, the same frames, looped; rfm1_audio_in_info(1,
 *                    latency) first. Only when the manifest lists ain1 in "wants" or "needs" and the module exports
 *                    rfm1_audio_in, as a host does (else said and ignored)
 *     --audio-in-once        the WAV once, then no more rfm1_audio_in calls (silence, state stays 1)
 *     --audio-in-latency N   the latency_frames given to rfm1_audio_in_info (default 0, unknown)
 *     --audio-in-off-at MS   at device ms MS: rfm1_audio_in_info(0, 0) (the input off), no more rfm1_audio_in
 *     more output channels (aout1, RemoteFM1's RFM1-FORMAT.md section 9): a module that exports rfm1_audio_out and
 *                    says 4..10 channels (rfm1_audio_out_info, after init) is given them as a host does: a zeroed
 *                    buffer through rfm1_audio_out right before each rfm1_audio, the same frames. Each channel's peak
 *                    and RMS over the run are printed (stderr)
 *     --no-aout      a host that does not take them: no rfm1_audio_out calls
 *     --aout-wav F   all the channels (the master on 1 and 2, then 3..N) as an N-channel WAV
 *     --expect-aout N   fail unless the module gives N channels and channels 3..N sounded (some RMS above --min-rms)
 *     the Move takeover (tk1, docs/RFM1-TAKEOVER.md): a module that exports rfm1_tk has its record checked after init
 *                    (version 1, size 1468, a NUL-terminated name), always; a bad one fails the run
 *     engine parameters (ep1, docs/RFM1.md "Engine parameters (ep1)"; engine-params/SPEC.md):
 *     --ep-info      no run: boot, init, rfm1_ep(), then the record's header as JSON (rfm1_check --run); with --out FILE
 *                    the description blob (EP1D) written there
 *     --ep           the description printed after init, then every change of the record after each call: the layers
 *                    ("ep-layers@MS: L:engine/channel"), the values ("ep-values@MS: layer L pid=v .."), the cells
 *     --ep-set L:PID=V@MS   rfm1_ep_set(SET, L, PID, V) before the tick of device ms MS (its answer printed)
 *     --ep-watch L:P@MS     rfm1_ep_watch(L, P) (P: a page index, -1 none)
 *     --ep-lock L:PID=V@MS, --ep-free L:PID@MS, --ep-free-all L@MS (255: every layer)   LOCK, FREE, FREE_ALL
 *     --midi HEX@MS  a three-byte MIDI message (B04A40: CC 74 = 64 on channel 1) into rfm1_midi_in at device ms MS
 *     --tk-info      no run: boot, init, then the record's header as JSON (version, size, name, claims; rfm1_check)
 *     --tk           drive the takeover as a Move host does: rfm1_tk_state(1, the record's claims, flags) at --tk-at,
 *                    the --tk-ev events through rfm1_tk_input (only the claimed controls'; the others are printed
 *                    "tk-skip"), the record read after each rfm1_tk_input and rfm1_frame: printed to stdout as
 *                    "tk-claims@MS: 0x..", "tk-screen@MS: seq N", "tk-led@MS: I R G B" (each changed LED),
 *                    "tk-say@MS: interrupt F \"..\"", "tk-state@MS: on|off claims 0x.."; claims of 0 close it
 *     --tk-at MS     when the takeover is shown (default 100)
 *     --tk-off-at MS when it is left: the release of everything held first, then rfm1_tk_state(0, ..)
 *     --tk-speech    the screen reader on (flags bit 0)
 *     --tk-ev SPEC   a Move surface event (repeat), at a device ms; D: held for D ms (default 100):
 *                      pad:N@MS[+D]       pad N (0..31, bottom-left first: note 68 + N), velocity 100
 *                      step:N@MS[+D]      step button N (0..15: note 16 + N)
 *                      touch:N@MS[+D]     a touch note (0..7 the knobs, 8 volume, 9 the jog wheel)
 *                      note:N@MS[+D]      any note
 *                      button:B@MS[+D]    a button by CC number or name (shift menu back capture down up undo loop
 *                                         copy left right mute delete play rec sample track1..track4 jogclick):
 *                                         CC 127, then 0
 *                      knob:K@MS:DELTA    knob K (1..8: CC 70 + K; 9 the volume knob, CC 79; 0 the jog wheel, CC
 *                                         14) turned by DELTA (relative: 1..63, 127 = -1)
 *                      cc:N@MS:V          any CC with value V
 *                      raw:XXXXXXXX@MS    a packet as hex (byte 0 the CIN, little-endian)
 *     --tk-script F  more events, one SPEC per line or space-separated ('#' starts a comment)
 *     --tk-screen F  the takeover's screen at the end as a PBM (P1, text) image; "-": to stdout, after the run
 *     --tk-leds      every LED of the record at the end, "tk-led-end: I R G B" (92 lines, after the run): the state,
 *                    where tk-led@ shows only the changes since init
 *
 * Prints one line: id version, the chord's RMS and peak, LCD writes, x real time, load and boot times. Exits 0 when
 * the module loaded, its layout and manifest agree, it booted, drew and sounded, and never trapped or halted; 1 on a
 * failure (said on stderr), 2 on a usage or file error. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fm1core.h"
#include "rfm1_move.h"
#include "wasm3.h"
#include "m3_env.h"                    /* (--helpers: IM3Function's module, LinkRawFunction) */

/* ------------------------------------------------------------------------------------------------ the exports --- */
enum {
    F_INITIALIZE, F_VERSION, F_GET, F_LAYOUT, F_ALLOC, F_FREE, F_OPTIONS, F_BOOT_OPTIONS, F_INIT, F_TICK, F_FRAME,
    F_IDLE, F_AUDIO, F_MIDI_IN, F_MIDI_OUT_TAKE, F_HALTED, F_SHUTDOWN, F_FLASH_STAGE, F_FLASH, F_FLASH_DIRTY,
    F_FLASH_SYNC, F_STATS, F_UI_INFO,
    F_AUDIO_IN, F_AUDIO_IN_INFO,           /* (optional: docs/VIRTUAL-CARTS.md 8, a cart with ain1) */
    F_LABELS_EPOCH, F_LABEL,               /* (optional: docs/DYNAMIC-LABELS.md) */
    F_AUDIO_OUT_INFO, F_AUDIO_OUT,         /* (optional: aout1, more output channels) */
    F_READOUT,                             /* (optional: rd1, docs/RFM1.md "Readouts (rd1)") */
    F_TK, F_TK_INPUT, F_TK_STATE,          /* (optional: tk1, docs/RFM1-TAKEOVER.md) */
    F_EP, F_EP_SET, F_EP_WATCH,            /* (optional: ep1, docs/RFM1.md "Engine parameters (ep1)") */
    F_N
};
static const struct {
    const char *name;
    unsigned nargs, nret;
} FN[F_N] = {
    {"_initialize", 0, 0},     {"rfm1_version", 0, 1},       {"rfm1_get", 1, 1},          {"rfm1_layout", 1, 1},
    {"rfm1_alloc", 1, 1},      {"rfm1_free", 1, 0},          {"rfm1_options", 4, 0},      {"rfm1_boot_options", 3, 1},
    {"rfm1_init", 1, 0},       {"rfm1_tick", 1, 0},          {"rfm1_frame", 0, 0},        {"rfm1_idle", 0, 0},
    {"rfm1_audio", 2, 0},      {"rfm1_midi_in", 1, 1},       {"rfm1_midi_out_take", 1, 1}, {"rfm1_halted", 0, 1},
    {"rfm1_shutdown", 0, 0},   {"rfm1_flash_stage", 2, 1},   {"rfm1_flash", 0, 1},        {"rfm1_flash_dirty", 0, 1},
    {"rfm1_flash_sync", 0, 1}, {"rfm1_stats", 2, 0},         {"rfm1_ui_info", 2, 0},
    {"rfm1_audio_in", 2, 0},   {"rfm1_audio_in_info", 2, 0},
    {"rfm1_labels_epoch", 0, 1}, {"rfm1_label", 1, 1},
    {"rfm1_audio_out_info", 0, 1}, {"rfm1_audio_out", 2, 0},
    {"rfm1_readout", 0, 1},
    {"rfm1_tk", 0, 1}, {"rfm1_tk_input", 1, 1}, {"rfm1_tk_state", 3, 0},
    {"rfm1_ep", 0, 1}, {"rfm1_ep_set", 4, 1}, {"rfm1_ep_watch", 2, 0},
};

static IM3Environment g_env;
static IM3Runtime g_rt;
static IM3Function g_fn[F_N];
static int g_verbose;
static int g_exit_code = -1;           /* proc_exit's code, -1 none */
static char g_err[512];                /* the first trap */
static const char *g_file = "";
static int g_profile;                  /* --profile */
static struct { uint64_t calls; double total, worst; } g_prof[F_N];

#ifdef RFM1_FPROF
/* --fprofile: wasm3's op_Entry pushes the function on this stack, op_Call / op_CallIndirect restore the depth (the
 * patch); a SIGPROF timer counts the top (self) and every distinct frame (inclusive) */
#include <signal.h>
#include <sys/time.h>
void *volatile rfm1_prof_stack[1024];
volatile int rfm1_prof_depth;
#define FP_SLOTS 16384
static struct { void *f; uint32_t self, incl; } g_fp[FP_SLOTS];
static volatile uint32_t g_fp_total, g_fp_outside, g_fp_on;
static uint32_t fp_slot(void *f)
{
    uint32_t h = (uint32_t)(((uintptr_t)f >> 4) * 2654435761u) & (FP_SLOTS - 1);
    while (g_fp[h].f && g_fp[h].f != f)
        h = (h + 1) & (FP_SLOTS - 1);
    g_fp[h].f = f;
    return h;
}
static void fp_signal(int sig)
{
    (void)sig;
    if (!g_fp_on)
        return;
    g_fp_total++;
    int d = rfm1_prof_depth;
    if (d > 1024)
        d = 1024;
    if (d <= 0) {
        g_fp_outside++;
        return;
    }
    g_fp[fp_slot(rfm1_prof_stack[d - 1])].self++;
    for (int i = 0; i < d; i++) {
        int seen = 0;
        for (int j = 0; j < i && !seen; j++)
            seen = rfm1_prof_stack[j] == rfm1_prof_stack[i];
        if (!seen)
            g_fp[fp_slot(rfm1_prof_stack[i])].incl++;
    }
}
static int fp_by_self(const void *a, const void *b)
{
    const uint32_t x = ((const uint32_t *)a)[0], y = ((const uint32_t *)b)[0];
    return x < y ? 1 : x > y ? -1 : 0;
}
static void fp_report(int top)
{
    static uint32_t idx[FP_SLOTS][2];
    int n = 0;
    for (int pass = 0; pass < 2; pass++) {
        n = 0;
        for (uint32_t i = 0; i < FP_SLOTS; i++)
            if (g_fp[i].f) {
                idx[n][0] = pass ? g_fp[i].incl : g_fp[i].self;
                idx[n][1] = i;
                n++;
            }
        qsort(idx, (size_t)n, sizeof idx[0], fp_by_self);
        printf("fprofile %s (%u samples, %u outside the module):\n", pass ? "inclusive" : "self", g_fp_total,
               g_fp_outside);
        for (int k = 0; k < n && k < top; k++) {
            const uint32_t i = idx[k][1];
            const char *nm = m3_GetFunctionName((IM3Function)g_fp[i].f);
            printf("  %6.2f%%  %s\n", g_fp_total ? 100.0 * idx[k][0] / g_fp_total : 0, nm ? nm : "?");
        }
    }
}
#endif

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

static int fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int fail(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "rfm1_run: %s: ", g_file);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the imports --- */
static uint8_t *mem(uint32_t *size)
{
    return m3_GetMemory(g_rt, size, 0);
}

static m3ApiRawFunction(w_fd_write)        /* stdout / stderr: the log (with --verbose); else EBADF */
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, fd);
    m3ApiGetArg(uint32_t, iovs);
    m3ApiGetArg(uint32_t, niov);
    m3ApiGetArg(uint32_t, nwritten);
    const uint32_t size = m3_GetMemorySize(runtime);
    uint8_t *m = (uint8_t *)_mem;
    uint32_t total = 0;
    (void)_ctx;
    if (fd != 1 && fd != 2)
        m3ApiReturn(8);                     /* EBADF */
    for (uint32_t i = 0; i < niov; i++) {
        uint32_t p, n;
        if ((uint64_t)iovs + 8u * i + 8u > size)
            m3ApiReturn(28);                /* EINVAL */
        memcpy(&p, m + iovs + 8u * i, 4);
        memcpy(&n, m + iovs + 8u * i + 4u, 4);
        if ((uint64_t)p + n > size)
            m3ApiReturn(28);
        if (g_verbose)
            fwrite(m + p, 1, n, fd == 2 ? stderr : stdout);
        total += n;
    }
    if ((uint64_t)nwritten + 4u <= size)
        memcpy(m + nwritten, &total, 4);
    m3ApiReturn(0);
}
static m3ApiRawFunction(w_fd_read)
{
    m3ApiReturnType(int32_t);
    (void)runtime; (void)_ctx; (void)_mem;
    m3ApiReturn(8);
}
static m3ApiRawFunction(w_fd_close)
{
    m3ApiReturnType(int32_t);
    (void)runtime; (void)_ctx; (void)_mem;
    m3ApiReturn(8);
}
static m3ApiRawFunction(w_fd_seek)
{
    m3ApiReturnType(int32_t);
    (void)runtime; (void)_ctx; (void)_mem;
    m3ApiReturn(8);
}
static m3ApiRawFunction(w_clock_time_get)  /* the host's monotonic clock, ns */
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, id);
    m3ApiGetArg(uint64_t, precision);
    m3ApiGetArg(uint32_t, out);
    (void)id; (void)precision; (void)_ctx;
    if ((uint64_t)out + 8u > m3_GetMemorySize(runtime))
        m3ApiReturn(28);
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    const uint64_t ns = (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
    memcpy((uint8_t *)_mem + out, &ns, 8);
    m3ApiReturn(0);
}
static m3ApiRawFunction(w_proc_exit)       /* the instance halts (halted = 0x100 | code) */
{
    m3ApiGetArg(uint32_t, code);
    (void)runtime; (void)_ctx; (void)_mem;
    g_exit_code = (int)(code & 0xFFu);
    m3ApiTrap(m3Err_trapExit);
}

/* -------------------------------------------------------------------------------------------- the helpers --- */
/* docs/HOST-HELPERS.md 3: C library functions the module exports by name, run natively with the same result. Each
 * checks every byte it reads against the memory's size and traps where the module's own code would */
M3Result LinkRawFunction(IM3Module io_module, IM3Function io_function, ccstr_t signature, const void *i_function,
                         const void *i_userdata);
static uint32_t g_helper_calls;
static m3ApiRawFunction(h_strlen)          /* size_t strlen(const char *) */
{
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, s);
    (void)_ctx;
    const uint32_t size = m3_GetMemorySize(runtime);
    const uint8_t *m = (const uint8_t *)_mem;
    g_helper_calls++;
    if (s >= size)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    const uint8_t *z = memchr(m + s, 0, size - s);
    if (!z)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    m3ApiReturn((uint32_t)(z - (m + s)));
}
static const struct {
    const char *name, *sig;
    M3RawCall f;
} HELPERS[] = {
    {"strlen", "i(i)", h_strlen},
};
/* each helper the module exports, bound before m3_CompileModule (so every call to it compiles to the native one);
 * the number bound */
static int helpers_bind(IM3Runtime rt)
{
    int n = 0;
    for (size_t i = 0; i < sizeof HELPERS / sizeof HELPERS[0]; i++) {
        IM3Function fn = NULL;
        if (m3_FindFunction(&fn, rt, HELPERS[i].name) || !fn || fn->import.moduleUtf8)
            continue;
        if (LinkRawFunction(fn->module, fn, HELPERS[i].sig, (const void *)HELPERS[i].f, NULL)) {
            fprintf(stderr, "rfm1_run: helper %s: not bound (its signature)\n", HELPERS[i].name);
            continue;
        }
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------------------------------- the folder (fs1) --- */
/* docs/VIRTUAL-CARTS.md section 4: rfm1_host_1, served from --folder. A request is checked when made (its path, the
 * access the manifest's "folders" gives, its buffer) and done one device ms later, before that ms's tick: between two
 * calls into the module, so the cart's polling is exercised. */
enum { FS_STAT = 1, FS_LIST, FS_READ, FS_WRITE, FS_DELETE, FS_MKDIR };
enum { FS_PENDING = -1, FS_ENOENT = -2, FS_EACCES = -3, FS_EIO = -4, FS_ENOSPC = -5, FS_EBUSY = -6, FS_ECANCELED = -7,
       FS_ENOFOLDER = -8 };
#define FS_INFLIGHT 32
#define FS_MAXLEN (1024 * 1024)
static struct {
    char root[PATH_MAX];               /* the folder (realpath), "" none */
    char id[64];                       /* the cart's id: code/<id>, data/<id> */
    char access[3][4];                 /* audio, code, data: "r", "rw" or "" (the manifest's "folders") */
    uint32_t now_ms;                   /* the device ms (requests made now are done at now + 1) */
    int32_t next_id;
    uint32_t served, refused, changed;
    struct {
        int32_t id, op, done, result;
        char path[PATH_MAX];           /* the host path */
        int area;
        int64_t off;
        uint32_t buf, len, due;
    } q[FS_INFLIGHT];
} g_fs;

static int32_t fs_errno(int e)
{
    return e == ENOENT || e == ENOTDIR ? FS_ENOENT : e == EACCES || e == EPERM || e == ELOOP ? FS_EACCES
         : e == ENOSPC ? FS_ENOSPC : FS_EIO;
}

/* the host path of a virtual one (UTF-8, not NUL-terminated); its area (0 audio, 1 code, 2 data); 0, or an error */
static int32_t fs_map(const uint8_t *v, uint32_t n, char *out, int *area)
{
    static const char *const AREA[3] = {"audio", "code", "data"};
    char seg[256];
    uint32_t i = 0, k;
    int a = -1;
    size_t at;
    if (!n || n >= 1024 || v[0] != '/')
        return FS_ENOENT;
    for (k = 0; k < n; k++)
        if (!v[k] || v[k] == '\\')
            return FS_EACCES;
    /* the first segment: the area */
    i = 1;
    for (k = 0; i < n && v[i] != '/' && k < sizeof seg - 1; i++)
        seg[k++] = (char)v[i];
    seg[k] = 0;
    for (int j = 0; j < 3; j++)
        if (!strcmp(seg, AREA[j]))
            a = j;
    if (a < 0)
        return FS_EACCES;
    if (a == 0)
        at = (size_t)snprintf(out, PATH_MAX, "%s/audio", g_fs.root);
    else
        at = (size_t)snprintf(out, PATH_MAX, "%s/%s/%s", g_fs.root, AREA[a], g_fs.id);
    /* the rest, segment by segment: no "..", no ".", no empty segment (a trailing '/' aside) */
    while (i < n) {
        i++;                                       /* (the '/') */
        if (i >= n)
            break;
        for (k = 0; i < n && v[i] != '/'; i++) {
            if (k >= sizeof seg - 1)
                return FS_EACCES;
            seg[k++] = (char)v[i];
        }
        seg[k] = 0;
        if (!k || !strcmp(seg, ".") || !strcmp(seg, ".."))
            return FS_EACCES;
        if (at + 1 + k >= PATH_MAX)
            return FS_EIO;
        at += (size_t)snprintf(out + at, PATH_MAX - at, "/%s", seg);
    }
    *area = a;
    return 0;
}

/* the path (or, for one that does not exist yet, its folder) resolves inside the area's base: no symlink out */
static int fs_inside(const char *path, int area)
{
    char base[PATH_MAX], rb[PATH_MAX], rp[PATH_MAX], tmp[PATH_MAX];
    if (area == 0)
        snprintf(base, sizeof base, "%s/audio", g_fs.root);
    else
        snprintf(base, sizeof base, "%s/%s/%s", g_fs.root, area == 1 ? "code" : "data", g_fs.id);
    if (!realpath(base, rb))
        return 0;
    {   /* the base itself inside the folder (a symlinked code/<id> pointing out is refused too) */
        size_t rl = strlen(g_fs.root);
        if (strncmp(rb, g_fs.root, rl) || (rb[rl] && rb[rl] != '/'))
            return 0;
    }
    snprintf(tmp, sizeof tmp, "%s", path);
    while (!realpath(tmp, rp)) {
        char *sl = strrchr(tmp, '/');
        if (errno != ENOENT || !sl || sl == tmp)
            return 0;
        *sl = 0;                                   /* (not there yet: its folder) */
    }
    size_t bl = strlen(rb);
    return !strncmp(rp, rb, bl) && (rp[bl] == 0 || rp[bl] == '/');
}

static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* the request done: its result, its bytes in the module's memory */
static int32_t fs_do(int op, const char *path, int area, int64_t off, uint32_t buf, uint32_t len)
{
    uint32_t msize = 0;
    uint8_t *m = mem(&msize);
    struct stat st;
    if (!fs_inside(path, area))
        return FS_EACCES;
    if ((uint64_t)buf + len > msize)
        return FS_EIO;
    switch (op) {
    case FS_STAT: {
        if (stat(path, &st))
            return fs_errno(errno);
        const uint64_t size = S_ISDIR(st.st_mode) ? 0 : (uint64_t)st.st_size;
        const uint32_t mt = (uint32_t)st.st_mtime, dir = S_ISDIR(st.st_mode) ? 1u : 0u;
        memcpy(m + buf, &size, 8);
        memcpy(m + buf + 8, &mt, 4);
        memcpy(m + buf + 12, &dir, 4);
        return 16;
    }
    case FS_LIST: {
        DIR *d = opendir(path);
        struct dirent *e;
        char **names = NULL;
        size_t nn = 0, cap = 0;
        int32_t written = 0;
        uint32_t at = 0;
        if (!d)
            return fs_errno(errno);
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || strlen(e->d_name) > 255)
                continue;                          /* (hidden files are not listed) */
            if (nn == cap) {
                cap = cap ? cap * 2 : 64;
                names = realloc(names, cap * sizeof *names);
            }
            names[nn++] = strdup(e->d_name);
        }
        closedir(d);
        qsort(names, nn, sizeof *names, name_cmp);
        for (size_t i = off < 0 ? nn : (size_t)off; i < nn; i++) {
            char full[PATH_MAX];
            const size_t nl = strlen(names[i]);
            if (at + 2u + nl > len)
                break;                             /* (an entry is never split) */
            snprintf(full, sizeof full, "%s/%s", path, names[i]);
            m[buf + at] = (uint8_t)(!stat(full, &st) && S_ISDIR(st.st_mode));
            m[buf + at + 1] = (uint8_t)nl;
            memcpy(m + buf + at + 2, names[i], nl);
            at += 2u + (uint32_t)nl;
            written++;
        }
        for (size_t i = 0; i < nn; i++)
            free(names[i]);
        free(names);
        return written;
    }
    case FS_READ: {
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            return fs_errno(errno);
        if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
            close(fd);
            return FS_EIO;
        }
        const ssize_t r = pread(fd, m + buf, len, off < 0 ? 0 : (off_t)off);
        const int e = errno;
        close(fd);
        return r < 0 ? fs_errno(e) : (int32_t)r;
    }
    case FS_WRITE: {
        char base[PATH_MAX];
        snprintf(base, sizeof base, "%s/data", g_fs.root);
        mkdir(base, 0755);
        snprintf(base, sizeof base, "%s/data/%s", g_fs.root, g_fs.id);
        mkdir(base, 0755);                         /* (the cart's own data folder, made at its first write) */
        int fd = open(path, O_WRONLY | O_CREAT | (off < 0 ? O_TRUNC : 0), 0644);
        if (fd < 0)
            return fs_errno(errno);
        const ssize_t w = pwrite(fd, m + buf, len, off < 0 ? 0 : (off_t)off);
        const int e = errno;
        close(fd);
        return w < 0 ? fs_errno(e) : (int32_t)w;
    }
    case FS_DELETE:
        if (stat(path, &st))
            return fs_errno(errno);
        if (S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path))
            return fs_errno(errno);
        return 0;
    case FS_MKDIR: {
        char base[PATH_MAX];                       /* (the cart's own data folder, as at its first write) */
        snprintf(base, sizeof base, "%s/data", g_fs.root);
        mkdir(base, 0755);
        snprintf(base, sizeof base, "%s/data/%s", g_fs.root, g_fs.id);
        mkdir(base, 0755);
    }
        if (mkdir(path, 0755) && !(errno == EEXIST && !stat(path, &st) && S_ISDIR(st.st_mode)))
            return fs_errno(errno);
        return 0;
    }
    return FS_EIO;
}

static m3ApiRawFunction(w_fs_request)     /* [i32 i32 i32 i64 i32 i32] -> [i32] */
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, op);
    m3ApiGetArg(uint32_t, path);
    m3ApiGetArg(int32_t, path_len);
    m3ApiGetArg(int64_t, off);
    m3ApiGetArg(uint32_t, buf);
    m3ApiGetArg(int32_t, len);
    (void)_ctx;
    const uint32_t size = m3_GetMemorySize(runtime);
    if (!g_fs.root[0])
        m3ApiReturn(FS_ENOFOLDER);
    int slot = -1, area = 0;
    for (int i = 0; i < FS_INFLIGHT; i++)
        if (!g_fs.q[i].id) {
            slot = i;
            break;
        }
    if (slot < 0) {
        g_fs.refused++;
        m3ApiReturn(FS_EBUSY);
    }
    if (op < FS_STAT || op > FS_MKDIR || path_len <= 0 || len < 0 || len > FS_MAXLEN ||
        (uint64_t)path + (uint32_t)path_len > size || (uint64_t)buf + (uint32_t)len > size ||
        (op == FS_STAT && len < 16)) {
        g_fs.refused++;
        m3ApiReturn(FS_EIO);
    }
    char host[PATH_MAX];
    int32_t rc = fs_map((const uint8_t *)_mem + path, (uint32_t)path_len, host, &area);
    if (!rc) {                                     /* the manifest's "folders": read where it says r, write where rw */
        const char *acc = g_fs.access[area];
        const int writes = op == FS_WRITE || op == FS_DELETE || op == FS_MKDIR;
        if (!acc[0] || (writes && (area != 2 || strcmp(acc, "rw"))))
            rc = FS_EACCES;
    }
    if (rc) {
        g_fs.refused++;
        m3ApiReturn(rc);
    }
    if (++g_fs.next_id <= 0)
        g_fs.next_id = 1;
    g_fs.q[slot].id = g_fs.next_id;
    g_fs.q[slot].op = op;
    g_fs.q[slot].done = 0;
    g_fs.q[slot].area = area;
    g_fs.q[slot].off = off;
    g_fs.q[slot].buf = buf;
    g_fs.q[slot].len = (uint32_t)len;
    g_fs.q[slot].due = g_fs.now_ms + 1u;
    snprintf(g_fs.q[slot].path, sizeof g_fs.q[slot].path, "%s", host);
    m3ApiReturn(g_fs.next_id);
}
static m3ApiRawFunction(w_fs_poll)        /* [i32] -> [i32] */
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    (void)runtime; (void)_ctx; (void)_mem;
    for (int i = 0; i < FS_INFLIGHT; i++)
        if (id > 0 && g_fs.q[i].id == id) {
            if (!g_fs.q[i].done)
                m3ApiReturn(FS_PENDING);
            g_fs.q[i].id = 0;                      /* (the result once: the id is free) */
            m3ApiReturn(g_fs.q[i].result);
        }
    m3ApiReturn(g_fs.root[0] ? FS_EIO : FS_ENOFOLDER);
}
static m3ApiRawFunction(w_fs_cancel)      /* [i32] -> [] */
{
    m3ApiGetArg(int32_t, id);
    (void)runtime; (void)_ctx; (void)_mem;
    for (int i = 0; i < FS_INFLIGHT; i++)
        if (id > 0 && g_fs.q[i].id == id && !g_fs.q[i].done) {
            g_fs.q[i].done = 1;
            g_fs.q[i].result = FS_ECANCELED;
        }
    m3ApiSuccess();
}
static m3ApiRawFunction(w_fs_changed)     /* [] -> [i32] */
{
    m3ApiReturnType(int32_t);
    (void)runtime; (void)_ctx; (void)_mem;
    m3ApiReturn((int32_t)g_fs.changed);
}
/* between two calls: the requests due by device ms `ms` done */
static void fs_complete(uint32_t ms)
{
    g_fs.now_ms = ms;
    for (int i = 0; i < FS_INFLIGHT; i++)
        if (g_fs.q[i].id && !g_fs.q[i].done && g_fs.q[i].due <= ms) {
            g_fs.q[i].result = fs_do(g_fs.q[i].op, g_fs.q[i].path, g_fs.q[i].area, g_fs.q[i].off, g_fs.q[i].buf,
                                     g_fs.q[i].len);
            g_fs.q[i].done = 1;
            g_fs.served++;
        }
}

/* ------------------------------------------------------------------------------------------------ calls --------- */
static uint32_t call(int f, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t a[4] = {a0, a1, a2, a3};
    const void *ap[4] = {&a[0], &a[1], &a[2], &a[3]};
    uint32_t r = 0;
    if (!g_fn[f])
        return 0;
    if (g_err[0])                           /* (after a trap: the instance is dead) */
        return 0;
    const double t0 = g_profile ? now_s() : 0;
#ifdef RFM1_FPROF
    rfm1_prof_depth = 0;
#endif
    M3Result res = m3_Call(g_fn[f], FN[f].nargs, ap);
#ifdef RFM1_FPROF
    rfm1_prof_depth = 0;
#endif
    if (g_profile) {
        const double dt = now_s() - t0;
        g_prof[f].calls++;
        g_prof[f].total += dt;
        if (dt > g_prof[f].worst)
            g_prof[f].worst = dt;
    }
    if (res) {
        M3ErrorInfo info;
        memset(&info, 0, sizeof info);
        m3_GetErrorInfo(g_rt, &info);
        snprintf(g_err, sizeof g_err, "%s: %s%s%s", FN[f].name, res, info.message ? " " : "",
                 info.message ? info.message : "");
        return 0;
    }
    if (FN[f].nret) {
        const void *rp[1] = {&r};
        if (m3_GetResults(g_fn[f], 1, rp))
            return 0;
    }
    return r;
}
#define C0(f) call(f, 0, 0, 0, 0)
#define C1(f, a) call(f, (uint32_t)(a), 0, 0, 0)
#define C2(f, a, b) call(f, (uint32_t)(a), (uint32_t)(b), 0, 0)

static int in_mem(uint32_t off, uint32_t n)
{
    uint32_t size = 0;
    mem(&size);
    return off && (uint64_t)off + n <= size;
}
static uint32_t rd32(uint32_t off)
{
    uint32_t v = 0;
    if (in_mem(off, 4))
        memcpy(&v, mem(NULL) + off, 4);
    return v;
}
static void wr32(uint32_t off, uint32_t v)
{
    if (in_mem(off, 4))
        memcpy(mem(NULL) + off, &v, 4);
}
static uint8_t rd8(uint32_t off) { return in_mem(off, 1) ? mem(NULL)[off] : 0; }
static void wr8(uint32_t off, uint8_t v)
{
    if (in_mem(off, 1))
        mem(NULL)[off] = v;
}
/* a C string in linear memory, NULL if the pointer is 0 or it runs off the end */
static const char *str(uint32_t off)
{
    uint32_t size = 0;
    const uint8_t *m = mem(&size);
    if (!off || off >= size)
        return NULL;
    const uint8_t *z = memchr(m + off, 0, size - off);
    return z ? (const char *)(m + off) : NULL;
}

/* ------------------------------------------------------------------------------------------------ the file ------ */
static int leb_u32(const uint8_t *p, size_t n, size_t *at, uint32_t *v)
{
    uint32_t r = 0;
    for (int sh = 0; sh < 35; sh += 7) {
        if (*at >= n)
            return 0;
        const uint8_t b = p[(*at)++];
        r |= (uint32_t)(b & 0x7Fu) << sh;
        if (!(b & 0x80u)) {
            *v = r;
            return 1;
        }
    }
    return 0;
}

/* the custom section's payload, or NULL */
static const uint8_t *custom_section(const uint8_t *p, size_t n, const char *want, uint32_t *len)
{
    size_t at = 8;
    while (at < n) {
        const uint8_t id = p[at++];
        uint32_t sz, nl;
        if (!leb_u32(p, n, &at, &sz) || at + sz > n)
            return NULL;
        const size_t end = at + sz;
        if (id == 0) {
            size_t q = at;
            if (leb_u32(p, end, &q, &nl) && q + nl <= end && nl == strlen(want) && !memcmp(p + q, want, nl)) {
                *len = (uint32_t)(end - (q + nl));
                return p + q + nl;
            }
        }
        at = end;
    }
    return NULL;
}

/* a JSON string member's value of a flat object (the manifest), decoded into out; 0 if absent or not a string */
static int json_get_str(const char *j, size_t n, const char *key, char *out, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const size_t pl = strlen(pat);
    for (size_t i = 0; i + pl <= n; i++) {
        if (memcmp(j + i, pat, pl))
            continue;
        size_t k = i + pl;
        while (k < n && (j[k] == ' ' || j[k] == '\t' || j[k] == '\n' || j[k] == '\r'))
            k++;
        if (k >= n || j[k] != ':')
            continue;
        k++;
        while (k < n && (j[k] == ' ' || j[k] == '\t' || j[k] == '\n' || j[k] == '\r'))
            k++;
        if (k >= n || j[k] != '"')
            return 0;
        k++;
        size_t o = 0;
        while (k < n && j[k] != '"') {
            unsigned c = (unsigned char)j[k++];
            if (c == '\\' && k < n) {
                const char e = j[k++];
                if (e == 'n') c = '\n';
                else if (e == 't') c = '\t';
                else if (e == 'r') c = '\r';
                else if (e == 'b') c = '\b';
                else if (e == 'f') c = '\f';
                else if (e == 'u' && k + 4 <= n) {
                    char h[5] = {j[k], j[k + 1], j[k + 2], j[k + 3], 0};
                    unsigned u = (unsigned)strtoul(h, NULL, 16);
                    k += 4;
                    if (u >= 0xD800 && u < 0xDC00 && k + 6 <= n && j[k] == '\\' && j[k + 1] == 'u') {
                        char h2[5] = {j[k + 2], j[k + 3], j[k + 4], j[k + 5], 0};
                        const unsigned lo = (unsigned)strtoul(h2, NULL, 16);
                        k += 6;
                        u = 0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u);
                    }
                    char b[4];
                    int bn = 0;
                    if (u < 0x80) b[bn++] = (char)u;
                    else if (u < 0x800) { b[bn++] = (char)(0xC0 | (u >> 6)); b[bn++] = (char)(0x80 | (u & 0x3F)); }
                    else if (u < 0x10000) { b[bn++] = (char)(0xE0 | (u >> 12)); b[bn++] = (char)(0x80 | ((u >> 6) & 0x3F)); b[bn++] = (char)(0x80 | (u & 0x3F)); }
                    else { b[bn++] = (char)(0xF0 | (u >> 18)); b[bn++] = (char)(0x80 | ((u >> 12) & 0x3F)); b[bn++] = (char)(0x80 | ((u >> 6) & 0x3F)); b[bn++] = (char)(0x80 | (u & 0x3F)); }
                    for (int q = 0; q < bn && o + 1 < cap; q++)
                        out[o++] = b[q];
                    continue;
                } else
                    c = (unsigned char)e;
            }
            if (o + 1 < cap)
                out[o++] = (char)c;
        }
        out[o] = 0;
        return 1;
    }
    return 0;
}

static void json_str(FILE *f, const char *s)
{
    if (!s) {
        fputs("null", f);
        return;
    }
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\')
            fprintf(f, "\\%c", *p);
        else if (*p == '\n')
            fputs("\\n", f);
        else if (*p == '\t')
            fputs("\\t", f);
        else if (*p == '\r')
            fputs("\\r", f);
        else if (*p < 0x20)
            fprintf(f, "\\u%04x", *p);
        else
            fputc(*p, f);
    }
    fputc('"', f);
}

/* ------------------------------------------------------------------------------------------------ the layout ---- */
/* fm1core_t as wasm32 lays it out: every pointer and function pointer 32 bits */
typedef struct {
    uint32_t abi_version, id, name, version, source_url, family, license, gated, flash_size;
    uint32_t button_names[FM1P_NB];
    uint32_t params, nparams, panel, keymap;
    uint32_t options, boot_options, init, tick, frame, idle, audio, midi_in, midi_out_take, dump, stats, ui_info,
        shutdown, halted, flash_stage, flash, flash_dirty, flash_sync, param_epoch, knob_target, credits;
} core_w32;

static const int32_t LAYOUT[] = {
    sizeof(fm1panel_t),
    offsetof(fm1panel_t, keys), offsetof(fm1panel_t, buttons), offsetof(fm1panel_t, keys_tap),
    offsetof(fm1panel_t, buttons_tap), offsetof(fm1panel_t, enc), offsetof(fm1panel_t, master),
    offsetof(fm1panel_t, key_vel), offsetof(fm1panel_t, key_lean), offsetof(fm1panel_t, led),
    offsetof(fm1panel_t, led_play_green), offsetof(fm1panel_t, lcd), offsetof(fm1panel_t, lcd_writes),
    offsetof(fm1panel_t, btn_id), offsetof(fm1panel_t, enc_id), offsetof(fm1panel_t, enc_dir),
    offsetof(fm1panel_t, ready),
    sizeof(core_w32),
    offsetof(core_w32, abi_version), offsetof(core_w32, id), offsetof(core_w32, name), offsetof(core_w32, version),
    offsetof(core_w32, source_url), offsetof(core_w32, family), offsetof(core_w32, license), offsetof(core_w32, gated),
    offsetof(core_w32, flash_size), offsetof(core_w32, button_names), offsetof(core_w32, params),
    offsetof(core_w32, nparams), offsetof(core_w32, panel), offsetof(core_w32, keymap), offsetof(core_w32, options),
    offsetof(core_w32, credits),
};
#define NLAYOUT ((int)(sizeof LAYOUT / sizeof LAYOUT[0]))
#define D(field) (desc + (uint32_t)offsetof(core_w32, field))
#define P(field) (panel + (uint32_t)offsetof(fm1panel_t, field))

/* ------------------------------------------------------------------------------------- the takeover (tk1) ------- */
/* docs/RFM1-TAKEOVER.md: the record at rfm1_tk(), read by its offsets (section 4.2), not through the shim's struct */
#define TK_VERSION 1u
#define TK_SIZE 1468u
#define TK_O_CLAIMS 4u
#define TK_O_SCR_SEQ 8u
#define TK_O_LED_SEQ 12u
#define TK_O_SAY_SEQ 16u
#define TK_O_SAY_FLAGS 20u
#define TK_O_NAME 24u
#define TK_O_SAY 40u
#define TK_O_SCREEN 168u
#define TK_O_LED 1192u
#define TK_NLED 92u
#define TK_MAXEV 1024
static struct {
    uint32_t rec;                      /* the record's address, 0 none */
    int drive, shown, speech;          /* --tk; shown now; --tk-speech */
    uint32_t claims, scr_seq, led_seq, say_seq;
    uint8_t led[TK_NLED][3];           /* the LEDs as last read */
    uint8_t held[2][128];              /* notes / CCs sent as held (released before rfm1_tk_state(0, ..)) */
    struct { uint32_t at, pkt; } ev[TK_MAXEV];
    int nev;
    uint32_t inputs, skipped;
} g_tk;

static uint8_t *read_file(const char *path, size_t *n);
static uint16_t rd16(uint32_t off)
{
    uint16_t v = 0;
    if (in_mem(off, 2))
        memcpy(&v, mem(NULL) + off, 2);
    return v;
}

/* the record's header after init: "" when it is good, else what is wrong */
static const char *tk_bad(uint32_t rec)
{
    static char why[128];
    if (!in_mem(rec, TK_SIZE))
        return "rfm1_tk() is not a record in memory";
    if (rd16(rec) != TK_VERSION || rd16(rec + 2) != TK_SIZE) {
        snprintf(why, sizeof why, "the record's version %u, size %u (1, 1468)", rd16(rec), rd16(rec + 2));
        return why;
    }
    if (!memchr(mem(NULL) + rec + TK_O_NAME, 0, 16))
        return "the record's name is not NUL-terminated within 16 bytes";
    for (const uint8_t *c = mem(NULL) + rec + TK_O_NAME; *c; c++)
        if (*c < 32 || *c > 126)
            return "the record's name is not printable ASCII";
    if (rd32(rec + TK_O_CLAIMS) & ~0xFFu)
        return "the record's claims have bits outside 0xFF";
    return "";
}

static void tk_add(uint32_t at, uint32_t pkt)
{
    if (g_tk.nev < TK_MAXEV) {
        g_tk.ev[g_tk.nev].at = at;
        g_tk.ev[g_tk.nev].pkt = pkt;
        g_tk.nev++;
    }
}
static uint32_t tk_pkt(uint8_t st, uint8_t d1, uint8_t d2)
{
    return (uint32_t)(st >> 4) | (uint32_t)st << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24;
}
/* one --tk-ev SPEC: 0, or -1 when it does not parse */
static int tk_parse(const char *spec)
{
    static const struct { const char *n; uint8_t cc; } BTN[] = {
        {"shift", 49}, {"menu", 50}, {"back", 51}, {"capture", 52}, {"down", 54}, {"up", 55}, {"undo", 56},
        {"loop", 58}, {"copy", 60}, {"left", 62}, {"right", 63}, {"mute", 88}, {"delete", 119}, {"play", 85},
        {"rec", 86}, {"sample", 118}, {"track1", 43}, {"track2", 42}, {"track3", 41}, {"track4", 40},
        {"jogclick", 3},
    };
    char kind[16], arg[24];
    const char *colon = strchr(spec, ':'), *at = colon ? strchr(colon, '@') : NULL;
    if (!colon || !at || (size_t)(colon - spec) >= sizeof kind || (size_t)(at - colon - 1) >= sizeof arg)
        return -1;
    memcpy(kind, spec, (size_t)(colon - spec));
    kind[colon - spec] = 0;
    memcpy(arg, colon + 1, (size_t)(at - colon - 1));
    arg[at - colon - 1] = 0;
    char *e;
    const uint32_t ms = (uint32_t)strtoul(at + 1, &e, 10);
    if (e == at + 1)
        return -1;
    uint32_t dur = 100, val = 0;
    int has_val = 0;
    if (*e == '+')
        dur = (uint32_t)strtoul(e + 1, &e, 10);
    if (*e == ':') {
        val = (uint32_t)strtol(e + 1, &e, 10) & 127u;
        has_val = 1;
    }
    if (*e)
        return -1;
    if (!strcmp(kind, "raw")) {
        tk_add(ms, (uint32_t)strtoul(arg, &e, 16));
        return *e || !*arg ? -1 : 0;
    }
    long n = -1;
    if (!strcmp(kind, "button")) {
        for (size_t k = 0; k < sizeof BTN / sizeof BTN[0]; k++)
            if (!strcmp(BTN[k].n, arg))
                n = BTN[k].cc;
    }
    if (n < 0) {
        n = strtol(arg, &e, 10);
        if (*e || !*arg || n < 0 || n > 127)
            return -1;
    }
    if (!strcmp(kind, "pad") || !strcmp(kind, "step") || !strcmp(kind, "touch") || !strcmp(kind, "note")) {
        const int base = kind[0] == 'p' ? 68 : kind[0] == 's' ? 16 : 0;
        if ((kind[0] == 'p' && n > 31) || (kind[0] == 's' && n > 15) || (kind[0] == 't' && n > 9) || has_val)
            return -1;
        tk_add(ms, tk_pkt(0x90, (uint8_t)(base + n), kind[0] == 't' ? 127 : 100));
        tk_add(ms + dur, tk_pkt(0x80, (uint8_t)(base + n), 0));
        return 0;
    }
    if (!strcmp(kind, "button")) {
        if (has_val)
            return -1;
        tk_add(ms, tk_pkt(0xB0, (uint8_t)n, 127));
        tk_add(ms + dur, tk_pkt(0xB0, (uint8_t)n, 0));
        return 0;
    }
    if (!strcmp(kind, "knob")) {
        if (n > 9 || !has_val)
            return -1;
        tk_add(ms, tk_pkt(0xB0, (uint8_t)(n == 0 ? 14 : n == 9 ? 79 : 70 + n), (uint8_t)val));
        return 0;
    }
    if (!strcmp(kind, "cc")) {
        if (!has_val)
            return -1;
        tk_add(ms, tk_pkt(0xB0, (uint8_t)n, (uint8_t)val));
        return 0;
    }
    return -1;
}
static int tk_script(const char *path)
{
    size_t n = 0;
    uint8_t *b = read_file(path, &n);
    if (!b)
        return -1;
    char *s = realloc(b, n + 1), *line, *save = NULL;
    if (!s) {
        free(b);
        return -1;
    }
    s[n] = 0;
    int bad = 0;
    for (line = strtok_r(s, "\n", &save); line && !bad; line = strtok_r(NULL, "\n", &save)) {
        char *h = strchr(line, '#'), *tok, *s2 = NULL;
        if (h)
            *h = 0;
        for (tok = strtok_r(line, " \t\r", &s2); tok && !bad; tok = strtok_r(NULL, " \t\r", &s2))
            if (tk_parse(tok)) {
                fprintf(stderr, "rfm1_run: --tk-script %s: \"%s\" does not parse\n", path, tok);
                bad = 1;
            }
    }
    free(s);
    return bad ? -1 : 0;
}
static void tk_sort(void)              /* by time; events at the same ms in the order given (insertion: stable) */
{
    for (int i = 1; i < g_tk.nev; i++)
        for (int j = i; j > 0 && g_tk.ev[j - 1].at > g_tk.ev[j].at; j--) {
            const uint32_t at = g_tk.ev[j].at, pkt = g_tk.ev[j].pkt;
            g_tk.ev[j] = g_tk.ev[j - 1];
            g_tk.ev[j - 1].at = at, g_tk.ev[j - 1].pkt = pkt;
        }
}

static void tk_poll(uint32_t ms)       /* the record after a call: what moved */
{
    const uint32_t rec = g_tk.rec;
    if (!rec || !in_mem(rec, TK_SIZE))
        return;
    const uint32_t claims = rd32(rec + TK_O_CLAIMS), ss = rd32(rec + TK_O_SCR_SEQ), ls = rd32(rec + TK_O_LED_SEQ),
                   ys = rd32(rec + TK_O_SAY_SEQ);
    if (claims != g_tk.claims) {
        g_tk.claims = claims;
        printf("tk-claims@%u: 0x%02x\n", ms, claims);
    }
    if (ss != g_tk.scr_seq) {
        g_tk.scr_seq = ss;
        printf("tk-screen@%u: seq %u\n", ms, ss);
    }
    if (ls != g_tk.led_seq) {
        g_tk.led_seq = ls;
        const uint8_t *l = mem(NULL) + rec + TK_O_LED;
        for (uint32_t i = 0; i < TK_NLED; i++)
            if (memcmp(g_tk.led[i], l + 3u * i, 3)) {
                memcpy(g_tk.led[i], l + 3u * i, 3);
                printf("tk-led@%u: %u %u %u %u\n", ms, i, l[3u * i], l[3u * i + 1], l[3u * i + 2]);
            }
    }
    if (ys != g_tk.say_seq) {
        char say[129];
        g_tk.say_seq = ys;
        memcpy(say, mem(NULL) + rec + TK_O_SAY, 128);
        say[128] = 0;
        printf("tk-say@%u: interrupt %u%s \"%s\"\n", ms, rd8(rec + TK_O_SAY_FLAGS) & 1u,
               g_tk.speech ? "" : " (speech off: a host would not say it)", say);
    }
}
static void tk_input(uint32_t ms, uint32_t pkt)
{
    C1(F_TK_INPUT, pkt);
    g_tk.inputs++;
    tk_poll(ms);
}
static void tk_off(uint32_t ms)        /* the release of everything held, then gone */
{
    for (int k = 0; k < 2; k++)
        for (int n = 0; n < 128; n++)
            if (g_tk.held[k][n]) {
                g_tk.held[k][n] = 0;
                const uint32_t pkt = k ? tk_pkt(0xB0, (uint8_t)n, 0) : tk_pkt(0x80, (uint8_t)n, 0);
                printf("tk-in@%u: %08x (release)\n", ms, pkt);
                tk_input(ms, pkt);
            }
    call(F_TK_STATE, 0, g_tk.claims, g_tk.speech ? 1u : 0u, 0);
    g_tk.shown = 0;
    printf("tk-state@%u: off\n", ms);
}
static void tk_event(uint32_t ms, uint32_t pkt)   /* a surface event: to the firmware when its part is claimed */
{
    const uint8_t b[4] = {(uint8_t)pkt, (uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
    const unsigned claim = rfm1_tk_claim_of(b);
    if (!g_tk.shown || !(claim & g_tk.claims)) {
        g_tk.skipped++;
        printf("tk-skip@%u: %08x (%s)\n", ms, pkt, !g_tk.shown ? "not shown" : claim ? "unclaimed" : "the host's");
        return;
    }
    const unsigned st = b[1] & 0xF0u;
    if (st == 0x90u || st == 0x80u)
        g_tk.held[0][b[2] & 127] = st == 0x90u && b[3];
    else if (st == 0xB0u && claim & (RFM1_TK_BUTTONS | RFM1_TK_TRACKS | RFM1_TK_JOG) && b[2] != RFM1_TK_CC_JOG)
        g_tk.held[1][b[2] & 127] = b[3] != 0;
    printf("tk-in@%u: %08x\n", ms, pkt);
    tk_input(ms, pkt);
}
/* each device ms, before its tick: shown at on_at, the events due, left at off_at; claims of 0 close it */
static void tk_ms(uint32_t ms, uint32_t on_at, uint32_t off_at, int *next)
{
    if (!g_tk.drive || !g_tk.rec)
        return;
    if (ms == on_at && !g_tk.shown) {
        g_tk.claims = rd32(g_tk.rec + TK_O_CLAIMS);
        if (!g_tk.claims)
            printf("tk-state@%u: not shown (claims 0)\n", ms);
        else {
            g_tk.shown = 1;
            printf("tk-state@%u: on claims 0x%02x%s\n", ms, g_tk.claims, g_tk.speech ? " speech" : "");
            call(F_TK_STATE, 1, g_tk.claims, g_tk.speech ? 1u : 0u, 0);
            tk_poll(ms);
        }
    }
    while (*next < g_tk.nev && g_tk.ev[*next].at <= ms) {
        tk_event(ms, g_tk.ev[*next].pkt);
        (*next)++;
        if (g_tk.shown && !g_tk.claims)
            tk_off(ms);
    }
    if (g_tk.shown && (ms == off_at || !g_tk.claims))
        tk_off(ms);
}
static void tk_pbm(FILE *o)            /* the screen, P1: 8 pages of 128 columns, bit 0 the top */
{
    const uint8_t *px = mem(NULL) + g_tk.rec + TK_O_SCREEN;
    fprintf(o, "P1\n128 64\n");
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 128; x++)
            fputc(px[(y >> 3) * 128 + x] >> (y & 7) & 1 ? '1' : '0', o);
        fputc('\n', o);
    }
}

/* ------------------------------------------------------------------------------------------------ the WAV ------- */
static void wav_header_n(FILE *f, uint32_t frames, uint16_t ch)
{
    const uint32_t data = frames * 2u * ch, rate = FM1P_FS, byterate = FM1P_FS * 2u * ch;
    const uint32_t riff = 36u + data, fmt = 16u;
    const uint16_t pcm = 1, align = (uint16_t)(2u * ch), bits = 16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byterate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}
static void wav_header(FILE *f, uint32_t frames) { wav_header_n(f, frames, 2); }

/* ------------------------------------------------------------------------------------------------ main ---------- */
static uint8_t *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = sz > 0 ? malloc((size_t)sz) : NULL;
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *n = b ? (size_t)sz : 0;
    return b;
}

/* the manifest lists name in its array member key ("wants": ["ain1"]): a flat object, so the array's span is enough */
static int manifest_lists(const uint8_t *man, uint32_t n, const char *key, const char *name)
{
    char pat[64], want[64];
    if (!man)
        return 0;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    snprintf(want, sizeof want, "\"%s\"", name);
    const char *j = (const char *)man, *end = j + n;
    for (const char *p = j; p + strlen(pat) <= end; p++) {
        if (memcmp(p, pat, strlen(pat)))
            continue;
        const char *q = p + strlen(pat);
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r' || *q == ':'))
            q++;
        if (q >= end || *q != '[')
            continue;
        const char *e = memchr(q, ']', (size_t)(end - q));
        if (!e)
            return 0;
        for (const char *r = q; r + strlen(want) <= e; r++)
            if (!memcmp(r, want, strlen(want)))
                return 1;
        return 0;
    }
    return 0;
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

/* --audio-in: a RIFF WAVE file as int16 stereo frames (malloc'd); NULL after saying why */
static int16_t *wav_read(const char *path, uint32_t *frames)
{
    size_t n = 0;
    uint8_t *b = read_file(path, &n);
    uint32_t fmt = 0, ch = 0, rate = 0, bits = 0, at = 12;
    const uint8_t *data = NULL;
    uint32_t dlen = 0;
    if (!b || n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) {
        fprintf(stderr, "rfm1_run: --audio-in %s: %s\n", path, b ? "not a RIFF WAVE file" : strerror(errno));
        free(b);
        return NULL;
    }
    while (at + 8 <= n) {
        const uint32_t len = le32(b + at + 4);
        const uint8_t *c = b + at + 8;
        if (!memcmp(b + at, "fmt ", 4) && len >= 16 && at + 8 + 16 <= n) {
            fmt = le16(c);
            ch = le16(c + 2);
            rate = le32(c + 4);
            bits = le16(c + 14);
            if (fmt == 0xFFFE && len >= 26 && at + 8 + 26 <= n)
                fmt = le16(c + 24);          /* WAVE_FORMAT_EXTENSIBLE: the sub-format's tag */
        } else if (!memcmp(b + at, "data", 4)) {
            data = c;
            dlen = len <= n - (at + 8) ? len : (uint32_t)(n - (at + 8));
            break;
        }
        at += 8 + len + (len & 1u);
    }
    const uint32_t bps = bits / 8;
    if (!data || !ch || !bps || !((fmt == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) {
        fprintf(stderr, "rfm1_run: --audio-in %s: no PCM 16 / 24 / 32-bit or float data\n", path);
        free(b);
        return NULL;
    }
    if (rate != FM1P_FS)
        fprintf(stderr, "rfm1_run: --audio-in %s: %u Hz, played at %u Hz\n", path, rate, FM1P_FS);
    const uint32_t nf = dlen / (bps * ch);
    int16_t *out = malloc((size_t)(nf ? nf : 1) * 4u);
    if (!out) {
        free(b);
        return NULL;
    }
    for (uint32_t i = 0; i < nf; i++)
        for (uint32_t k = 0; k < 2; k++) {
            const uint8_t *p = data + ((size_t)i * ch + (k < ch ? k : 0)) * bps;   /* (mono: L = R) */
            int32_t v;
            if (fmt == 3) {
                float f;
                memcpy(&f, p, 4);
                f = f > 1.0f ? 1.0f : f < -1.0f ? -1.0f : f;
                v = (int32_t)lrintf(f * 32767.0f);
            } else if (bits == 16)
                v = (int16_t)le16(p);
            else if (bits == 24)
                v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 16;
            else
                v = (int32_t)le32(p) >> 16;
            out[2 * i + k] = (int16_t)v;
        }
    free(b);
    *frames = nf;
    if (!nf)
        fprintf(stderr, "rfm1_run: --audio-in %s: no frames (silence)\n", path);
    return out;
}

/* ------------------------------------------------------------------- engine parameters (ep1) ----------------- */
/* engine-params/SPEC.md 3-5: the record rfm1_ep() gives (264 bytes), its description (EP1D); --ep prints the
 * description and every change of the layers, values and cells after each frame; --ep-set / --ep-watch call the
 * exports at a device ms, as a host's queued input */
#define EP_O_DSEQ 4
#define EP_O_DPTR 8
#define EP_O_DLEN 12
#define EP_O_LSEQ 16
#define EP_O_NLAY 20
#define EP_O_LAY 24
#define EP_O_VSEQ 56
#define EP_O_VPTR 60
#define EP_O_CSEQ 64
#define EP_O_WPAGE 68
#define EP_O_CELL 72
static struct {
    uint32_t rec, dseq, lseq, vseq, cseq;
    uint8_t *desc;                     /* a copy */
    uint32_t dlen, np, ne, ng, nl, nt;
    int16_t *val;                      /* the last seen, [nl][np] */
    int print;                         /* --ep */
    struct { int op, layer, pid, v; uint32_t at; } ev[256];
    int nev;
    int sets, bad_sets;
    double set_us_total, set_us_worst, frame_us_total, frame_us_worst;
    uint32_t frames;
} g_ep;
static uint16_t ep_u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static const uint8_t *ep_param(uint32_t i) { return g_ep.desc + 32 + 16 * g_ep.nl + 16 * g_ep.ne + 32 * g_ep.ng + 32 * i; }
static const char *ep_pname(uint32_t i) { return (const char *)ep_param(i) + 2; }
static void ep_take_desc(void)         /* the description copied (desc_seq moved) */
{
    const uint32_t ptr = rd32(g_ep.rec + EP_O_DPTR), len = rd32(g_ep.rec + EP_O_DLEN);
    free(g_ep.desc);
    g_ep.desc = NULL;
    g_ep.dlen = 0;
    if (!len || len > 65536 || !in_mem(ptr, len))
        return;
    g_ep.desc = malloc(len);
    memcpy(g_ep.desc, mem(NULL) + ptr, len);
    g_ep.dlen = len;
    g_ep.np = ep_u16(g_ep.desc + 8), g_ep.ne = g_ep.desc[10], g_ep.nl = g_ep.desc[11];
    g_ep.ng = ep_u16(g_ep.desc + 12), g_ep.nt = ep_u16(g_ep.desc + 14);
    free(g_ep.val);
    g_ep.val = malloc(sizeof(int16_t) * g_ep.np * g_ep.nl);
    for (uint32_t i = 0; i < g_ep.np * g_ep.nl; i++)
        g_ep.val[i] = (int16_t)0x7FFF;
}
static void ep_print_desc(void)
{
    const uint8_t *L = g_ep.desc + 32, *E = L + 16 * g_ep.nl, *G = E + 16 * g_ep.ne;
    printf("ep: description %u bytes: %u layers, %u engines, %u pages, %u params, %u tables\n", g_ep.dlen, g_ep.nl,
           g_ep.ne, g_ep.ng, g_ep.np, g_ep.nt);
    for (uint32_t i = 0; i < g_ep.nl; i++)
        printf("ep: layer %u \"%.11s\" flags %u\n", i, (const char *)L + 16 * i, L[16 * i + 11]);
    for (uint32_t e = 0; e < g_ep.ne; e++) {
        const uint8_t *q = E + 16 * e;
        printf("ep: engine %u id %u \"%.11s\" %u pages\n", e, ep_u16(q), (const char *)q + 2, q[13]);
        for (uint32_t k = 0; k < q[13]; k++) {
            const uint8_t *g = G + 32 * (ep_u16(q + 14) + k);
            printf("ep:   page %u id %u \"%.11s\":", ep_u16(q + 14) + k, g[0], (const char *)g + 1);
            for (int j = 0; j < 8; j++) {
                const uint16_t x = ep_u16(g + 12 + 2 * j);
                if (x == 0xFFFF)
                    printf(" -");
                else
                    printf(" %u:\"%.11s\"", ep_u16(ep_param(x)), ep_pname(x));
            }
            printf("\n");
        }
    }
}
static void ep_poll(uint32_t ms)       /* after a call: what moved */
{
    uint32_t s;
    if (!g_ep.rec)
        return;
    if ((s = rd32(g_ep.rec + EP_O_DSEQ)) != g_ep.dseq) {
        g_ep.dseq = s;
        ep_take_desc();
        if (g_ep.print && g_ep.desc)
            ep_print_desc();
        g_ep.lseq = g_ep.vseq = g_ep.cseq = 0;
    }
    if (!g_ep.desc)
        return;
    if ((s = rd32(g_ep.rec + EP_O_LSEQ)) != g_ep.lseq) {
        g_ep.lseq = s;
        if (g_ep.print) {
            printf("ep-layers@%u:", ms);
            for (uint32_t i = 0; i < g_ep.nl; i++)
                printf(" %u:%d/%d", i, rd8(g_ep.rec + EP_O_LAY + 4 * i) == 0xFF ? -1 : rd8(g_ep.rec + EP_O_LAY + 4 * i),
                       rd8(g_ep.rec + EP_O_LAY + 4 * i + 1) == 0xFF ? -1 : rd8(g_ep.rec + EP_O_LAY + 4 * i + 1));
            printf("\n");
        }
    }
    if ((s = rd32(g_ep.rec + EP_O_VSEQ)) != g_ep.vseq) {
        const uint32_t vp = rd32(g_ep.rec + EP_O_VPTR);
        g_ep.vseq = s;
        if (in_mem(vp, 2 * g_ep.np * g_ep.nl))
            for (uint32_t l = 0; l < g_ep.nl; l++) {
                int n = 0;
                for (uint32_t i = 0; i < g_ep.np; i++) {
                    int16_t v;
                    memcpy(&v, mem(NULL) + vp + 2 * (l * g_ep.np + i), 2);
                    const int16_t was = g_ep.val[l * g_ep.np + i];
                    if (v == was)
                        continue;
                    g_ep.val[l * g_ep.np + i] = v;
                    if (v == (int16_t)0x8000 && was == (int16_t)0x7FFF)
                        continue;                /* (the first time: only the values the layer's engine has) */
                    if (!g_ep.print)
                        continue;
                    if (!n++)
                        printf("ep-values@%u: layer %u", ms, l);
                    if (v == (int16_t)0x8000)
                        printf(" %u=-", ep_u16(ep_param(i)));
                    else
                        printf(" %u=%d", ep_u16(ep_param(i)), v);
                }
                if (n)
                    printf("\n");
            }
    }
    if ((s = rd32(g_ep.rec + EP_O_CSEQ)) != g_ep.cseq) {
        g_ep.cseq = s;
        if (g_ep.print) {
            printf("ep-cells@%u: page %d layer %u:", ms, (int16_t)rd16(g_ep.rec + EP_O_WPAGE), rd8(g_ep.rec + 70));
            for (int k = 0; k < 8; k++) {
                const uint32_t c = g_ep.rec + EP_O_CELL + 24u * (uint32_t)k;
                if (!rd16(c))
                    printf(" -");
                else
                    printf(" %u:\"%.11s\"=\"%.11s\"", rd16(c), (const char *)mem(NULL) + c + 2,
                           (const char *)mem(NULL) + c + 13);
            }
            printf("\n");
        }
    }
}
/* --ep-set / --ep-lock L:PID=V@MS, --ep-free L:PID@MS, --ep-free-all L@MS, --ep-watch L:P@MS (P -1: none); op: the
 * export's 1 SET 2 LOCK 3 FREE 4 FREE_ALL, 10 the watch */
static int ep_parse(const char *a, int op)
{
    char *e;
    if (g_ep.nev >= 256)
        return 0;
    g_ep.ev[g_ep.nev].op = op;
    g_ep.ev[g_ep.nev].layer = (int)strtol(a, &e, 10);
    g_ep.ev[g_ep.nev].pid = 0;
    g_ep.ev[g_ep.nev].v = 0;
    if (op != 4) {
        if (*e != ':')
            return 0;
        g_ep.ev[g_ep.nev].pid = (int)strtol(e + 1, &e, 10);
    }
    if (op == 1 || op == 2) {
        if (*e != '=')
            return 0;
        g_ep.ev[g_ep.nev].v = (int)strtol(e + 1, &e, 10);
    }
    if (*e != '@')
        return 0;
    g_ep.ev[g_ep.nev].at = (uint32_t)strtoul(e + 1, &e, 10);
    g_ep.nev++;
    return !*e;
}
static struct { uint32_t pkt, at; } g_midi[64];   /* --midi */
static int g_nmidi;
static void ep_ms(uint32_t ms)         /* the calls due at ms, before its tick (and --midi's messages) */
{
    for (int k = 0; k < g_nmidi; k++)
        if (g_midi[k].at == ms) {
            (void)call(F_MIDI_IN, g_midi[k].pkt, 0, 0, 0);
            printf("midi@%u: %06x\n", ms, g_midi[k].pkt >> 8);
        }
    for (int k = 0; k < g_ep.nev; k++)
        if (g_ep.ev[k].at == ms) {
            if (g_ep.ev[k].op <= 4) {
                static const char *const OPN[5] = {"", "set", "lock", "free", "free-all"};
                const double t0 = now_s();
                const int32_t r = (int32_t)call(F_EP_SET, (uint32_t)g_ep.ev[k].op, (uint32_t)g_ep.ev[k].layer,
                                                (uint32_t)g_ep.ev[k].pid, (uint32_t)g_ep.ev[k].v);
                const double us = (now_s() - t0) * 1e6;
                g_ep.sets++;
                g_ep.set_us_total += us;
                if (us > g_ep.set_us_worst)
                    g_ep.set_us_worst = us;
                if (r)
                    g_ep.bad_sets++;
                printf("ep-%s@%u: layer %d pid %d = %d -> %d\n", OPN[g_ep.ev[k].op], ms, g_ep.ev[k].layer, g_ep.ev[k].pid,
                       g_ep.ev[k].v, r);
            } else {
                call(F_EP_WATCH, (uint32_t)g_ep.ev[k].layer, (uint32_t)g_ep.ev[k].pid, 0, 0);
                printf("ep-watch@%u: layer %d page %d\n", ms, g_ep.ev[k].layer, g_ep.ev[k].pid);
            }
            ep_poll(ms);
        }
}

static void usage(void)
{
    fputs("usage: rfm1_run [--seconds S] [--settle MS] [--keys K,K,..] [--vel V] [--lean L] [--tap B,..] [--sysex FILE] [--wav FILE]\n"
          "                [--min-rms X] [--folder DIR] [--press B@MS[+D]].. [--info-at MS].. [--expect-ui S]..\n"
          "                [--flash-in FILE] [--flash-out FILE] [--screen FILE.ppm] [--midi-out FILE]\n"
          "                [--profile] [--frame-ms N] [--helpers]\n"
          "                [--audio-in FILE.wav [--audio-in-once] [--audio-in-latency N] [--audio-in-off-at MS]]\n"
          "                [--no-aout] [--aout-wav FILE] [--expect-aout N]\n"
          "                [--turn ROLE@MS[:N]].. [--readouts] [--expect-readout S].. [--verbose] [--describe [--labels] [--out FILE]]\n"
          "                [--tk-info] [--tk [--tk-at MS] [--tk-off-at MS] [--tk-speech] [--tk-ev SPEC].. [--tk-script FILE]\n"
          "                 [--tk-screen FILE.pbm|-] [--tk-leds]]\n"
          "                [--ep-info] [--ep] [--ep-set L:PID=V@MS].. [--ep-lock L:PID=V@MS].. [--ep-free L:PID@MS]..\n"
          "                [--ep-free-all L@MS].. [--ep-watch L:P@MS].. [--midi HEX@MS].. <file.rfm1>\n", stderr);
}

int main(int argc, char **argv)
{
    double seconds = 5.0, min_rms = 0.0005;
    uint32_t settle_ms = 4000, frame_ms = 15;
    int helpers = 0, nhelpers = 0;
#ifdef RFM1_FPROF
    int fprofile = 0;
#endif
    int describe = 0, labels = 0, readouts = 0, nturn = 0;
    struct { int r, n; uint32_t at; } turn[256];
    uint32_t rd_ptr = 0, rd_last = 0, lb_last = 0;
    const char *expect_rd[16];                   /* --expect-readout */
    int nexpect_rd = 0, expect_rd_seen[16] = {0};
    const char *out_path = NULL, *wav_path = NULL, *sysex_path = NULL, *path = NULL, *midi_out_path = NULL;
    int keys[FM1P_NKEY], nkeys = 0, vel = 100, lean = 0, no_keys = 0;
    uint32_t taps = 0;                           /* (label bits: --tap) */
    static const char *const LABEL[FM1P_NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                               "PLAY", "REC", "OCT-", "OCT+"};
    struct { int b; uint32_t at, dur; } press[256];
    int npress = 0, ninfo = 0, nexpect = 0;
    uint32_t info_at[64];
    const char *expect[16], *folder = NULL, *flash_in = NULL, *flash_out = NULL, *screen = NULL;
    const char *ain_path = NULL;                 /* --audio-in */
    int ain_once = 0;
    uint32_t ain_latency = 0, ain_off_at = UINT32_MAX;
    int aout_off = 0, expect_aout = 0;           /* --no-aout, --expect-aout */
    const char *aout_wav_path = NULL;            /* --aout-wav */
    int tk_info = 0, tk_next = 0;                /* --tk-info; the next --tk-ev due */
    int ep_info = 0;                             /* --ep-info */
    uint32_t tk_on_at = 100, tk_off_at = UINT32_MAX;
    const char *tk_screen = NULL;
    int tk_leds = 0;                             /* --tk-leds */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const int more = i + 1 < argc;
        if (!strcmp(a, "--seconds") && more) seconds = atof(argv[++i]);
        else if (!strcmp(a, "--settle") && more) settle_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--min-rms") && more) min_rms = atof(argv[++i]);
        else if (!strcmp(a, "--wav") && more) wav_path = argv[++i];
        else if (!strcmp(a, "--vel") && more) vel = atoi(argv[++i]);
        else if (!strcmp(a, "--lean") && more) lean = atoi(argv[++i]);
        else if (!strcmp(a, "--sysex") && more) sysex_path = argv[++i];
        else if (!strcmp(a, "--midi-out") && more) midi_out_path = argv[++i];
        else if (!strcmp(a, "--out") && more) out_path = argv[++i];
        else if (!strcmp(a, "--describe")) describe = 1;
        else if (!strcmp(a, "--labels")) labels = 1;
        else if (!strcmp(a, "--readouts")) readouts = 1;
        else if (!strcmp(a, "--expect-readout") && more && nexpect_rd < 16) {
            static char pat[16][96];             /* ('_' stands for a space: a pattern through ctest's quoting) */
            snprintf(pat[nexpect_rd], sizeof pat[0], "%s", argv[++i]);
            for (char *c = pat[nexpect_rd]; *c; c++)
                if (*c == '_')
                    *c = ' ';
            expect_rd[nexpect_rd] = pat[nexpect_rd];
            nexpect_rd++;
            readouts = 1;
        }
        else if (!strcmp(a, "--audio-in") && more) ain_path = argv[++i];
        else if (!strcmp(a, "--audio-in-once")) ain_once = 1;
        else if (!strcmp(a, "--audio-in-latency") && more) ain_latency = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--audio-in-off-at") && more) ain_off_at = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--no-aout")) aout_off = 1;
        else if (!strcmp(a, "--aout-wav") && more) aout_wav_path = argv[++i];
        else if (!strcmp(a, "--expect-aout") && more) expect_aout = atoi(argv[++i]);
        else if (!strcmp(a, "--verbose")) g_verbose = 1;
        else if (!strcmp(a, "--profile")) g_profile = 1;
        else if (!strcmp(a, "--helpers")) helpers = 1;
#ifdef RFM1_FPROF
        else if (!strcmp(a, "--fprofile") && more) fprofile = atoi(argv[++i]);
#endif
        else if (!strcmp(a, "--frame-ms") && more) frame_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--folder") && more) folder = argv[++i];
        else if (!strcmp(a, "--flash-in") && more) flash_in = argv[++i];
        else if (!strcmp(a, "--flash-out") && more) flash_out = argv[++i];
        else if (!strcmp(a, "--screen") && more) screen = argv[++i];
        else if (!strcmp(a, "--info-at") && more && ninfo < 64) info_at[ninfo++] = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--expect-ui") && more && nexpect < 16) expect[nexpect++] = argv[++i];
        else if (!strcmp(a, "--press") && more && npress < 256) {
            char lab[16], *at = strchr(argv[++i], '@');
            const size_t ln = at ? (size_t)(at - argv[i]) : 0;
            int b = -1;
            if (at && ln < sizeof lab) {
                memcpy(lab, argv[i], ln);
                lab[ln] = 0;
                for (int k = 0; k < FM1P_NB; k++)
                    if (!strcmp(LABEL[k], lab))
                        b = k;
            }
            if (b < 0) {
                usage();
                return 2;
            }
            char *e;
            press[npress].b = b;
            press[npress].at = (uint32_t)strtoul(at + 1, &e, 10);
            press[npress].dur = *e == '+' ? (uint32_t)strtoul(e + 1, NULL, 10) : 60u;
            npress++;
        }
        else if (!strcmp(a, "--turn") && more && nturn < 256) {   /* --turn ROLE@MS[:N]: N detents (default 1) */
            static const char *const ROLE[] = {"SELECT", "ALGO", "PRESETS", "K1", "K2", "K3", "K4"};
            char lab[16], *at = strchr(argv[++i], '@'), *e;
            const size_t ln = at ? (size_t)(at - argv[i]) : 0;
            int r = -1;
            if (at && ln < sizeof lab) {
                memcpy(lab, argv[i], ln);
                lab[ln] = 0;
                for (int k = 0; k < 7; k++)
                    if (!strcmp(ROLE[k], lab))
                        r = k;
            }
            if (r < 0) {
                usage();
                return 2;
            }
            turn[nturn].r = r;
            turn[nturn].at = (uint32_t)strtoul(at + 1, &e, 10);
            turn[nturn].n = *e == ':' ? (int)strtol(e + 1, NULL, 10) : 1;
            nturn++;
        }
        else if (!strcmp(a, "--tk")) g_tk.drive = 1;
        else if (!strcmp(a, "--tk-info")) tk_info = 1;
        else if (!strcmp(a, "--ep")) g_ep.print = 1;
        else if (!strcmp(a, "--ep-info")) ep_info = 1;
        else if (!strcmp(a, "--ep-set") && more) {
            if (!ep_parse(argv[++i], 1)) {
                fprintf(stderr, "rfm1_run: --ep-set %s: LAYER:PID=VALUE@MS\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--ep-lock") && more) {
            if (!ep_parse(argv[++i], 2)) {
                fprintf(stderr, "rfm1_run: --ep-lock %s: LAYER:PID=VALUE@MS\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--ep-free") && more) {
            if (!ep_parse(argv[++i], 3)) {
                fprintf(stderr, "rfm1_run: --ep-free %s: LAYER:PID@MS\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--ep-free-all") && more) {
            if (!ep_parse(argv[++i], 4)) {
                fprintf(stderr, "rfm1_run: --ep-free-all %s: LAYER@MS (255: every layer)\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--midi") && more && g_nmidi < 64) {   /* --midi HEX@MS: a short message into midi_in */
            unsigned b0 = 0, b1 = 0, b2 = 0, at = 0;
            if (sscanf(argv[++i], "%2x%2x%2x@%u", &b0, &b1, &b2, &at) != 4) {
                fprintf(stderr, "rfm1_run: --midi %s: three bytes as hex, @MS (B04A40@5000)\n", argv[i]);
                return 2;
            }
            g_midi[g_nmidi].pkt = (b0 >> 4) | b0 << 8 | b1 << 16 | b2 << 24;
            g_midi[g_nmidi++].at = at;
        } else if (!strcmp(a, "--ep-watch") && more) {
            if (!ep_parse(argv[++i], 10)) {
                fprintf(stderr, "rfm1_run: --ep-watch %s: LAYER:PAGE@MS\n", argv[i]);
                return 2;
            }
        }
        else if (!strcmp(a, "--tk-speech")) g_tk.speech = 1;
        else if (!strcmp(a, "--tk-at") && more) tk_on_at = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--tk-off-at") && more) tk_off_at = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--tk-screen") && more) tk_screen = argv[++i];
        else if (!strcmp(a, "--tk-leds")) tk_leds = 1;
        else if (!strcmp(a, "--tk-ev") && more) {
            if (tk_parse(argv[++i])) {
                fprintf(stderr, "rfm1_run: --tk-ev \"%s\" does not parse\n", argv[i]);
                usage();
                return 2;
            }
        } else if (!strcmp(a, "--tk-script") && more) {
            if (tk_script(argv[++i])) {
                fprintf(stderr, "rfm1_run: --tk-script %s: not read\n", argv[i]);
                return 2;
            }
        }
        else if (!strcmp(a, "--tap") && more) {
            char *s = argv[++i];
            while (*s) {
                const long b = strtol(s, &s, 10);
                if (b < 0 || b >= FM1P_NB) {
                    usage();
                    return 2;
                }
                taps |= 1u << b;
                while (*s == ',' || *s == ' ')
                    s++;
            }
        } else if (!strcmp(a, "--keys") && more && !strcmp(argv[i + 1], "none")) {
            no_keys = 1, i++;                    /* --keys none: no chord (a screen check the keys would change) */
        } else if (!strcmp(a, "--keys") && more) {
            char *s = argv[++i];
            nkeys = 0;
            if (!strcmp(s, "none")) {
                nkeys = -1;                      /* (no chord: below) */
                continue;
            }
            while (*s && nkeys < FM1P_NKEY) {
                const long k = strtol(s, &s, 10);
                if (k < 0 || k >= FM1P_NKEY) {
                    usage();
                    return 2;
                }
                keys[nkeys++] = (int)k;
                while (*s == ',' || *s == ' ')
                    s++;
            }
        } else if (a[0] == '-' || path) {
            usage();
            return 2;
        } else
            path = a;
    }
    if (!path || !frame_ms || vel < 0 || vel > 127 || lean < 0 || lean > 100) {
        usage();
        return 2;
    }
    if (!nkeys && !no_keys) {
        keys[0] = 9; keys[1] = 13; keys[2] = 16;   /* D4 F#4 A4 */
        nkeys = 3;
    }
    if (nkeys < 0)                               /* --keys none */
        nkeys = 0;
    g_file = path;
    size_t n = 0;
    uint8_t *bytes = read_file(path, &n);
    if (!bytes) {
        fprintf(stderr, "rfm1_run: %s: %s\n", path, strerror(errno));
        return 2;
    }
    static const uint8_t magic[8] = {0, 'a', 's', 'm', 1, 0, 0, 0};
    if (n < 8 || memcmp(bytes, magic, 8))
        return fail("not a WebAssembly module (version 1)");

    /* the manifest (section 4): its id, name, version, against the descriptor's below */
    char m_id[64] = "", m_name[256] = "", m_version[128] = "";
    uint32_t mlen = 0;
    const uint8_t *man = custom_section(bytes, n, "rfm1", &mlen);
    if (!man && !describe)
        return fail("no rfm1 section: not an .rfm1 (a bare module: --describe only)");
    if (man) {
        json_get_str((const char *)man, mlen, "id", m_id, sizeof m_id);
        json_get_str((const char *)man, mlen, "name", m_name, sizeof m_name);
        json_get_str((const char *)man, mlen, "version", m_version, sizeof m_version);
    }

    /* load: parse, link the imports (section 3), compile every function up front */
    const double t_load = now_s();
    g_env = m3_NewEnvironment();
    g_rt = m3_NewRuntime(g_env, 1024 * 1024, NULL);
    if (!g_env || !g_rt)
        return fail("wasm3: no memory");
    IM3Module mod = NULL;
    M3Result r = m3_ParseModule(g_env, &mod, bytes, (uint32_t)n);
    if (r)
        return fail("parse: %s", r);
    r = m3_LoadModule(g_rt, mod);
    if (r)
        return fail("load: %s", r);
    static const struct {
        const char *name, *sig;
        M3RawCall f;
    } imp[] = {
        {"fd_write", "i(iiii)", w_fd_write}, {"fd_read", "i(iiii)", w_fd_read}, {"fd_close", "i(i)", w_fd_close},
        {"fd_seek", "i(iIii)", w_fd_seek},   {"clock_time_get", "i(iIi)", w_clock_time_get},
        {"proc_exit", "v(i)", w_proc_exit},
    };
    for (size_t i = 0; i < sizeof imp / sizeof imp[0]; i++) {
        r = m3_LinkRawFunction(mod, "wasi_snapshot_preview1", imp[i].name, imp[i].sig, imp[i].f);
        if (r && r != m3Err_functionLookupFailed)
            return fail("import %s: %s", imp[i].name, r);
    }
    {   /* docs/VIRTUAL-CARTS.md 4: the host's file API (served from --folder, else FS_ENOFOLDER) */
        static const struct {
            const char *name, *sig;
            M3RawCall f;
        } hi[] = {
            {"fs_request", "i(iiiIii)", w_fs_request}, {"fs_poll", "i(i)", w_fs_poll},
            {"fs_cancel", "v(i)", w_fs_cancel},         {"fs_changed", "i()", w_fs_changed},
        };
        for (size_t i = 0; i < sizeof hi / sizeof hi[0]; i++) {
            r = m3_LinkRawFunction(mod, "rfm1_host_1", hi[i].name, hi[i].sig, hi[i].f);
            if (r && r != m3Err_functionLookupFailed)
                return fail("import rfm1_host_1.%s: %s", hi[i].name, r);
        }
    }
    if (helpers)
        nhelpers = helpers_bind(g_rt);
    r = m3_CompileModule(mod);
    if (r)
        return fail("compile: %s (an import outside the format's list, or an instruction wasm3 0.5.0 lacks)", r);
    for (int f = 0; f < F_N; f++) {
        r = m3_FindFunction(&g_fn[f], g_rt, FN[f].name);
        if (r) {
            g_fn[f] = NULL;
            if (f == F_INITIALIZE || f == F_AUDIO_IN || f == F_AUDIO_IN_INFO || f == F_LABELS_EPOCH || f == F_LABEL ||
                f == F_AUDIO_OUT_INFO || f == F_AUDIO_OUT || f == F_READOUT || f == F_TK || f == F_TK_INPUT ||
                f == F_TK_STATE || f == F_EP || f == F_EP_SET || f == F_EP_WATCH)
                continue;
            return fail("export %s: %s", FN[f].name, r);
        }
        if (m3_GetArgCount(g_fn[f]) != FN[f].nargs || m3_GetRetCount(g_fn[f]) != FN[f].nret)
            return fail("export %s: %u arguments, %u results (the format: %u, %u)", FN[f].name,
                        m3_GetArgCount(g_fn[f]), m3_GetRetCount(g_fn[f]), FN[f].nargs, FN[f].nret);
    }
    uint32_t memsize = 0;
    mem(&memsize);
    const double load_ms = (now_s() - t_load) * 1000.0;

    /* section 7: _initialize, the version, the descriptor, the layout */
    C0(F_INITIALIZE);
    if (g_err[0])
        return fail("trap: %s", g_err);
    const uint32_t ver = C0(F_VERSION);
    if (ver != 1)
        return fail("rfm1_version() = %u, not 1", ver);
    const uint32_t desc = C1(F_GET, FM1CORE_ABI);
    if (!desc || g_err[0])
        return fail("rfm1_get(%u) = 0: the module does not speak ABI %u%s%s", FM1CORE_ABI, FM1CORE_ABI,
                    g_err[0] ? ": " : "", g_err);
    for (int i = 0; i <= NLAYOUT; i++) {
        const int32_t v = (int32_t)C1(F_LAYOUT, (uint32_t)i);
        const int32_t want = i < NLAYOUT ? LAYOUT[i] : -1;
        if (v != want)
            return fail("rfm1_layout(%d) = %d, expected %d", i, v, want);
    }
    if (!in_mem(desc, sizeof(core_w32)))
        return fail("the descriptor at %u is outside memory", desc);
    if (rd32(D(abi_version)) != FM1CORE_ABI)
        return fail("the descriptor's abi_version is %u", rd32(D(abi_version)));
    const char *d_id = str(rd32(D(id))), *d_name = str(rd32(D(name))), *d_version = str(rd32(D(version)));
    if (!d_id || !d_name || !d_version)
        return fail("the descriptor's id, name or version is not a string in memory");
    char id[64], name[256], version[128];
    snprintf(id, sizeof id, "%s", d_id);
    snprintf(name, sizeof name, "%s", d_name);
    snprintf(version, sizeof version, "%s", d_version);
    const uint32_t panel = rd32(D(panel)), keymap = rd32(D(keymap)), flash_size = rd32(D(flash_size));
    if (!in_mem(panel, sizeof(fm1panel_t)))
        return fail("the descriptor's panel (%u) is outside memory", panel);
    if (!in_mem(keymap, 6 * FM1P_NCOL))
        return fail("the descriptor's keymap (%u) is outside memory", keymap);

    if (describe) {
        FILE *o = out_path ? fopen(out_path, "w") : stdout;
        if (!o) {
            fprintf(stderr, "rfm1_run: %s: %s\n", out_path, strerror(errno));
            return 2;
        }
        fprintf(o, "{\n  \"abi\": %u,\n  \"id\": ", rd32(D(abi_version)));
        json_str(o, id);
        fputs(",\n  \"name\": ", o); json_str(o, name);
        fputs(",\n  \"version\": ", o); json_str(o, version);
        fputs(",\n  \"source_url\": ", o); json_str(o, str(rd32(D(source_url))));
        fputs(",\n  \"family\": ", o); json_str(o, str(rd32(D(family))));
        fputs(",\n  \"license\": ", o); json_str(o, str(rd32(D(license))));
        fputs(",\n  \"gated\": ", o); json_str(o, str(rd32(D(gated))));
        fprintf(o, ",\n  \"flash_size\": %u,\n  \"nparams\": %u,\n  \"button_names\": [", flash_size,
                rd32(D(nparams)));
        for (int b = 0; b < FM1P_NB; b++) {
            if (b)
                fputs(", ", o);
            json_str(o, str(rd32(D(button_names) + 4u * (uint32_t)b)));
        }
        fputs("],\n  \"credits\": ", o); json_str(o, str(rd32(D(credits))));
        if (labels) {
            fprintf(o, ",\n  \"labels_epoch\": %u,\n  \"labels\": [", C0(F_LABELS_EPOCH));
            for (int w = 0; w < FM1CORE_LBL_N; w++) {
                const uint32_t lp = C1(F_LABEL, (uint32_t)w);
                if (w)
                    fputs(", ", o);
                if (lp)
                    json_str(o, str(lp));
                else
                    fputs("null", o);
            }
            fputs("]", o);
        }
        fprintf(o, ",\n  \"memory_bytes\": %u\n}\n", memsize);
        if (out_path)
            fclose(o);
        return 0;
    }
    if (strcmp(id, m_id) || strcmp(name, m_name) || strcmp(version, m_version))
        return fail("the manifest (%s, %s, %s) and the descriptor (%s, %s, %s) disagree", m_id, m_name, m_version,
                    id, name, version);

    /* --folder: the RemoteFM1 root, the cart's id, the manifest's "folders" */
    if (folder) {
        if (!realpath(folder, g_fs.root))
            return fail("--folder %s: %s", folder, strerror(errno));
        snprintf(g_fs.id, sizeof g_fs.id, "%s", id);
        const char *f = man ? strstr((const char *)man, "\"folders\"") : NULL;
        const char *fe = f ? memchr(f, '}', mlen - (size_t)(f - (const char *)man)) : NULL;
        if (f && fe) {
            static const char *const AREA[3] = {"audio", "code", "data"};
            for (int a = 0; a < 3; a++)
                json_get_str(f, (size_t)(fe - f), AREA[a], g_fs.access[a], sizeof g_fs.access[a]);
        }
        g_fs.changed = 1;
        fprintf(stderr, "rfm1_run: folder %s for %s: audio \"%s\", code \"%s\", data \"%s\"\n", g_fs.root, id,
                g_fs.access[0], g_fs.access[1], g_fs.access[2]);
    }

    /* --audio-in: the WAV, and whether the cart takes input (ain1 in the manifest's wants / needs, rfm1_audio_in) */
    int16_t *ain = NULL;
    uint32_t ain_frames = 0, ain_pos = 0, ain_blocks = 0, ain_buf = 0;
    int ain_on = 0;
    if (ain_path) {
        const int listed = manifest_lists(man, mlen, "wants", "ain1") || manifest_lists(man, mlen, "needs", "ain1");
        ain = wav_read(ain_path, &ain_frames);
        if (!ain)
            return 2;
        if (!listed || !g_fn[F_AUDIO_IN])
            fprintf(stderr, "rfm1_run: --audio-in %s ignored: %s\n", ain_path,
                    !listed ? "the manifest has no ain1 in wants or needs" : "the module exports no rfm1_audio_in");
        else
            ain_on = 1;
    }

    /* boot from a fresh flash (FM1CORE_FLASH_STAGED with no bytes), as Device::boot_from; or --flash-in's */
    const uint32_t scr = C1(F_ALLOC, 8192);
    if (!scr || !in_mem(scr, 8192))
        return fail("rfm1_alloc(8192) = %u%s%s", scr, g_err[0] ? ": " : "", g_err);
    const uint32_t s_str = scr, s_audio = scr + 1024, s_words = scr + 4096;
    const uint32_t lcd0 = rd32(P(lcd_writes));   /* (init's first screen counts) */
    const double t_boot = now_s();
    if (flash_in) {
        size_t fn = 0;
        uint8_t *fb = read_file(flash_in, &fn);
        const uint32_t at = fb ? C1(F_ALLOC, (uint32_t)fn) : 0;
        if (!fb || !at || !in_mem(at, (uint32_t)fn))
            return fail("--flash-in %s: not read or no room", flash_in);
        memcpy(mem(NULL) + at, fb, fn);
        if (!C2(F_FLASH_STAGE, at, (uint32_t)fn))
            return fail("--flash-in %s: flash_stage refused it", flash_in);
        free(fb);
    } else
        C2(F_FLASH_STAGE, 0, 0);
    memcpy(mem(NULL) + s_str, FM1CORE_FLASH_STAGED, sizeof FM1CORE_FLASH_STAGED);
    call(F_OPTIONS, s_str, 0, 0, 1);
    call(F_BOOT_OPTIONS, (uint32_t)-1, 0, (uint32_t)-1, 0);
    wr8(P(ready), 2);
    C1(F_INIT, 0);
    const double boot_ms = (now_s() - t_boot) * 1000.0;
    if (g_err[0])
        return fail("init trapped: %s", g_err);
    if (g_fn[F_TK]) {                            /* tk1: the record after init (docs/RFM1-TAKEOVER.md 4.2) */
        if (!g_fn[F_TK_INPUT] || !g_fn[F_TK_STATE])
            return fail("tk1: rfm1_tk without rfm1_tk_input and rfm1_tk_state");
        g_tk.rec = C0(F_TK);
        const char *why = g_err[0] ? g_err : tk_bad(g_tk.rec);
        if (tk_info) {
            if (!in_mem(g_tk.rec, TK_SIZE)) {
                printf("{\"error\": ");
                json_str(stdout, why);
                printf("}\n");
                return 1;
            }
            char nm[17];
            memcpy(nm, mem(NULL) + g_tk.rec + TK_O_NAME, 16);
            nm[16] = 0;
            printf("{\"version\": %u, \"size\": %u, \"name\": ", rd16(g_tk.rec), rd16(g_tk.rec + 2));
            json_str(stdout, nm);
            printf(", \"name_terminated\": %s, \"claims\": %u, \"scr_seq\": %u, \"led_seq\": %u, \"say_seq\": %u}\n",
                   memchr(mem(NULL) + g_tk.rec + TK_O_NAME, 0, 16) ? "true" : "false", rd32(g_tk.rec + TK_O_CLAIMS),
                   rd32(g_tk.rec + TK_O_SCR_SEQ), rd32(g_tk.rec + TK_O_LED_SEQ), rd32(g_tk.rec + TK_O_SAY_SEQ));
            return *why ? 1 : 0;
        }
        if (*why)
            return fail("tk1: %s", why);
        g_tk.claims = rd32(g_tk.rec + TK_O_CLAIMS);
        g_tk.scr_seq = rd32(g_tk.rec + TK_O_SCR_SEQ);
        g_tk.led_seq = rd32(g_tk.rec + TK_O_LED_SEQ);
        g_tk.say_seq = rd32(g_tk.rec + TK_O_SAY_SEQ);
        memcpy(g_tk.led, mem(NULL) + g_tk.rec + TK_O_LED, sizeof g_tk.led);
        if (g_tk.drive)
            printf("tk: \"%s\" claims 0x%02x\n", (const char *)mem(NULL) + g_tk.rec + TK_O_NAME, g_tk.claims);
    } else if (tk_info) {
        printf("{\"error\": \"no rfm1_tk export\"}\n");
        return 1;
    } else if (g_tk.drive)
        return fail("--tk: the module exports no rfm1_tk (not built with tk1)");
    if (g_fn[F_EP]) {                            /* ep1: the record after init (engine-params/SPEC.md 4) */
        if (!g_fn[F_EP_SET] || !g_fn[F_EP_WATCH])
            return fail("ep1: rfm1_ep without rfm1_ep_set and rfm1_ep_watch");
        g_ep.rec = C0(F_EP);
        if (g_ep.rec && !in_mem(g_ep.rec, 264))
            return fail("ep1: rfm1_ep() = %u, outside the memory", g_ep.rec);
        if (g_ep.rec)
            ep_poll(0);
        if (ep_info) {
            if (!g_ep.rec || !g_ep.desc) {
                printf("{\"record\": %u, \"desc_len\": 0}\n", g_ep.rec);
                return 1;
            }
            printf("{\"record\": %u, \"version\": %u, \"size\": %u, \"nlayers\": %u, \"nengines\": %u, "
                   "\"npages\": %u, \"nparams\": %u, \"ntables\": %u, \"desc_len\": %u}\n", g_ep.rec,
                   rd16(g_ep.rec), rd16(g_ep.rec + 2), rd8(g_ep.rec + EP_O_NLAY), g_ep.ne, g_ep.ng, g_ep.np, g_ep.nt,
                   g_ep.dlen);
            if (out_path) {
                FILE *f = fopen(out_path, "wb");
                if (!f || fwrite(g_ep.desc, 1, g_ep.dlen, f) != g_ep.dlen)
                    return fail("--out %s: not written", out_path);
                fclose(f);
            }
            return 0;
        }
    } else if (ep_info) {
        printf("{\"record\": 0, \"error\": \"no rfm1_ep export\"}\n");
        return 1;
    } else if (g_ep.print || g_ep.nev)
        return fail("--ep: the module exports no rfm1_ep (not built with ep1)");
    tk_sort();
    if (ain_on) {                                /* (the input buffer: rfm1_alloc'd like the output's) */
        ain_buf = C1(F_ALLOC, FM1P_BLOCK * 4u);
        if (!ain_buf || !in_mem(ain_buf, FM1P_BLOCK * 4u))
            return fail("rfm1_alloc(%u) for the input = %u", FM1P_BLOCK * 4u, ain_buf);
        C2(F_AUDIO_IN_INFO, 1, ain_latency);     /* before the first rfm1_audio_in */
        fprintf(stderr, "rfm1_run: audio in %s: %u frames%s, latency %u frames\n", ain_path, ain_frames,
                ain_once ? " once" : " looped", ain_latency);
    }

    /* aout1: the channels the module gives (asked once, after init) and a buffer for 3..N, as a host does */
    uint32_t aout_n = 2, aout_buf = 0;
    if (g_fn[F_AUDIO_OUT_INFO]) {
        const uint32_t n = C0(F_AUDIO_OUT_INFO);
        if (n == 4 || n == 6 || n == 8 || n == 10)
            aout_n = n;
        fprintf(stderr, "rfm1_run: aout1: rfm1_audio_out_info %u%s%s\n", n, aout_n > 2 ? "" : " (the master alone)",
                aout_n > 2 && !g_fn[F_AUDIO_OUT] ? ", but no rfm1_audio_out" : aout_n > 2 && aout_off ? ", not taken (--no-aout)" : "");
    }
    const int aout_on = aout_n > 2 && g_fn[F_AUDIO_OUT] && !aout_off;
    const uint32_t aout_k = aout_n - 2;          /* channels 3..N */
    double aout_sumsq[8] = {0}, aout_peak[8] = {0};
    uint64_t aout_frames = 0;
    FILE *aout_wav = NULL;
    if (aout_on) {
        aout_buf = C1(F_ALLOC, FM1P_BLOCK * 2u * aout_k);
        if (!aout_buf || !in_mem(aout_buf, FM1P_BLOCK * 2u * aout_k))
            return fail("rfm1_alloc(%u) for the channels 3..%u = %u", FM1P_BLOCK * 2u * aout_k, aout_n, aout_buf);
        if (aout_wav_path) {
            aout_wav = fopen(aout_wav_path, "wb");
            if (!aout_wav)
                return fail("--aout-wav %s: not written", aout_wav_path);
            wav_header_n(aout_wav, 0, (uint16_t)aout_n);
        }
    }

    /* the clock */
    FILE *wav = NULL;
    if (wav_path) {
        wav = fopen(wav_path, "wb");
        if (!wav) {
            fprintf(stderr, "rfm1_run: %s: %s\n", wav_path, strerror(errno));
            return 2;
        }
        wav_header(wav, 0);
    }
    size_t sx_n = 0, sx_at = 0;
    uint8_t *sx = NULL;
    if (sysex_path) {
        sx = read_file(sysex_path, &sx_n);
        if (!sx) {
            fprintf(stderr, "rfm1_run: %s: %s\n", sysex_path, strerror(errno));
            return 2;
        }
    }
    FILE *mo = NULL;                             /* --midi-out */
    uint32_t mo_bytes = 0;
    if (midi_out_path && !(mo = fopen(midi_out_path, "wb"))) {
        fprintf(stderr, "rfm1_run: %s: %s\n", midi_out_path, strerror(errno));
        return 2;
    }
    const uint32_t chord_ms = (uint32_t)(seconds * 1000.0 + 0.5);
    uint32_t chord_mask = 0;
    for (int k = 0; k < nkeys; k++)
        chord_mask |= 1u << keys[k];
    /* the .syx (from settle - 800 ms, as the conformance scripts send a bank): USB-MIDI packets, CIN 4 for three bytes
     * of a frame, 5 / 6 / 7 for its last one, two or three; as many per ms as the core takes, its replies drained.
     * The chord waits until every packet is in, then 300 ms more */
    const uint32_t sx_from = settle_ms > 800u ? settle_ms - 800u : 0u;
    uint32_t chord_at = settle_ms, sx_done_ms = 0;
    uint64_t frames_done = 0, wav_frames = 0, chord_frames = 0;
    uint32_t last_frame = 0, end_ms = UINT32_MAX;
    double sumsq = 0, peak = 0, wall_chord = 0;
    uint32_t pending_pkt = 0, sx_packets = 0;
    int sx_in = 0;                               /* (inside a frame) */
    memset(g_prof, 0, sizeof g_prof);            /* (--profile: the run only, not the load and boot) */
    double worst_ms = 0;
    uint32_t worst_ms_at = 0;
#ifdef RFM1_FPROF
    if (fprofile) {
        struct itimerval it = {{0, 100}, {0, 100}};
        signal(SIGPROF, fp_signal);
        setitimer(ITIMER_PROF, &it, NULL);
        g_fp_on = 1;
    }
#endif
    const double t_run = now_s();
    double t_chord = 0;
    int halted = 0, chord_on = 0, chord_done = 0;
    uint32_t ms = 0;
    for (; ms < end_ms && ms < 600000u; ms++) {
        const double t_ms = g_profile ? now_s() : 0;
        if (sx && ms >= sx_from && (pending_pkt || sx_at < sx_n)) {
            while (!mo && C1(F_MIDI_OUT_TAKE, s_words) && !g_err[0])
                (void)rd32(s_words);
            for (;;) {
                if (!pending_pkt) {
                    while (!sx_in && sx_at < sx_n && sx[sx_at] != 0xF0)
                        sx_at++;                 /* (to the next frame) */
                    if (sx_at >= sx_n)
                        break;
                    sx_in = 1;
                    uint8_t b[3] = {0, 0, 0};
                    int nb = 0, last = 0;
                    while (nb < 3 && sx_at < sx_n) {
                        b[nb] = sx[sx_at++];
                        if (b[nb++] == 0xF7) {
                            last = 1;
                            sx_in = 0;
                            break;
                        }
                    }
                    pending_pkt = (last ? (uint32_t)(4 + nb) : 4u) | (uint32_t)b[0] << 8 | (uint32_t)b[1] << 16 |
                                  (uint32_t)b[2] << 24;
                }
                if (!C1(F_MIDI_IN, pending_pkt) || g_err[0])
                    break;                       /* (no room: the rest after the next ms) */
                pending_pkt = 0;
                sx_packets++;
            }
            if (!pending_pkt && sx_at >= sx_n) {
                sx_done_ms = ms;
                if (chord_at < ms + 300u)
                    chord_at = ms + 300u;
            }
        }
        if (taps && !chord_on && !chord_done && ms + 1000u >= chord_at) {   /* --tap: the buttons, by their matrix ids */
            uint32_t m = 0;
            for (int b = 0; b < FM1P_NB; b++)
                if (taps >> b & 1u)
                    m |= 1u << rd8(P(btn_id) + (uint32_t)b);
            wr32(P(buttons_tap), rd32(P(buttons_tap)) | m);
            taps = 0;
        }
        if (!chord_on && !chord_done && ms >= chord_at && (!sx || (!pending_pkt && sx_at >= sx_n))) {
            /* the chord down (Device::keys: taps for the new keys) */
            for (int k = 0; k < nkeys; k++)
            {
                wr8(P(key_vel) + (uint32_t)keys[k], (uint8_t)vel);
                wr8(P(key_lean) + (uint32_t)keys[k], (uint8_t)lean);
            }
            const uint32_t held = rd32(P(keys));
            wr32(P(keys_tap), rd32(P(keys_tap)) | (chord_mask & ~held));
            wr32(P(keys), chord_mask);
            t_chord = now_s();
            chord_on = 1;
            chord_at = ms;
        }
        if (chord_on && ms == chord_at + chord_ms) {   /* and up; 200 ms more */
            wr32(P(keys), 0);
            for (int k = 0; k < nkeys; k++)
                wr8(P(key_lean) + (uint32_t)keys[k], 0);
            wall_chord = now_s() - t_chord;
            chord_on = 0;
            chord_done = 1;
            end_ms = ms + 200u;
        }
        while (mo && C1(F_MIDI_OUT_TAKE, s_words) && !g_err[0]) {   /* --midi-out: a USB-MIDI packet's bytes, by CIN */
            static const uint8_t CIN_N[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
            const uint32_t pkt = rd32(s_words);
            for (uint32_t k = 0; k < CIN_N[pkt & 15u]; k++)
                fputc((int)(pkt >> (8u + 8u * k) & 0xFFu), mo);
            mo_bytes += CIN_N[pkt & 15u];
        }
        fs_complete(ms);                         /* (between two calls: the folder's requests due now) */
        for (int k = 0; k < npress; k++) {       /* --press: held from at, let go after dur */
            const uint32_t m = 1u << rd8(P(btn_id) + (uint32_t)press[k].b);
            if (ms == press[k].at) {
                wr32(P(buttons), rd32(P(buttons)) | m);
                wr32(P(buttons_tap), rd32(P(buttons_tap)) | m);
            } else if (ms == press[k].at + press[k].dur)
                wr32(P(buttons), rd32(P(buttons)) & ~m);
        }
        for (int k = 0; k < nturn; k++)   /* --turn: the role's detents, as a host turns a knob (enc_id, enc_dir) */
            if (ms == turn[k].at) {
                const uint32_t e = P(enc) + 4u * rd8(P(enc_id) + (uint32_t)turn[k].r);
                const int dir = (int8_t)rd8(P(enc_dir) + (uint32_t)turn[k].r);
                wr32(e, rd32(e) + (uint32_t)(turn[k].n * (dir ? dir : 1)));
            }
        for (int k = 0; k < ninfo; k++)
            if (ms == info_at[k]) {
                C2(F_UI_INFO, s_str, 1024);
                const char *ui = str(s_str);
                printf("ui@%u: %s\n", ms, ui ? ui : "");
            }
        tk_ms(ms, tk_on_at, tk_off_at, &tk_next);   /* --tk: the takeover's events, between two ticks */
        ep_ms(ms);                               /* --ep-set, --ep-watch: between two ticks, as a host's queue */
        C1(F_TICK, ms);
        if (ms == 0 || ms - last_frame >= frame_ms) {
            last_frame = ms;
            C0(F_FRAME);
            ep_poll(ms);
            if (g_tk.drive)
                tk_poll(ms);
            if (readouts) {   /* --readouts: each new readout (rd1), and the knobs' live labels when they move */
                uint32_t sz = 0;
                const uint8_t *m;
                if (g_fn[F_READOUT] && !rd_ptr)
                    rd_ptr = C0(F_READOUT);
                m = mem(&sz);
                if (rd_ptr && (uint64_t)rd_ptr + 48u <= sz) {
                    uint32_t seq;
                    memcpy(&seq, m + rd_ptr + 4, 4);
                    if (seq != rd_last) {
                        int16_t frac;
                        memcpy(&frac, m + rd_ptr + 12, 2);
                        rd_last = seq;
                        printf("readout@%u: seq %u ctl %d kind %u flags %u frac %d \"%.16s\" \"%.16s\"\n", ms, seq,
                               (int)(int8_t)m[rd_ptr + 8], m[rd_ptr + 9], m[rd_ptr + 10], frac,
                               (const char *)m + rd_ptr + 14, (const char *)m + rd_ptr + 31);
                        if (nexpect_rd) {   /* --expect-readout: matched against `ctl C title = value` */
                            char line[96];
                            snprintf(line, sizeof line, "ctl %d %.16s = %.16s", (int)(int8_t)m[rd_ptr + 8],
                                     (const char *)m + rd_ptr + 14, (const char *)m + rd_ptr + 31);
                            for (int k = 0; k < nexpect_rd; k++)
                                expect_rd_seen[k] |= strstr(line, expect_rd[k]) != NULL;
                        }
                    }
                }
                if (g_fn[F_LABELS_EPOCH]) {
                    const uint32_t e = C0(F_LABELS_EPOCH);
                    if (e != lb_last) {
                        lb_last = e;
                        printf("labels@%u: epoch %u knobs", ms, e);
                        for (int k = 0; k < 8; k++) {
                            const char *l = str(C1(F_LABEL, k));
                            printf(" \"%s\"", l ? l : "");
                        }
                        printf("\n");
                        int nb = 0;              /* the buttons' (FM1P_B_* order) when any is given: null as - */
                        for (int k = 8; k < 22; k++)
                            nb += str(C1(F_LABEL, k)) != NULL;
                        if (nb) {
                            printf("labels@%u: buttons", ms);
                            for (int k = 8; k < 22; k++) {
                                const char *l = str(C1(F_LABEL, k));
                                printf(l ? " \"%s\"" : " -", l ? l : "");
                            }
                            printf("\n");
                        }
                    }
                }
            }
        } else
            C0(F_IDLE);
        if (ain_on && ms >= ain_off_at) {        /* --audio-in-off-at: the input off (unplugged, denied) */
            C2(F_AUDIO_IN_INFO, 0, 0);
            ain_on = 0;
            fprintf(stderr, "rfm1_run: audio in off at %u ms (%u blocks fed)\n", ms, ain_blocks);
        }
        while (frames_done + FM1P_BLOCK <= (uint64_t)ms * FM1P_FS / 1000u) {
            if (ain_on && (!ain_once || ain_pos < ain_frames)) {   /* the input block, then the same frames out */
                uint8_t *m = mem(NULL) + ain_buf;
                for (uint32_t i = 0; i < FM1P_BLOCK; i++) {
                    int16_t l = 0, r = 0;
                    if (ain_pos < ain_frames) {
                        l = ain[2 * ain_pos];
                        r = ain[2 * ain_pos + 1];
                        ain_pos++;
                        if (!ain_once && ain_pos >= ain_frames)
                            ain_pos = 0;
                    }
                    memcpy(m + 4u * i, &l, 2);
                    memcpy(m + 4u * i + 2u, &r, 2);
                }
                C2(F_AUDIO_IN, ain_buf, FM1P_BLOCK);
                ain_blocks++;
            }
            if (aout_on) {                       /* (aout1: zeroed, right before rfm1_audio, the same frames) */
                memset(mem(NULL) + aout_buf, 0, FM1P_BLOCK * 2u * aout_k);
                C2(F_AUDIO_OUT, aout_buf, FM1P_BLOCK);
            }
            C2(F_AUDIO, s_audio, FM1P_BLOCK);
            int16_t blk[FM1P_BLOCK * 2];
            if (in_mem(s_audio, sizeof blk))
                memcpy(blk, mem(NULL) + s_audio, sizeof blk);
            if (aout_on && in_mem(aout_buf, FM1P_BLOCK * 2u * aout_k)) {
                int16_t ab[FM1P_BLOCK * 8];
                memcpy(ab, mem(NULL) + aout_buf, FM1P_BLOCK * 2u * aout_k);
                for (uint32_t i = 0; i < FM1P_BLOCK; i++) {
                    for (uint32_t c = 0; c < aout_k; c++) {
                        const double v = ab[i * aout_k + c] / 32768.0;
                        aout_sumsq[c] += v * v;
                        if (fabs(v) > aout_peak[c])
                            aout_peak[c] = fabs(v);
                    }
                    if (aout_wav) {
                        fwrite(&blk[2 * i], 2, 2, aout_wav);
                        fwrite(&ab[i * aout_k], 2, aout_k, aout_wav);
                    }
                }
                aout_frames += FM1P_BLOCK;
            }
            if (chord_on) {
                for (int i = 0; i < FM1P_BLOCK * 2; i++) {
                    const double v = blk[i] / 32768.0;
                    sumsq += v * v;
                    if (fabs(v) > peak)
                        peak = fabs(v);
                }
                chord_frames += FM1P_BLOCK;
            }
            if (wav) {
                fwrite(blk, sizeof blk, 1, wav);
                wav_frames += FM1P_BLOCK;
            }
            frames_done += FM1P_BLOCK;
        }
        if (g_profile && now_s() - t_ms > worst_ms) {
            worst_ms = now_s() - t_ms;
            worst_ms_at = ms;
        }
        if (g_err[0])
            break;
        if (ms % 50u == 0 && C0(F_HALTED)) {
            halted = (int)C0(F_HALTED);
            break;
        }
    }
    const uint32_t total_ms = ms;
#ifdef RFM1_FPROF
    g_fp_on = 0;
#endif
    if (mo) {
        fclose(mo);
        fprintf(stderr, "rfm1_run: %s: %u MIDI bytes out\n", midi_out_path, mo_bytes);
    }
    if (sx && !g_err[0] && !halted)
        fprintf(stderr, "rfm1_run: %s: %u SysEx packets in from %u ms, the last at %u ms\n", sysex_path, sx_packets,
                sx_from, sx_done_ms);
    const double wall = now_s() - t_run;
    const uint32_t lcd_writes = rd32(P(lcd_writes)) - lcd0;
    if (wav) {
        wav_header(wav, (uint32_t)wav_frames);
        fclose(wav);
    }
    if (aout_wav) {
        wav_header_n(aout_wav, (uint32_t)aout_frames, (uint16_t)aout_n);
        fclose(aout_wav);
    }
    char info[1024] = "";
    if (!g_err[0]) {
        C2(F_UI_INFO, s_str, sizeof info);
        const char *ui = str(s_str);
        if (ui)
            snprintf(info, sizeof info, "%s", ui);
        C2(F_STATS, s_words, s_words + 4);
        if (flash_out) {                         /* a save: the lazy writes done, then the image */
            C0(F_FLASH_SYNC);
            const uint32_t fl = C0(F_FLASH);
            FILE *o = fopen(flash_out, "wb");
            if (!o || !in_mem(fl, flash_size) || fwrite(mem(NULL) + fl, 1, flash_size, o) != flash_size)
                fprintf(stderr, "rfm1_run: --flash-out %s: not written\n", flash_out);
            if (o)
                fclose(o);
        }
        if (screen && in_mem(P(lcd), FM1P_LCD_W * FM1P_LCD_H * 2u)) {   /* RGB565 high byte first -> PPM */
            FILE *o = fopen(screen, "wb");
            if (o) {
                const uint8_t *px = mem(NULL) + P(lcd);
                fprintf(o, "P6\n%d %d\n255\n", FM1P_LCD_W, FM1P_LCD_H);
                for (int i = 0; i < FM1P_LCD_W * FM1P_LCD_H; i++) {
                    const unsigned v = (unsigned)px[2 * i] << 8 | px[2 * i + 1];
                    const uint8_t rgb[3] = {(uint8_t)((v >> 11) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63),
                                            (uint8_t)((v & 31) * 255 / 31)};
                    fwrite(rgb, 1, 3, o);
                }
                fclose(o);
            }
        }
        if (g_tk.drive && g_tk.rec) {
            if (tk_screen) {
                FILE *o = strcmp(tk_screen, "-") ? fopen(tk_screen, "w") : stdout;
                if (o) {
                    tk_pbm(o);
                    if (o != stdout)
                        fclose(o);
                } else
                    fprintf(stderr, "rfm1_run: --tk-screen %s: not written\n", tk_screen);
            }
            if (tk_leds) {
                const uint8_t *l = mem(NULL) + g_tk.rec + TK_O_LED;
                for (unsigned i = 0; i < TK_NLED; i++)
                    printf("tk-led-end: %u %u %u %u\n", i, l[3u * i], l[3u * i + 1], l[3u * i + 2]);
            }
            printf("tk: %u inputs, %u skipped, scr_seq %u, led_seq %u, say_seq %u, claims 0x%02x, %s\n", g_tk.inputs,
                   g_tk.skipped, g_tk.scr_seq, g_tk.led_seq, g_tk.say_seq, g_tk.claims, g_tk.shown ? "shown" : "not shown");
        }
        C0(F_SHUTDOWN);
    }

    const double rms = chord_frames ? sqrt(sumsq / (double)(chord_frames * 2u)) : 0.0;
    const double xrt = wall > 0 ? (total_ms / 1000.0) / wall : 0, xrt_chord = wall_chord > 0 ? seconds / wall_chord : 0;
    printf("%s %s: chord rms %.4f (peak %.3f), %u LCD writes, %.1fx real time (chord %.1fx), load %.1f ms, "
           "boot %.1f ms, %u KiB memory, flash %u KiB, %ld KiB file%s%s\n",
           id, version, rms, peak, lcd_writes, xrt, xrt_chord, load_ms, boot_ms, memsize / 1024u, flash_size / 1024u,
           (long)(n / 1024), info[0] ? "; ui_info: " : "", info);
    if (helpers)
        printf("helpers: %d bound, %u native calls\n", nhelpers, g_helper_calls);
    if (g_ep.sets)
        printf("ep: %d sets (%d refused), %.1f us mean, %.1f us worst\n", g_ep.sets, g_ep.bad_sets,
               g_ep.set_us_total / g_ep.sets, g_ep.set_us_worst);
    if (g_profile) {   /* the exports' share of the run's wall time (the rest: rfm1_run itself) */
        static const int SHOW[] = {F_TICK, F_FRAME, F_IDLE, F_AUDIO, F_MIDI_IN, F_MIDI_OUT_TAKE, F_HALTED};
        double in = 0;
        printf("profile %s %s (frame every %u ms, %u device ms, %.1f ms wall):\n", id, version, frame_ms, total_ms,
               wall * 1e3);
        printf("  %-18s %8s %7s %10s %10s\n", "export", "calls", "share", "mean us", "worst us");
        for (size_t k = 0; k < sizeof SHOW / sizeof SHOW[0]; k++) {
            const int f = SHOW[k];
            if (!g_prof[f].calls)
                continue;
            in += g_prof[f].total;
            printf("  %-18s %8llu %6.1f%% %10.2f %10.1f\n", FN[f].name, (unsigned long long)g_prof[f].calls,
                   wall > 0 ? 100.0 * g_prof[f].total / wall : 0, 1e6 * g_prof[f].total / (double)g_prof[f].calls,
                   1e6 * g_prof[f].worst);
        }
        printf("  %-18s %8s %6.1f%%\n", "(rfm1_run)", "", wall > 0 ? 100.0 * (wall - in) / wall : 0);
        printf("  worst device ms: %.1f us at ms %u; an audio block (%d frames) is due every %.0f us\n",
               worst_ms * 1e6, worst_ms_at, FM1P_BLOCK, 1e6 * FM1P_BLOCK / FM1P_FS);
    }
#ifdef RFM1_FPROF
    if (fprofile)
        fp_report(fprofile);
#endif
    int bad = 0;
    if (g_err[0]) {
        bad |= fail("trap: %s", g_err);
        if (g_exit_code >= 0)
            fprintf(stderr, "rfm1_run: (the firmware called exit(%d): halted 0x%x)\n", g_exit_code,
                    0x100 | g_exit_code);
    }
    if (halted)
        bad |= fail("halted (0x%x) during the run", (unsigned)halted);
    if (g_fs.root[0])
        fprintf(stderr, "rfm1_run: folder: %u requests served, %u refused\n", g_fs.served, g_fs.refused);
    for (int k = 0; k < nexpect; k++)
        if (!strstr(info, expect[k]))
            bad |= fail("ui_info has no \"%s\"", expect[k]);
    for (int k = 0; k < nexpect_rd; k++)
        if (!expect_rd_seen[k])
            bad |= fail("no readout matched \"%s\"%s", expect_rd[k], rd_ptr ? "" : " (no rfm1_readout: not built with rd1)");
    if (!bad && rms < min_rms)
        bad |= fail("silent: the chord's RMS %.6f is under %.6f", rms, min_rms);
    if (!bad && !lcd_writes)
        bad |= fail("no LCD writes");
    if (ain_path)
        fprintf(stderr, "rfm1_run: audio in: %u blocks fed\n", ain_blocks);
    double aout_rms_max = 0;
    if (aout_on) {                               /* each channel's level over the whole run */
        fprintf(stderr, "rfm1_run: aout1: %u channels:", aout_n);
        for (uint32_t c = 0; c < aout_k; c++) {
            const double r = aout_frames ? sqrt(aout_sumsq[c] / (double)aout_frames) : 0.0;
            if (r > aout_rms_max)
                aout_rms_max = r;
            fprintf(stderr, " %u rms %.4f peak %.3f%s", c + 3, r, aout_peak[c], c + 1 < aout_k ? ";" : "\n");
        }
    }
    if (expect_aout && (int)aout_n != expect_aout)
        bad |= fail("aout1: %u channels, not %d", aout_n, expect_aout);
    else if (expect_aout && (!aout_on || aout_rms_max < min_rms))
        bad |= fail("aout1: channels 3..%u silent (the loudest's RMS %.6f under %.6f)", aout_n, aout_rms_max, min_rms);
    free(sx);
    free(ain);
    m3_FreeRuntime(g_rt);
    m3_FreeEnvironment(g_env);
    free(bytes);
    return bad ? 1 : 0;
}
