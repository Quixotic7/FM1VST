/* SPDX-License-Identifier: GPL-3.0-only */
/* The fm1core ABI as an .rfm1 module's flat exports (docs/RFM1-FORMAT.md, version 1, section 2). Compiled into every
 * .rfm1 by fm1_add_rfm1 (cmake/FM1Rfm1.cmake); never into a native core.
 *
 * Inside a Wasm module the descriptor's function pointers are indices into the module's function table, which an
 * interpreter's public API (wasm3's, WAMR's) cannot call. So the module exports one function per entry point, each
 * calling through its own descriptor: the host never calls a function pointer, it only reads data (strings, the panel,
 * the keymap, flash_size, button names) out of linear memory at the wasm32 offsets rfm1_layout() gives.
 *
 * Pointers are linear-memory offsets (wasm32: 32 bits). A NULL entry point in the descriptor is a no-op here (or 0;
 * boot_options: 1, as the format says). Every export may be called before rfm1_get: the descriptor is then fetched
 * for this build's ABI. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "fm1core.h"
#ifdef RFM1_META               /* a firmware's RemoteFM1Metadata.json labels (tools/rfm1_labels.py, fm1_add_rfm1) */
#include "rfm1_meta_labels.h"
#endif
#ifndef RFM1_META_LABELS
#define RFM1_META_LABELS 0
#endif

#define RFM1_FORMAT_VERSION 1
#define EXPORT(name) __attribute__((export_name(#name), used))

static const fm1core_t *C;

static const fm1core_t *core(void)
{
    if (!C)
        C = fm1core_get(FM1CORE_ABI);
    return C;
}

EXPORT(rfm1_version) int32_t rfm1_version(void) { return RFM1_FORMAT_VERSION; }

#if RFM1_META_LABELS
/* the author's button names (RemoteFM1Metadata.json labels.buttons) over the glue's: the host gets a copy of the
 * descriptor with those entries replaced (made again at each rfm1_get; the entry points still go to the core's own) */
static fm1core_t M;
static const fm1core_t *with_meta(const fm1core_t *c)
{
    if (!c)
        return c;
    M = *c;
    for (int b = 0; b < FM1P_NB; b++)
        if (RFM1_META_BTN[b])
            M.button_names[b] = RFM1_META_BTN[b];
    return &M;
}
#else
#define with_meta(c) (c)
#endif

EXPORT(rfm1_get) uint32_t rfm1_get(uint32_t abi)
{
    const fm1core_t *c = fm1core_get(abi);
    if (c)
        C = c;
    return (uint32_t)(uintptr_t)with_meta(c);
}

EXPORT(rfm1_alloc) uint32_t rfm1_alloc(uint32_t n) { return (uint32_t)(uintptr_t)malloc(n ? n : 1u); }
EXPORT(rfm1_free) void rfm1_free(uint32_t p) { free((void *)(uintptr_t)p); }

/* the format's section 5: index -> value; -1 past the end */
EXPORT(rfm1_layout) int32_t rfm1_layout(int32_t i)
{
    static const int32_t L[] = {
        sizeof(fm1panel_t),
        offsetof(fm1panel_t, keys), offsetof(fm1panel_t, buttons), offsetof(fm1panel_t, keys_tap),
        offsetof(fm1panel_t, buttons_tap), offsetof(fm1panel_t, enc), offsetof(fm1panel_t, master),
        offsetof(fm1panel_t, key_vel), offsetof(fm1panel_t, key_lean), offsetof(fm1panel_t, led),
        offsetof(fm1panel_t, led_play_green), offsetof(fm1panel_t, lcd), offsetof(fm1panel_t, lcd_writes),
        offsetof(fm1panel_t, btn_id), offsetof(fm1panel_t, enc_id), offsetof(fm1panel_t, enc_dir),
        offsetof(fm1panel_t, ready),
        sizeof(fm1core_t),
        offsetof(fm1core_t, abi_version), offsetof(fm1core_t, id), offsetof(fm1core_t, name),
        offsetof(fm1core_t, version), offsetof(fm1core_t, source_url), offsetof(fm1core_t, family),
        offsetof(fm1core_t, license), offsetof(fm1core_t, gated), offsetof(fm1core_t, flash_size),
        offsetof(fm1core_t, button_names), offsetof(fm1core_t, params), offsetof(fm1core_t, nparams),
        offsetof(fm1core_t, panel), offsetof(fm1core_t, keymap), offsetof(fm1core_t, options),
        offsetof(fm1core_t, credits),
    };
    return i >= 0 && i < (int32_t)(sizeof L / sizeof L[0]) ? L[i] : -1;
}

