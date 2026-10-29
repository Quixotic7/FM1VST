/* SPDX-License-Identifier: GPL-3.0-only */
/* rfm1_run: an .rfm1 file (docs/RFM1-FORMAT.md, version 1) booted and played in wasm3 v0.5.0 (third_party/wasm3), as a
 * host would run it: the imports of section 3, the checks of section 7, a fresh flash, the native ABI's device clock
 * (fm1core.h: tick once per device ms, frame every 15 ms else idle, audio per 128-frame block due at 44.1 kHz).
 *
 *   rfm1_run [options] <file.rfm1>
 *     --seconds S    the chord held for S device seconds (default 5)
 *     --settle MS    device ms after init before the chord (default 4000)
 *     --keys K,K,..  the chord's key numbers (default 9,13,16: D4 F#4 A4, as core_bench / core_conformance), velocity 100
 *     --sysex FILE   a .syx file sent over midi_in from settle - 800 ms (a sample bank, a preset); the chord waits
 *                    until it is all in, then 300 ms
 *     --wav FILE     the whole run's audio as a 44.1 kHz 16-bit stereo WAV
 *     --min-rms X    the chord's RMS below this (full scale 1.0) fails as silent (default 0.0005)
 *     --verbose      the firmware's stdout / stderr through
 *     --describe     no run: the descriptor as JSON (stdout, or --out FILE); works on a bare module (no manifest):
 *                    fm1_add_rfm1 makes the manifest from it
 *
 * Prints one line: id version, the chord's RMS and peak, LCD writes, x real time, load and boot times. Exits 0 when
 * the module loaded, its layout and manifest agree, it booted, drew and sounded, and never trapped or halted; 1 on a
 * failure (said on stderr), 2 on a usage or file error. */
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fm1core.h"
#include "wasm3.h"

