/* SPDX-License-Identifier: GPL-3.0-only */
/* rfm1_run: an .rfm1 file (docs/RFM1-FORMAT.md, version 1) booted and played in wasm3 v0.5.0 (third_party/wasm3), as a
 * host would run it: the imports of section 3, the checks of section 7, a fresh flash, the native ABI's device clock
 * (fm1core.h: tick once per device ms, frame every 15 ms else idle, audio per 128-frame block due at 44.1 kHz).
 *
 *   rfm1_run [options] <file.rfm1>
 *     --seconds S    the chord held for S device seconds (default 5)
 *     --settle MS    device ms after init before the chord (default 4000)
 *     --keys K,K,..  the chord's key numbers (default 9,13,16: D4 F#4 A4, as core_bench / core_conformance)
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
 *     --expect-ui S  fail unless the final ui_info contains S (repeat)
 *     --flash-in F   boot from this flash image (a save) instead of a fresh flash
 *     --flash-out F  the flash image after the run (flash_sync first): a save
 *     --screen F     the screen at the end as a PPM image
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
#include "wasm3.h"

/* ------------------------------------------------------------------------------------------------ the exports --- */
enum {
    F_INITIALIZE, F_VERSION, F_GET, F_LAYOUT, F_ALLOC, F_FREE, F_OPTIONS, F_BOOT_OPTIONS, F_INIT, F_TICK, F_FRAME,
    F_IDLE, F_AUDIO, F_MIDI_IN, F_MIDI_OUT_TAKE, F_HALTED, F_SHUTDOWN, F_FLASH_STAGE, F_FLASH, F_FLASH_DIRTY,
    F_FLASH_SYNC, F_STATS, F_UI_INFO,
    F_AUDIO_IN, F_AUDIO_IN_INFO,           /* (optional: docs/VIRTUAL-CARTS.md 8, a cart with ain1) */
    F_LABELS_EPOCH, F_LABEL,               /* (optional: docs/DYNAMIC-LABELS.md) */
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
};

static IM3Environment g_env;
static IM3Runtime g_rt;
static IM3Function g_fn[F_N];
static int g_verbose;
static int g_exit_code = -1;           /* proc_exit's code, -1 none */
static char g_err[512];                /* the first trap */
static const char *g_file = "";

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
    M3Result res = m3_Call(g_fn[f], FN[f].nargs, ap);
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