EXPORT(rfm1_options) void rfm1_options(uint32_t path, int32_t no_flash, int32_t save, int32_t headless)
{
    if (core() && C->options)
        C->options((const char *)(uintptr_t)path, no_flash, save, headless);
}
EXPORT(rfm1_boot_options) int32_t rfm1_boot_options(int32_t fail, uint32_t reason, int32_t stage)
{
    return core() && C->boot_options ? C->boot_options(fail, (const char *)(uintptr_t)reason, stage) : 1;
}
EXPORT(rfm1_init) void rfm1_init(int32_t demo)
{
    if (core() && C->init)
        C->init(demo);
}
#ifdef RFM1_RD1
static void rd_note(void);
static void rd_bridge(void);
#else
#define rd_note() ((void)0)
#define rd_bridge() ((void)0)
#endif
EXPORT(rfm1_tick) void rfm1_tick(uint32_t ms)
{
    rd_note();
    if (core() && C->tick)
        C->tick(ms);
}
EXPORT(rfm1_frame) void rfm1_frame(void)
{
    rd_note();
    if (core() && C->frame)
        C->frame();
    rd_bridge();
}
EXPORT(rfm1_idle) void rfm1_idle(void)
{
    rd_note();
    if (core() && C->idle)
        C->idle();
}
EXPORT(rfm1_audio) void rfm1_audio(uint32_t out, uint32_t frames)
{
    if (core() && C->audio)
        C->audio((int16_t *)(uintptr_t)out, frames);
}
EXPORT(rfm1_midi_in) int32_t rfm1_midi_in(uint32_t pkt) { return core() && C->midi_in ? C->midi_in(pkt) : 0; }
EXPORT(rfm1_midi_out_take) int32_t rfm1_midi_out_take(uint32_t pkt_out)
{
    return core() && C->midi_out_take ? C->midi_out_take((uint32_t *)(uintptr_t)pkt_out) : 0;
}
EXPORT(rfm1_halted) int32_t rfm1_halted(void) { return core() && C->halted ? C->halted() : 0; }
EXPORT(rfm1_shutdown) void rfm1_shutdown(void)
{
    if (core() && C->shutdown)
        C->shutdown();
}
EXPORT(rfm1_flash_stage) int32_t rfm1_flash_stage(uint32_t bytes, uint32_t n)
{
    return core() && C->flash_stage ? C->flash_stage((const uint8_t *)(uintptr_t)bytes, n) : 0;
}
EXPORT(rfm1_flash) uint32_t rfm1_flash(void) { return core() && C->flash ? (uint32_t)(uintptr_t)C->flash() : 0u; }
EXPORT(rfm1_flash_dirty) int32_t rfm1_flash_dirty(void) { return core() && C->flash_dirty ? (int32_t)C->flash_dirty() : 0; }
EXPORT(rfm1_flash_sync) int32_t rfm1_flash_sync(void) { return core() && C->flash_sync ? C->flash_sync() : 1; }
EXPORT(rfm1_stats) void rfm1_stats(uint32_t shed_out, uint32_t cpu_out)
{
    uint32_t shed = 0, cpu = 0;
    if (core() && C->stats)
        C->stats(&shed, &cpu);
    if (shed_out)
        *(uint32_t *)(uintptr_t)shed_out = shed;
    if (cpu_out)
        *(uint32_t *)(uintptr_t)cpu_out = cpu;
}
EXPORT(rfm1_ui_info) void rfm1_ui_info(uint32_t buf, uint32_t n)
{
    char *b = (char *)(uintptr_t)buf;
    if (!b || !n)
        return;
    b[0] = 0;
    if (core() && C->ui_info)
        C->ui_info(b, n);
    b[n - 1] = 0;
}

/* live panel labels (docs/DYNAMIC-LABELS.md): the core's optional symbols through weak references, so every .rfm1
 * exports both; a core without them: epoch 0, label 0 */