/* ------------------------------------------------------------------------------------------------ the exports --- */
enum {
    F_INITIALIZE, F_VERSION, F_GET, F_LAYOUT, F_ALLOC, F_FREE, F_OPTIONS, F_BOOT_OPTIONS, F_INIT, F_TICK, F_FRAME,
    F_IDLE, F_AUDIO, F_MIDI_IN, F_MIDI_OUT_TAKE, F_HALTED, F_SHUTDOWN, F_FLASH_STAGE, F_FLASH, F_FLASH_DIRTY,
    F_FLASH_SYNC, F_STATS, F_UI_INFO, F_N
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

static void usage(void)
{
    fputs("usage: rfm1_run [--seconds S] [--settle MS] [--keys K,K,..] [--sysex FILE] [--wav FILE] [--min-rms X]\n"
          "                [--verbose] [--describe [--out FILE]] <file.rfm1>\n", stderr);
}

int main(int argc, char **argv)
{
    double seconds = 5.0, min_rms = 0.0005;
    uint32_t settle_ms = 4000;
    int describe = 0;
    const char *out_path = NULL, *wav_path = NULL, *sysex_path = NULL, *path = NULL;
    int keys[FM1P_NKEY], nkeys = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const int more = i + 1 < argc;
        if (!strcmp(a, "--seconds") && more) seconds = atof(argv[++i]);
        else if (!strcmp(a, "--settle") && more) settle_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--min-rms") && more) min_rms = atof(argv[++i]);
        else if (!strcmp(a, "--wav") && more) wav_path = argv[++i];
        else if (!strcmp(a, "--sysex") && more) sysex_path = argv[++i];
        else if (!strcmp(a, "--out") && more) out_path = argv[++i];
        else if (!strcmp(a, "--describe")) describe = 1;
        else if (!strcmp(a, "--verbose")) g_verbose = 1;
        else if (!strcmp(a, "--keys") && more) {
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
    if (!path) {
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
    r = m3_CompileModule(mod);
    if (r)
        return fail("compile: %s (an import outside the format's list, or an instruction wasm3 0.5.0 lacks)", r);
    for (int f = 0; f < F_N; f++) {
        r = m3_FindFunction(&g_fn[f], g_rt, FN[f].name);
        if (r) {
            g_fn[f] = NULL;
            if (f == F_INITIALIZE)
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
        fprintf(o, ",\n  \"memory_bytes\": %u\n}\n", memsize);
        if (out_path)
            fclose(o);
        return 0;
    }
    if (strcmp(id, m_id) || strcmp(name, m_name) || strcmp(version, m_version))
        return fail("the manifest (%s, %s, %s) and the descriptor (%s, %s, %s) disagree", m_id, m_name, m_version,
                    id, name, version);

    /* boot from a fresh flash (FM1CORE_FLASH_STAGED with no bytes), as Device::boot_from */
    const uint32_t scr = C1(F_ALLOC, 8192);
    if (!scr || !in_mem(scr, 8192))
        return fail("rfm1_alloc(8192) = %u%s%s", scr, g_err[0] ? ": " : "", g_err);
    const uint32_t s_str = scr, s_audio = scr + 1024, s_words = scr + 4096;
    const uint32_t lcd0 = rd32(P(lcd_writes));   /* (init's first screen counts) */
    const double t_boot = now_s();
    C2(F_FLASH_STAGE, 0, 0);
    memcpy(mem(NULL) + s_str, FM1CORE_FLASH_STAGED, sizeof FM1CORE_FLASH_STAGED);
    call(F_OPTIONS, s_str, 0, 0, 1);
    call(F_BOOT_OPTIONS, (uint32_t)-1, 0, (uint32_t)-1, 0);
    wr8(P(ready), 2);
    C1(F_INIT, 0);
    const double boot_ms = (now_s() - t_boot) * 1000.0;
    if (g_err[0])
        return fail("init trapped: %s", g_err);

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
            uint32_t out;
            while (C1(F_MIDI_OUT_TAKE, s_words) && !g_err[0])
                (void)(out = rd32(s_words));
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
        if (!chord_on && !chord_done && ms >= chord_at && (!sx || (!pending_pkt && sx_at >= sx_n))) {
            /* the chord down (Device::keys: taps for the new keys) */
            for (int k = 0; k < nkeys; k++)
                wr8(P(key_vel) + (uint32_t)keys[k], 100);
            const uint32_t held = rd32(P(keys));
            wr32(P(keys_tap), rd32(P(keys_tap)) | (chord_mask & ~held));
            wr32(P(keys), chord_mask);
            t_chord = now_s();
            chord_on = 1;
            chord_at = ms;
        }
        if (chord_on && ms == chord_at + chord_ms) {   /* and up; 200 ms more */
            wr32(P(keys), 0);
            wall_chord = now_s() - t_chord;
            chord_on = 0;
            chord_done = 1;
            end_ms = ms + 200u;
        }
        C1(F_TICK, ms);
        if (ms == 0 || ms - last_frame >= 15u) {
            last_frame = ms;
            C0(F_FRAME);
        } else
            C0(F_IDLE);
        while (frames_done + FM1P_BLOCK <= (uint64_t)ms * FM1P_FS / 1000u) {
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
    if (sx && !g_err[0] && !halted)
        fprintf(stderr, "rfm1_run: %s: %u SysEx packets in from %u ms, the last at %u ms\n", sysex_path, sx_packets,
                sx_from, sx_done_ms);
    const double wall = now_s() - t_run;
    const uint32_t lcd_writes = rd32(P(lcd_writes)) - lcd0;
    if (wav) {
        wav_header(wav, (uint32_t)wav_frames);
        fclose(wav);
    }
    char info[256] = "";
    if (!g_err[0]) {
        C2(F_UI_INFO, s_str, sizeof info);
        const char *ui = str(s_str);
        if (ui)
            snprintf(info, sizeof info, "%s", ui);
        C2(F_STATS, s_words, s_words + 4);
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
    if (!bad && rms < min_rms)
        bad |= fail("silent: the chord's RMS %.6f is under %.6f", rms, min_rms);
    if (!bad && !lcd_writes)
        bad |= fail("no LCD writes");
    free(sx);
    m3_FreeRuntime(g_rt);
    m3_FreeEnvironment(g_env);
    free(bytes);
    return bad ? 1 : 0;
}