/* ------------------------------------------------------------------------------------------------ the WAV ------- */
static void wav_header(FILE *f, uint32_t frames)
{
    const uint32_t data = frames * 4u, rate = FM1P_FS, byterate = FM1P_FS * 4u;
    const uint32_t riff = 36u + data, fmt = 16u;
    const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byterate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

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

static void usage(void)
{
    fputs("usage: rfm1_run [--seconds S] [--settle MS] [--keys K,K,..] [--vel V] [--lean L] [--tap B,..] [--sysex FILE] [--wav FILE]\n"
          "                [--min-rms X] [--folder DIR] [--press B@MS[+D]].. [--info-at MS].. [--expect-ui S]..\n"
          "                [--flash-in FILE] [--flash-out FILE] [--screen FILE.ppm] [--midi-out FILE]\n"
          "                [--audio-in FILE.wav [--audio-in-once] [--audio-in-latency N] [--audio-in-off-at MS]]\n"
          "                [--verbose] [--describe [--labels] [--out FILE]] <file.rfm1>\n", stderr);
}

int main(int argc, char **argv)
{
    double seconds = 5.0, min_rms = 0.0005;
    uint32_t settle_ms = 4000;
    int describe = 0, labels = 0;
    const char *out_path = NULL, *wav_path = NULL, *sysex_path = NULL, *path = NULL, *midi_out_path = NULL;
    int keys[FM1P_NKEY], nkeys = 0, vel = 100, lean = 0;
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
        else if (!strcmp(a, "--audio-in") && more) ain_path = argv[++i];
        else if (!strcmp(a, "--audio-in-once")) ain_once = 1;
        else if (!strcmp(a, "--audio-in-latency") && more) ain_latency = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--audio-in-off-at") && more) ain_off_at = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--verbose")) g_verbose = 1;
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
        } else if (!strcmp(a, "--keys") && more) {
            char *s = argv[++i];
            nkeys = 0;
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
    if (!path || vel < 0 || vel > 127 || lean < 0 || lean > 100) {
        usage();
        return 2;
    }
    if (!nkeys) {
        keys[0] = 9; keys[1] = 13; keys[2] = 16;   /* D4 F#4 A4 */
        nkeys = 3;
    }
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
    r = m3_CompileModule(mod);
    if (r)
        return fail("compile: %s (an import outside the format's list, or an instruction wasm3 0.5.0 lacks)", r);
    for (int f = 0; f < F_N; f++) {
        r = m3_FindFunction(&g_fn[f], g_rt, FN[f].name);
        if (r) {
            g_fn[f] = NULL;
            if (f == F_INITIALIZE || f == F_AUDIO_IN || f == F_AUDIO_IN_INFO || f == F_LABELS_EPOCH || f == F_LABEL)
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
    if (ain_on) {                                /* (the input buffer: rfm1_alloc'd like the output's) */
        ain_buf = C1(F_ALLOC, FM1P_BLOCK * 4u);
        if (!ain_buf || !in_mem(ain_buf, FM1P_BLOCK * 4u))
            return fail("rfm1_alloc(%u) for the input = %u", FM1P_BLOCK * 4u, ain_buf);
        C2(F_AUDIO_IN_INFO, 1, ain_latency);     /* before the first rfm1_audio_in */
        fprintf(stderr, "rfm1_run: audio in %s: %u frames%s, latency %u frames\n", ain_path, ain_frames,
                ain_once ? " once" : " looped", ain_latency);
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
    const double t_run = now_s();
    double t_chord = 0;
    int halted = 0, chord_on = 0, chord_done = 0;
    uint32_t ms = 0;
    for (; ms < end_ms && ms < 600000u; ms++) {
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
        for (int k = 0; k < ninfo; k++)
            if (ms == info_at[k]) {
                C2(F_UI_INFO, s_str, 1024);
                const char *ui = str(s_str);
                printf("ui@%u: %s\n", ms, ui ? ui : "");
            }
        C1(F_TICK, ms);
        if (ms == 0 || ms - last_frame >= 15u) {
            last_frame = ms;
            C0(F_FRAME);
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
            C2(F_AUDIO, s_audio, FM1P_BLOCK);
            int16_t blk[FM1P_BLOCK * 2];
            if (in_mem(s_audio, sizeof blk))
                memcpy(blk, mem(NULL) + s_audio, sizeof blk);
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
        if (g_err[0])
            break;
        if (ms % 50u == 0 && C0(F_HALTED)) {
            halted = (int)C0(F_HALTED);
            break;
        }
    }
    const uint32_t total_ms = ms;
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
        C0(F_SHUTDOWN);
    }

    const double rms = chord_frames ? sqrt(sumsq / (double)(chord_frames * 2u)) : 0.0;
    const double xrt = wall > 0 ? (total_ms / 1000.0) / wall : 0, xrt_chord = wall_chord > 0 ? seconds / wall_chord : 0;
    printf("%s %s: chord rms %.4f (peak %.3f), %u LCD writes, %.1fx real time (chord %.1fx), load %.1f ms, "
           "boot %.1f ms, %u KiB memory, flash %u KiB, %ld KiB file%s%s\n",
           id, version, rms, peak, lcd_writes, xrt, xrt_chord, load_ms, boot_ms, memsize / 1024u, flash_size / 1024u,
           (long)(n / 1024), info[0] ? "; ui_info: " : "", info);
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
    if (!bad && rms < min_rms)
        bad |= fail("silent: the chord's RMS %.6f is under %.6f", rms, min_rms);
    if (!bad && !lcd_writes)
        bad |= fail("no LCD writes");
    if (ain_path)
        fprintf(stderr, "rfm1_run: audio in: %u blocks fed\n", ain_blocks);
    free(sx);
    free(ain);
    m3_FreeRuntime(g_rt);
    m3_FreeEnvironment(g_env);
    free(bytes);
    return bad ? 1 : 0;
}