extern uint32_t fm1core_labels_epoch(void) __attribute__((weak));
extern const char *fm1core_label(int32_t which) __attribute__((weak));
#ifdef RFM1_RD1                /* (the readouts' knob labels wrap these: below) */
#define LBL_EXPORT(name) static
#define LBL_FN(name) base_##name
#else
#define LBL_EXPORT(name) EXPORT(name)
#define LBL_FN(name) name
#endif
#if RFM1_META_LABELS
/* with the author's static labels (RemoteFM1Metadata.json: RFM1_META_SLOT, knobs and buttons): served while the core
 * has no live labels of its own (no symbols, or its epoch 0); once its epoch is not 0 everything is the core's. The
 * epoch here is the shim's own, moved at each switch between the two and at each move of the core's, so a host never
 * misses a change */
static uint32_t meta_epoch, meta_core_epoch;
static int meta_src;
LBL_EXPORT(rfm1_labels_epoch) uint32_t LBL_FN(rfm1_labels_epoch)(void)
{
    const uint32_t ce = fm1core_labels_epoch ? fm1core_labels_epoch() : 0u;
    const int src = ce ? 2 : 1;
    if (src != meta_src || ce != meta_core_epoch) {
        meta_src = src;
        meta_core_epoch = ce;
        if (++meta_epoch == 0)
            meta_epoch = 1;
    }
    (void)*(volatile const char *)RFM1_META_TAG;   /* (kept in the data: rfm1_check reports the counts) */
    return meta_epoch;
}
LBL_EXPORT(rfm1_label) uint32_t LBL_FN(rfm1_label)(int32_t which)
{
    if (which < 0 || which >= FM1CORE_LBL_N)
        return 0u;
    if (fm1core_labels_epoch && fm1core_labels_epoch())
        return fm1core_label ? (uint32_t)(uintptr_t)fm1core_label(which) : 0u;
    return (uint32_t)(uintptr_t)RFM1_META_SLOT[which];
}
#else
LBL_EXPORT(rfm1_labels_epoch) uint32_t LBL_FN(rfm1_labels_epoch)(void)
{
    return fm1core_labels_epoch ? fm1core_labels_epoch() : 0u;
}
LBL_EXPORT(rfm1_label) uint32_t LBL_FN(rfm1_label)(int32_t which)
{
    return fm1core_label && which >= 0 && which < FM1CORE_LBL_N ? (uint32_t)(uintptr_t)fm1core_label(which) : 0u;
}
#endif

/* audio input (docs/VIRTUAL-CARTS.md 8, capability ain1): only in a module built with ain1 in its fm1_add_rfm1 WANTS or
 * NEEDS (RFM1_AIN1), so a cart without it exports neither and rfm1_check can require ain1 of a cart that does. The
 * core's optional symbols, through weak references (a core without them: no-ops). Until the next ABI bump these are
 * module symbols, not descriptor fields: a native host does not see them yet. */
#ifdef RFM1_AIN1
extern void fm1core_audio_in(const int16_t *stereo, uint32_t frames) __attribute__((weak));
extern void fm1core_audio_in_info(uint32_t state, uint32_t latency_frames) __attribute__((weak));
EXPORT(rfm1_audio_in) void rfm1_audio_in(uint32_t stereo, uint32_t frames)
{
    if (fm1core_audio_in)
        fm1core_audio_in((const int16_t *)(uintptr_t)stereo, frames);
}
EXPORT(rfm1_audio_in_info) void rfm1_audio_in_info(uint32_t state, uint32_t latency_frames)
{
    if (fm1core_audio_in_info)
        fm1core_audio_in_info(state, latency_frames);
}
#endif

/* more output channels (aout1, RemoteFM1's docs/RFM1-FORMAT.md section 9): only in a module built with AUDIO_OUT in
 * its fm1_add_rfm1 (RFM1_AOUT1), so a cart that gives the master alone exports neither. The core's optional symbols
 * (fm1core.h), through weak references: a core without them says 2 (the master alone) and takes no channels */
#ifdef RFM1_AOUT1
extern uint32_t fm1core_audio_out_info(void) __attribute__((weak));
extern void fm1core_audio_out(int16_t *buf, uint32_t frames) __attribute__((weak));
EXPORT(rfm1_audio_out_info) uint32_t rfm1_audio_out_info(void)
{
    return fm1core_audio_out_info ? fm1core_audio_out_info() : 2u;
}
EXPORT(rfm1_audio_out) void rfm1_audio_out(uint32_t buf, uint32_t frames)
{
    if (fm1core_audio_out)
        fm1core_audio_out((int16_t *)(uintptr_t)buf, frames);
}
#endif

/* core 1 (docs/RFM1.md "cpu2"): only in a module built with cpu2 in its fm1_add_rfm1 WANTS (RFM1_CPU2). No import: the
 * module's rfm1_cpu2_run / rfm1_cpu2_wait (rfm1_cpu2.h) call through two entries of its own function table, whose
 * indices the record rfm1_cpu2() gives. As built, the entries are the one-core answers (run 0: run it yourself), so a
 * host that knows nothing of cpu2 runs the module as any other. A host with a thread for core 1 puts its own run and
 * wait into those two table entries of the instance (the same signatures), and runs a job by calling rfm1_c1_call on
 * a twin of the instance (the same module and memory, its own globals and table, so its own stack pointer and its own
 * one-core answers: a job never hands a job on), after rfm1_c1_stack has put the twin's stack at top. */
#ifdef RFM1_CPU2
#include "rfm1_cpu2.h"
typedef struct {
    uint16_t version, size;            /* 1, sizeof */
    volatile uint32_t run;             /* table index: int32_t (uint32_t fn, uint32_t arg); 1 handed over, 0 not */
    volatile uint32_t wait;            /* table index: int32_t (void); 0 the job is done, else run it yourself */
} rfm1_cpu2_rec_t;
static int32_t rfm1_cpu2_run_none(uint32_t fn, uint32_t arg) { (void)fn, (void)arg; return 0; }
static int32_t rfm1_cpu2_wait_none(void) { return -1; }
static rfm1_cpu2_rec_t rfm1_cpu2_rec = {1, sizeof(rfm1_cpu2_rec_t), 0, 0};
static volatile uint32_t rfm1_c1_lo, rfm1_c1_hi;   /* core 1's stack, once a host set it (rfm1_cpu2_core) */
/* emscripten's stack (system/lib/compiler-rt/stack_ops.S, stack_limits.S) */
void _emscripten_stack_restore(uintptr_t sp);
void emscripten_stack_set_limits(void *base, void *end);
uintptr_t emscripten_stack_get_current(void);
#define RFM1_C1_STACK_MIN (64u * 1024u)   /* what a host gives core 1 at the least (it gives 256 KiB) */

static rfm1_cpu2_rec_t *cpu2_rec(void)
{
    if (!rfm1_cpu2_rec.run) {          /* (the indices are the linker's: set on first use, before any host reads them) */
        rfm1_cpu2_rec.run = (uint32_t)(uintptr_t)rfm1_cpu2_run_none;
        rfm1_cpu2_rec.wait = (uint32_t)(uintptr_t)rfm1_cpu2_wait_none;
    }
    return &rfm1_cpu2_rec;
}
EXPORT(rfm1_cpu2) uint32_t rfm1_cpu2(void) { return (uint32_t)(uintptr_t)cpu2_rec(); }
EXPORT(rfm1_c1_stack) void rfm1_c1_stack(uint32_t top)
{
    top &= ~15u;
    emscripten_stack_set_limits((void *)(uintptr_t)top, (void *)(uintptr_t)(top - RFM1_C1_STACK_MIN));
    _emscripten_stack_restore(top);
    rfm1_c1_lo = top - RFM1_C1_STACK_MIN, rfm1_c1_hi = top;
}
EXPORT(rfm1_c1_call) void rfm1_c1_call(uint32_t fn, uint32_t arg) { ((void (*)(uint32_t))(uintptr_t)fn)(arg); }
int rfm1_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    const rfm1_cpu2_rec_t *r = cpu2_rec();
    return ((int32_t (*)(uint32_t, uint32_t))(uintptr_t)r->run)((uint32_t)(uintptr_t)fn, arg) == 1;
}
int rfm1_cpu2_wait(void)
{
    const rfm1_cpu2_rec_t *r = cpu2_rec();
    return (int)((int32_t (*)(void))(uintptr_t)r->wait)();
}
int rfm1_cpu2_core(void)
{
    const uint32_t sp = (uint32_t)emscripten_stack_get_current();
    return rfm1_c1_hi && sp > rfm1_c1_lo && sp <= rfm1_c1_hi;
}
#endif

/* readouts (docs/RFM1.md "Readouts (rd1)"): only in a module built with rd1 in its fm1_add_rfm1 WANTS (RFM1_RD1; its
 * units get FM1_HOST_EXT, so core-api/rfm1_ext.h's macros reach rfm1_readout_push). One record at a stable address,
 * rfm1_readout() -> ptr; the host reads its seq after each rfm1_frame (no call) and shows a new one.
 *   The firmware's own readouts (RFM1_READOUT, RFM1_PRESET, RFM1_MESSAGE) always win. A core with the Tier 2 map's
 * knob_target and params gets one without a line of its own: a knob whose detents the core took this frame, and that
 * the core said nothing about, is read out as its target's name and value text (the bridge).
 *   Live labels for the knobs too: a core without labels of its own has its knobs named after their targets' names
 * (knob_target, as of param_epoch), over the static labels of the shim (RemoteFM1Metadata.json) where a knob has a
 * target; the rest as before. */
#ifdef RFM1_RD1
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "rfm1_ext.h"
typedef struct {
    uint16_t version, size;    /* 1, sizeof (48) */
    uint32_t seq;              /* moves on every new readout; 0: none yet */
    int8_t control;            /* 0..48: the DYNAMIC-LABELS slot it belongs to; -1: the control in use */
    uint8_t kind;              /* RFM1_RD_PARAM, _PRESET, _MESSAGE */
    uint8_t flags;             /* RFM1_RD_BIPOLAR */
    uint8_t ms10;              /* how long, x 10 ms; 0: the host's default */
    int16_t frac;              /* 0..1000 a bar; -1 none */
    char title[17], value[17]; /* printable ASCII, NUL-terminated */
} rfm1_readout_t;
typedef char rfm1_readout_size_ok[sizeof(rfm1_readout_t) == 48 ? 1 : -1];
static rfm1_readout_t RD = {1, sizeof(rfm1_readout_t), 0, -1, 0, 0, 0, -1, "", ""};
static uint32_t rd_pushes, rd_seen;   /* the core's own readouts; the count the bridge last saw */
static uint8_t rd_pending;            /* knobs (FM1P_E_SELECT .. K4) with detents waiting for the core */

static void rd_txt(char *d, const char *s, uint32_t n)   /* printable ASCII only, cut at n - 1 */
{
    uint32_t i = 0;
    while (s && *s && i + 1 < n) {
        const unsigned char c = (unsigned char)*s++;
        d[i++] = c >= 32 && c < 127 ? (char)c : c == 0xB7 ? '.' : ' ';
    }
    d[i] = 0;
}
static void rd_set(int ctl, int kind, unsigned flags, int frac, const char *title, const char *value, const char *sub)
{
    char v[40];
    rd_txt(RD.title, title, sizeof RD.title);
    rd_txt(v, value, sizeof v);
    if (sub && *sub) {
        size_t k = strlen(v);
        if (k && k + 1 < sizeof v)
            v[k++] = ' ', v[k] = 0;
        rd_txt(v + k, sub, (uint32_t)(sizeof v - k));
    }
    rd_txt(RD.value, v, sizeof RD.value);
    RD.control = (int8_t)(ctl >= -1 && ctl < FM1CORE_LBL_N ? ctl : -1);
    RD.kind = (uint8_t)kind;
    RD.flags = (uint8_t)flags;
    RD.ms10 = 0;
    RD.frac = (int16_t)(frac < 0 ? -1 : frac > 1000 ? 1000 : frac);
    if (++RD.seq == 0)
        RD.seq = 1;
}
void rfm1_readout_push(int ctl, int kind, unsigned flags, int frac, const char *title, const char *value,
                       const char *sub)
{
    rd_set(ctl, kind, flags, frac, title, value, sub);
    rd_pushes++;
}
EXPORT(rfm1_readout) uint32_t rfm1_readout(void) { return (uint32_t)(uintptr_t)&RD; }

static const fm1param_t *rd_target(int role)   /* what knob role turns now, or 0 */
{
    int32_t i;
    if (!core() || !C->knob_target || !C->params || !C->nparams)
        return 0;
    i = C->knob_target(role);
    return i >= 0 && (uint32_t)i < C->nparams ? &C->params[i] : 0;
}
static void rd_note(void)                       /* (each call in: the knobs whose detents wait) */
{
    const fm1panel_t *P;
    int r;
    if (!core() || !(P = C->panel))
        return;
    for (r = FM1P_E_SELECT; r <= FM1P_E_K4; r++)
        if (P->enc[P->enc_id[r]] != 0)
            rd_pending |= (uint8_t)(1u << r);
}
static void rd_bridge(void)                     /* (after a frame: the knobs the core took and said nothing about) */
{
    const fm1panel_t *P;
    int r;
    if (!rd_pending || !core() || !(P = C->panel))
        return;
    if (rd_pushes != rd_seen || !C->knob_target || !C->params) {   /* the core spoke (or has no map): its own wins */
        rd_seen = rd_pushes;
        rd_pending = 0;
        return;
    }
    if (C->param_epoch)
        (void)C->param_epoch();
    for (r = FM1P_E_SELECT; r <= FM1P_E_K4; r++) {
        const fm1param_t *p;
        char t[24];
        int32_t v;
        if (!(rd_pending >> r & 1u) || P->enc[P->enc_id[r]] != 0)
            continue;                           /* (not turned, or not taken yet: the next frame) */
        rd_pending &= (uint8_t)~(1u << r);
        if (!(p = rd_target(r)) || !p->get)
            continue;
        v = p->get();
        t[0] = 0;
        if (p->text)
            p->text(v, t, sizeof t);
        else if (p->names && v >= p->min && v <= p->max)
            rd_txt(t, p->names[v - p->min], sizeof t);
        else
            snprintf(t, sizeof t, "%ld%s", (long)v, p->unit ? p->unit : "");
        rd_set(FM1CORE_LBL_KNOB0 + r, RFM1_RD_PARAM, p->min < 0 && p->max > 0 ? RFM1_RD_BIPOLAR : 0u,
               p->max > p->min ? (int)(((int64_t)v - p->min) * 1000 / ((int64_t)p->max - p->min)) : -1, p->name, t, 0);
    }
    rd_seen = rd_pushes;
}

/* the knobs' live labels from the map (a core without labels of its own) */
static char rd_kl[FM1P_E_K4 + 1][FM1CORE_LBL_LEN];
static uint32_t rd_lbl_epoch, rd_lbl_base;
static int rd_lbl_src;
static int rd_knob_labels(void)                 /* 1: changed */
{
    char n[FM1P_E_K4 + 1][FM1CORE_LBL_LEN];
    int r, k;
    memset(n, 0, sizeof n);
    if (C && C->knob_target && C->params) {
        if (C->param_epoch)
            (void)C->param_epoch();
        for (r = FM1P_E_SELECT; r <= FM1P_E_K4; r++) {
            const fm1param_t *p = rd_target(r);
            if (p && p->name) {
                rd_txt(n[r], p->name, FM1CORE_LBL_LEN);
                for (k = 0; n[r][k]; k++)
                    if (n[r][k] >= 'a' && n[r][k] <= 'z')
                        n[r][k] = (char)(n[r][k] - 32);
            }
        }
    }
    if (!memcmp(n, rd_kl, sizeof n))
        return 0;
    memcpy(rd_kl, n, sizeof n);
    return 1;
}
EXPORT(rfm1_labels_epoch) uint32_t rfm1_labels_epoch(void)
{
    const uint32_t be = base_rfm1_labels_epoch();
    const int own = fm1core_labels_epoch && fm1core_labels_epoch() != 0;
    int moved = be != rd_lbl_base || (own ? 2 : 1) != rd_lbl_src;
    rd_lbl_base = be;
    rd_lbl_src = own ? 2 : 1;
    if (!own && core())
        moved |= rd_knob_labels();
    if (moved && ++rd_lbl_epoch == 0)
        rd_lbl_epoch = 1;
    return rd_lbl_epoch;
}
EXPORT(rfm1_label) uint32_t rfm1_label(int32_t which)
{
    const int own = fm1core_labels_epoch && fm1core_labels_epoch() != 0;
    if (!own && which >= FM1CORE_LBL_KNOB0 && which <= FM1CORE_LBL_KNOB0 + FM1P_E_K4 && rd_kl[which][0])
        return (uint32_t)(uintptr_t)rd_kl[which];
    return base_rfm1_label(which);
}
#endif
