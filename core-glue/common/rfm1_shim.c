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
#ifdef RFM1_EP1
static void ep_frame(void);
#else
#define ep_frame() ((void)0)
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
    ep_frame();
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
/* (the adapter hooks, declared here too: a core may put its own older copy of rfm1_ext.h first on the include path) */
int fm1core_rd_knob(int role, char *title, char *value, unsigned n, int32_t *frac, unsigned *flags) __attribute__((weak));
void fm1core_rd_turn(int role) __attribute__((weak));
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
static uint8_t rd_retry[FM1P_E_K4 + 1];   /* an adapter's: frames left to ask again (its screen may draw later) */
#define RD_RETRY_FRAMES 8

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
        if (P->enc[P->enc_id[r]] != 0 && !(rd_pending >> r & 1u)) {
            rd_pending |= (uint8_t)(1u << r);
            if (fm1core_rd_turn)                /* (an adapter: the value before the core takes the turn) */
                fm1core_rd_turn(r);
        }
}
static void rd_bridge(void)                     /* (after a frame: the knobs the core took and said nothing about) */
{
    const fm1panel_t *P;
    int r;
    if (!core() || !(P = C->panel))
        return;
    const int map = C->knob_target && C->params;
    if (!map && fm1core_rd_knob)                /* an adapter's knobs still waiting for their screen */
        for (r = FM1P_E_SELECT; r <= FM1P_E_K4; r++)
            if (rd_retry[r] && !(rd_pending >> r & 1u)) {
                char t[24], val[24];
                int32_t fr = -1;
                uint32_t fl = 0;
                int kind;
                if (rd_pushes != rd_seen) {     /* (the core spoke: its own wins) */
                    rd_retry[r] = 0;
                    continue;
                }
                rd_retry[r]--;
                kind = fm1core_rd_knob(r, t, val, sizeof t, &fr, &fl);
                t[sizeof t - 1] = val[sizeof val - 1] = 0;
                if (kind == RFM1_RD_PARAM || kind == RFM1_RD_PRESET) {
                    rd_set(kind == RFM1_RD_PARAM ? FM1CORE_LBL_KNOB0 + r : -1, kind, fl, fr, t, val, 0);
                    rd_retry[r] = 0;
                }
            }
    if (!rd_pending)
        return;
    if (rd_pushes != rd_seen || (!map && !fm1core_rd_knob)) {   /* the core spoke (or has no map, no adapter): its own wins */
        rd_seen = rd_pushes;
        rd_pending = 0;
        return;
    }
    if (map && C->param_epoch)
        (void)C->param_epoch();
    for (r = FM1P_E_SELECT; r <= FM1P_E_K4; r++) {
        const fm1param_t *p;
        char t[24];
        int32_t v;
        if (!(rd_pending >> r & 1u) || P->enc[P->enc_id[r]] != 0)
            continue;                           /* (not turned, or not taken yet: the next frame) */
        rd_pending &= (uint8_t)~(1u << r);
        if (!map) {                             /* the glue's adapter (rfm1_ext.h fm1core_rd_knob): its words */
            char val[24];
            int32_t fr = -1;
            uint32_t fl = 0;
            const int kind = fm1core_rd_knob(r, t, val, sizeof t, &fr, &fl);
            t[sizeof t - 1] = val[sizeof val - 1] = 0;
            if (kind == RFM1_RD_PARAM || kind == RFM1_RD_PRESET)
                rd_set(kind == RFM1_RD_PARAM ? FM1CORE_LBL_KNOB0 + r : -1, kind, fl, fr, t, val, 0);
            else
                rd_retry[r] = RD_RETRY_FRAMES;  /* (nothing yet: asked again the next frames) */
            continue;
        }
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

/* engine parameters (engine-params/SPEC.md, capability ep1; docs/RFM1.md "Engine parameters (ep1)"): only in a module
 * built with ep1 in its fm1_add_rfm1 WANTS (RFM1_EP1; its units get FM1_HOST_EP1, so core-api/rfm1_ep.h declares
 * fm1core_ep). The firmware's tables (fm1core_ep(), asked once after init) become the EP1D description blob; one EP1R
 * record at a stable address (rfm1_ep() -> ptr) holds the layers, every value and the watched page's cells, refreshed
 * after each rfm1_frame and after each rfm1_ep_set; each part's seq moves (+1, never 0) after its data is written, and
 * only on a change. rfm1_ep_set (SET; LOCK / FREE / FREE_ALL are stage 3: -3 for now) calls the firmware's set with
 * rfm1_ep_host = 1 and drops the readouts pushed meanwhile (a host write is never drawn by the firmware). */
#ifdef RFM1_EP1
#include <string.h>
#include "rfm1_ep.h"
#define EP_DESC_MAX 65536u
#define EP_LAYERS 8
#define EP_PARAMS 2048
typedef struct {
    uint16_t version, size;            /* 1, 264 */
    uint32_t desc_seq, desc_ptr, desc_len;
    uint32_t lay_seq;
    uint8_t nlayers, rsv0[3];
    uint8_t lay[EP_LAYERS][4];         /* engine (0xFF none), midi_ch (0xFF none), flags, 0 */
    uint32_t val_seq, val_ptr;
    uint32_t cell_seq;
    uint16_t wpage;                    /* the watched page, 0xFFFF none */
    uint8_t wlayer, rsv1;
    struct { uint16_t pid; char name[11], text[11]; } cell[8];
} rfm1_ep_rec_t;
_Static_assert(offsetof(rfm1_ep_rec_t, desc_seq) == 4 && offsetof(rfm1_ep_rec_t, lay_seq) == 16, "ep1 record");
_Static_assert(offsetof(rfm1_ep_rec_t, nlayers) == 20 && offsetof(rfm1_ep_rec_t, lay) == 24, "ep1 record");
_Static_assert(offsetof(rfm1_ep_rec_t, val_seq) == 56 && offsetof(rfm1_ep_rec_t, cell_seq) == 64, "ep1 record");
_Static_assert(offsetof(rfm1_ep_rec_t, wpage) == 68 && offsetof(rfm1_ep_rec_t, wlayer) == 70, "ep1 record");
_Static_assert(offsetof(rfm1_ep_rec_t, cell) == 72 && sizeof(rfm1_ep_rec_t) == 264, "ep1 record: 264 bytes");
static rfm1_ep_rec_t EPR = {1, sizeof(rfm1_ep_rec_t), 0, 0, 0, 0, 0, {0, 0, 0}, {{0}}, 0, 0, 0, 0xFFFFu, 0, 0, {{0}}};
volatile int rfm1_ep_host;
static const rfm1_ep_src_t *EPS;
static uint8_t *ep_blob;               /* the description */
static int16_t *ep_val;                /* [nlayers][nparams] */
static uint16_t *ep_order;             /* param indices sorted by pid (set's lookup) */
static uint8_t *ep_on;                 /* [nengines][nparams]: the param is on that engine's pages (or GLOBAL) */
static int ep_state;                   /* 0 not tried, 1 on, -1 none */
static uint32_t ep_epoch;
static int16_t ep_wpage = -1;
static uint8_t ep_wlayer;
static uint32_t ep_nsets;              /* SETs applied (rfm1_run --ep reports it) */
/* the holds (SPEC.md 5): per layer up to EP_HOLDS parameters a host LOCKed, each with its base (the value outside the
 * lock: what FREE writes back) and the value it locked (what the firmware read after the write: a firmware that
 * writes the parameter itself makes them differ, and the hold is dropped, its value the new base). An engine switch
 * drops the layer's holds */
#define EP_HOLDS 64
typedef struct { uint16_t x; int16_t base, val; } ep_hold_t;
static ep_hold_t ep_hold[EP_LAYERS][EP_HOLDS];
static uint8_t ep_nhold[EP_LAYERS], ep_hold_eng[EP_LAYERS];
static uint32_t ep_nlocks, ep_nfrees, ep_ndropped;

static uint32_t ep_next(uint32_t s) { return s + 1u ? s + 1u : 1u; }
static void ep_name(char *d, const char *s, uint32_t n)   /* printable ASCII, NUL-padded, cut at n - 1 */
{
    uint32_t i = 0;
    while (s && *s && i + 1 < n) {
        const unsigned char c = (unsigned char)*s++;
        d[i++] = c >= 32 && c < 127 ? (char)c : ' ';
    }
    while (i < n)
        d[i++] = 0;
}
static void ep_w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void ep_w32(uint8_t *p, uint32_t v) { ep_w16(p, v), ep_w16(p + 2, v >> 16); }
static int ep_pidx(uint16_t pid)       /* the param index of pid, -1 none (binary search) */
{
    int lo = 0, hi = (int)EPS->nparams - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2, p = EPS->params[ep_order[mid]].pid;
        if (p == pid)
            return ep_order[mid];
        if (p < pid)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

/* the blob (SPEC.md 3): header, layers, engines, pages, params, tables, strings. A TABLE's strings: its names, or its
 * text() at each value; two params whose tables are the same strings share one. 0: it does not fit */
static int ep_build(void)
{
    const rfm1_ep_src_t *s = EPS;
    uint32_t npages = 0, ntab = 0, e, p, k, i, sl = 0, off, total;
    static uint8_t tmp[EP_DESC_MAX];   /* (the strings, then copied after the fixed sections) */
    uint16_t *ptab = malloc(sizeof(uint16_t) * s->nparams);
    uint32_t *toff = malloc(sizeof(uint32_t) * s->nparams), *tlen = malloc(sizeof(uint32_t) * s->nparams);
    uint16_t *tcnt = malloc(sizeof(uint16_t) * s->nparams);
    int16_t *tfirst = malloc(sizeof(int16_t) * s->nparams);
    if (!ptab || !toff || !tlen || !tcnt || !tfirst)
        return 0;
    for (e = 0; e < s->nengines; e++)
        npages += s->engines[e].npages;
    for (p = 0; p < s->nparams; p++) {
        const rfm1_ep_param_t *q = &s->params[p];
        uint32_t a = sl, j;
        ptab[p] = 0xFFFFu;
        if (q->fmt != RFM1_EP_TABLE)
            continue;
        for (j = 0; j < p; j++)        /* (the same names, the same range: the same table) */
            if (q->names && s->params[j].fmt == RFM1_EP_TABLE && s->params[j].names == q->names &&
                s->params[j].min == q->min && s->params[j].max == q->max) {
                ptab[p] = ptab[j];
                break;
            }
        if (ptab[p] != 0xFFFFu)
            continue;
        for (i = 0; i <= (uint32_t)(q->max - q->min); i++) {
            char b[24];
            b[0] = 0;
            if (q->names)
                ep_name(b, q->names[i], 11);
            else if (q->text) {
                q->text(q->min + (int32_t)i, b, sizeof b);
                ep_name(b, b, 11);
            }
            k = (uint32_t)strlen(b) + 1u;
            if (sl + k > sizeof tmp)
                return 0;
            memcpy(tmp + sl, b, k);
            sl += k;
        }
        for (j = 0; j < ntab; j++)     /* (the same strings as a table made before) */
            if (tcnt[j] == q->max - q->min + 1 && tfirst[j] == q->min && tlen[j] == sl - a &&
                !memcmp(tmp + toff[j], tmp + a, sl - a))
                break;
        if (j < ntab)
            sl = a;
        else {
            toff[ntab] = a, tlen[ntab] = sl - a, tcnt[ntab] = (uint16_t)(q->max - q->min + 1), tfirst[ntab] = q->min;
            ntab++;
        }
        ptab[p] = (uint16_t)j;
    }
    off = 32u + 16u * s->nlayers + 16u * s->nengines + 32u * npages + 32u * s->nparams + 8u * ntab;
    total = off + sl;
    if (total > EP_DESC_MAX || !(ep_blob = calloc(1, total)))
        return 0;
    {
        uint8_t *b = ep_blob, *q = b + 32;
        uint32_t pg = 0, h = 2166136261u;
        memcpy(b, "EP1D", 4);
        ep_w16(b + 4, 1), ep_w16(b + 6, 32), ep_w16(b + 8, s->nparams);
        b[10] = s->nengines, b[11] = s->nlayers;
        ep_w16(b + 12, npages), ep_w16(b + 14, ntab), ep_w32(b + 16, total);
        for (i = 0; i < s->nlayers; i++, q += 16) {
            ep_name((char *)q, s->layer_names[i], 11);
            q[11] = s->layer_flags;
        }
        for (e = 0; e < s->nengines; e++, q += 16) {
            ep_w16(q, s->engines[e].id);
            ep_name((char *)q + 2, s->engines[e].name, 11);
            q[13] = s->engines[e].npages;
            ep_w16(q + 14, pg);
            pg += s->engines[e].npages;
        }
        for (e = 0; e < s->nengines; e++)
            for (p = 0; p < s->engines[e].npages; p++, q += 32) {
                const rfm1_ep_page_t *g = &s->engines[e].pages[p];
                q[0] = g->id;
                ep_name((char *)q + 1, g->name, 11);
                for (k = 0; k < 8; k++) {
                    const int x = g->pid[k] ? ep_pidx(g->pid[k]) : -1;
                    ep_w16(q + 12 + 2 * k, x < 0 ? 0xFFFFu : (uint32_t)x);
                }
                q[28] = g->group;
            }
        for (p = 0; p < s->nparams; p++, q += 32) {
            const rfm1_ep_param_t *r = &s->params[p];
            ep_w16(q, r->pid);
            ep_name((char *)q + 2, r->name, 11);
            q[13] = r->flags;
            ep_w16(q + 14, (uint16_t)r->min), ep_w16(q + 16, (uint16_t)r->max), ep_w16(q + 18, (uint16_t)r->def);
            q[20] = r->fmt, q[21] = r->arg;
            ep_w16(q + 22, ptab[p]);
            ep_name((char *)q + 24, r->unit, 5);
            q[29] = r->step, q[30] = r->cc;
        }
        for (i = 0; i < ntab; i++, q += 8) {
            ep_w32(q, toff[i]);
            ep_w16(q + 4, tcnt[i]), ep_w16(q + 6, (uint16_t)tfirst[i]);
        }
        memcpy(q, tmp, sl);
        for (i = 32; i < total; i++)   /* FNV-1a 32 of everything after the header */
            h = (h ^ b[i]) * 16777619u;
        ep_w32(b + 20, h);
    }
    free(ptab), free(toff), free(tlen), free(tcnt), free(tfirst);
    EPR.desc_ptr = (uint32_t)(uintptr_t)ep_blob;
    EPR.desc_len = total;
    EPR.desc_seq = ep_next(EPR.desc_seq);
    return 1;
}

static void ep_layers(void)            /* the layers' engines and channels: lay_seq when one changed */
{
    uint8_t n[EP_LAYERS][4];
    int i;
    memset(n, 0, sizeof n);
    for (i = 0; i < EPS->nlayers; i++) {
        int e = -1, ch = -1;
        EPS->layer(i, &e, &ch);
        n[i][0] = (uint8_t)(e >= 0 && e < EPS->nengines ? e : 0xFF);
        n[i][1] = (uint8_t)(ch >= 0 && ch < 16 ? ch : 0xFF);
    }
    if (memcmp(n, EPR.lay, sizeof n)) {
        memcpy(EPR.lay, n, sizeof n);
        EPR.lay_seq = ep_next(EPR.lay_seq);
    }
}
static void ep_values(void)            /* every value: val_seq when one changed */
{
    int l, p, moved = 0;
    for (l = 0; l < EPS->nlayers; l++) {
        const uint8_t e = EPR.lay[l][0];
        int16_t *row = ep_val + l * EPS->nparams;
        for (p = 0; p < EPS->nparams; p++) {
            int32_t v = RFM1_EP_NONE;
            if (e != 0xFF && ep_on[e * EPS->nparams + p]) {
                v = EPS->get(l, EPS->params[p].pid);
                if (v != RFM1_EP_NONE)
                    v = v < -32767 ? -32767 : v > 32767 ? 32767 : v;
            }
            if (row[p] != (int16_t)v)
                row[p] = (int16_t)v, moved = 1;
        }
    }
    if (moved)
        EPR.val_seq = ep_next(EPR.val_seq);
}
static void ep_cells(void)             /* the watched page's eight cells: cell_seq when one changed */
{
    __typeof__(EPR.cell) c;
    const rfm1_ep_page_t *g = 0;
    int e, k, pg = ep_wpage;
    memset(c, 0, sizeof c);
    for (e = 0; pg >= 0 && e < EPS->nengines; e++) {
        if (pg < EPS->engines[e].npages) {
            g = &EPS->engines[e].pages[pg];
            break;
        }
        pg -= EPS->engines[e].npages;
    }
    for (k = 0; g && ep_wlayer < EPS->nlayers && k < 8; k++) {
        const int x = g->pid[k] ? ep_pidx(g->pid[k]) : -1;
        char nm[11], tx[11];
        if (x < 0)
            continue;
        c[k].pid = g->pid[k];
        nm[0] = tx[0] = 0;
        if (!EPS->words || !EPS->words(ep_wlayer, g->pid[k], nm, tx))
            nm[0] = tx[0] = 0;
        nm[10] = tx[10] = 0;
        ep_name(c[k].name, nm[0] ? nm : EPS->params[x].name, 11);
        ep_name(c[k].text, tx, 11);
    }
    if (memcmp(c, EPR.cell, sizeof c) || EPR.wpage != (uint16_t)ep_wpage || EPR.wlayer != ep_wlayer) {
        memcpy(EPR.cell, c, sizeof c);
        EPR.wpage = (uint16_t)ep_wpage;
        EPR.wlayer = ep_wlayer;
        EPR.cell_seq = ep_next(EPR.cell_seq);
    }
}
static int ep_cmp_pid(const void *a, const void *b)
{
    return (int)EPS->params[*(const uint16_t *)a].pid - (int)EPS->params[*(const uint16_t *)b].pid;
}
static int ep_open(void)               /* the description and the record, once (after init) */
{
    uint32_t e, p, k, n;
    if (ep_state)
        return ep_state > 0;
    ep_state = -1;
    if (!fm1core_ep || !(EPS = fm1core_ep()) || !EPS->nlayers || EPS->nlayers > EP_LAYERS || !EPS->nengines ||
        !EPS->nparams || EPS->nparams > EP_PARAMS || !EPS->layer || !EPS->get || !EPS->set)
        return 0;
    n = EPS->nparams;
    ep_order = malloc(sizeof(uint16_t) * n);
    ep_val = malloc(sizeof(int16_t) * n * EPS->nlayers);
    ep_on = calloc(EPS->nengines, n);
    if (!ep_order || !ep_val || !ep_on)
        return 0;
    for (p = 0; p < n; p++)
        ep_order[p] = (uint16_t)p;
    qsort(ep_order, n, sizeof ep_order[0], ep_cmp_pid);
    for (p = 1; p < n; p++)            /* (a pid twice, 0 or 0xFFFF: no ep1) */
        if (EPS->params[ep_order[p]].pid == EPS->params[ep_order[p - 1]].pid)
            return 0;
    if (!EPS->params[ep_order[0]].pid || EPS->params[ep_order[n - 1]].pid == 0xFFFFu)
        return 0;
    for (e = 0; e < EPS->nengines; e++) {
        for (p = 0; p < EPS->engines[e].npages; p++)
            for (k = 0; k < 8; k++) {
                const int x = EPS->engines[e].pages[p].pid[k] ? ep_pidx(EPS->engines[e].pages[p].pid[k]) : -1;
                if (x >= 0)
                    ep_on[e * n + (uint32_t)x] = 1;
            }
        for (p = 0; p < n; p++)
            if (EPS->params[p].flags & (RFM1_EPF_GLOBAL | RFM1_EPF_RETIRED))
                ep_on[e * n + p] = 1;
    }
    for (p = 0; p < n * EPS->nlayers; p++)
        ep_val[p] = (int16_t)RFM1_EP_NONE;
    ep_epoch = EPS->epoch ? EPS->epoch() : 0u;
    if (!ep_build())
        return 0;
    EPR.nlayers = EPS->nlayers;
    memset(EPR.lay, 0xFF, sizeof EPR.lay);
    EPR.val_ptr = (uint32_t)(uintptr_t)ep_val;
    ep_layers();
    ep_values();
    ep_cells();
    ep_state = 1;
    return 1;
}
static int ep_find_hold(int l, int x)
{
    int i;
    for (i = 0; i < ep_nhold[l]; i++)
        if (ep_hold[l][i].x == x)
            return i;
    return -1;
}
static void ep_drop_hold(int l, int i)
{
    ep_hold[l][i] = ep_hold[l][--ep_nhold[l]];
}
/* the holds against the firmware now: an engine switch drops a layer's; a value the firmware changed itself drops
 * that one (the firmware's value is the base now: nothing to write back). Before the values are read after a frame */
static void ep_holds_check(void)
{
    int l, i;
    for (l = 0; l < EPS->nlayers; l++) {
        if (!ep_nhold[l])
            continue;
        if (EPR.lay[l][0] != ep_hold_eng[l]) {
            ep_ndropped += ep_nhold[l];
            ep_nhold[l] = 0;
            continue;
        }
        for (i = ep_nhold[l] - 1; i >= 0; i--)
            if (EPS->get(l, EPS->params[ep_hold[l][i].x].pid) != ep_hold[l][i].val) {
                ep_ndropped++;
                ep_drop_hold(l, i);
            }
    }
}
static void ep_value_one(int l, int x)  /* one value again (after a LOCK / FREE: no whole refresh) */
{
    int16_t *row = ep_val + l * EPS->nparams;
    int32_t v = EPS->get(l, EPS->params[x].pid);
    if (v != RFM1_EP_NONE)
        v = v < -32767 ? -32767 : v > 32767 ? 32767 : v;
    if (row[x] != (int16_t)v) {
        row[x] = (int16_t)v;
        if ((EPS->params[x].flags & RFM1_EPF_GLOBAL))
            for (int k = 0; k < EPS->nlayers; k++)
                ep_val[k * EPS->nparams + x] = (int16_t)v;
        EPR.val_seq = ep_next(EPR.val_seq);
    }
}
/* the firmware's set, quietly: how 1 a SET (the sound marked edited as its editor does), 2 a lock's write or its
 * return (not an edit of the sound); readouts it pushed dropped */
static void ep_write(int l, int x, int32_t v, int how)
{
#ifdef RFM1_RD1
    const rfm1_readout_t rd = RD;
    const uint32_t pushes = rd_pushes;
#endif
    rfm1_ep_host = how;
    EPS->set(l, EPS->params[x].pid, v);
    rfm1_ep_host = 0;
#ifdef RFM1_RD1
    RD = rd;
    rd_pushes = pushes;
#endif
}
static void ep_free_layer(int l)       /* every hold of layer l back to its base */
{
    while (ep_nhold[l]) {
        const ep_hold_t h = ep_hold[l][--ep_nhold[l]];
        ep_write(l, h.x, h.base, 2);
        ep_value_one(l, h.x);
        ep_nfrees++;
    }
}
static void ep_refresh(void)
{
    if (EPS->epoch && EPS->epoch() != ep_epoch) {   /* (a description made at run time: made again) */
        ep_epoch = EPS->epoch();
        free(ep_blob);
        ep_blob = 0;
        if (!ep_build()) {
            ep_state = -1;
            EPR.desc_ptr = EPR.desc_len = 0;
            EPR.desc_seq = ep_next(EPR.desc_seq);
            return;
        }
    }
    ep_layers();
    ep_holds_check();
    ep_values();
    ep_cells();
}
static void ep_frame(void)
{
    if (ep_state > 0)
        ep_refresh();
}
EXPORT(rfm1_ep) uint32_t rfm1_ep(void) { return ep_open() ? (uint32_t)(uintptr_t)&EPR : 0u; }
EXPORT(rfm1_ep_set) int32_t rfm1_ep_set(int32_t op, int32_t layer, int32_t pid, int32_t value)
{
    const rfm1_ep_param_t *q;
    int x;
    uint8_t e;
    if (!ep_open())
        return -1;
    if (op < 1 || op > 4)
        return -3;
    if (op == 4) {                     /* FREE_ALL: the layer's holds (layer 0xFF or -1: every layer's) */
        ep_layers();
        ep_holds_check();
        if (layer == 0xFF || layer < 0) {
            for (int l = 0; l < EPS->nlayers; l++)
                ep_free_layer(l);
        } else if (layer < EPS->nlayers)
            ep_free_layer(layer);
        else
            return -1;
        return 0;
    }
    if (layer < 0 || layer >= EPS->nlayers || pid <= 0 || pid >= 0xFFFF || (x = ep_pidx((uint16_t)pid)) < 0)
        return -1;
    q = &EPS->params[x];
    if (q->flags & RFM1_EPF_GLOBAL)
        layer = 0;
    ep_layers();
    e = EPR.lay[layer][0];
    if (e == 0xFF || !ep_on[e * EPS->nparams + (uint32_t)x])
        return -1;
    value = value < q->min ? q->min : value > q->max ? q->max : value;
    if (ep_nhold[layer] && EPR.lay[layer][0] != ep_hold_eng[layer])
        ep_nhold[layer] = 0;           /* (an engine switch since the last frame: its holds gone) */
    {
        const int h = ep_find_hold(layer, x);
        if (op == 1) {                 /* SET: held, the base only (the step goes on as locked); else the value */
            if (h >= 0 && EPS->get(layer, (uint16_t)pid) == ep_hold[layer][h].val) {
                ep_hold[layer][h].base = (int16_t)value;
                ep_nsets++;
                return 0;
            }
            if (h >= 0)
                ep_drop_hold(layer, h);
            ep_write(layer, x, value, 1);
            ep_nsets++;
            ep_refresh();
            return 0;
        }
        if (op == 2) {                 /* LOCK: the base kept at the first, the value written */
            int32_t g;
            if (q->flags & RFM1_EPF_NOLOCK)
                return -2;
            if (h < 0) {
                if (ep_nhold[layer] >= EP_HOLDS)
                    return -4;
                g = EPS->get(layer, (uint16_t)pid);
                if (g == RFM1_EP_NONE)
                    return -1;
                if (!ep_nhold[layer])
                    ep_hold_eng[layer] = e;
                ep_hold[layer][ep_nhold[layer]].x = (uint16_t)x;
                ep_hold[layer][ep_nhold[layer]].base = (int16_t)g;
                ep_nhold[layer]++;
            }
            ep_write(layer, x, value, 2);
            ep_hold[layer][h >= 0 ? h : ep_nhold[layer] - 1].val = (int16_t)EPS->get(layer, (uint16_t)pid);
            ep_value_one(layer, x);
            ep_nlocks++;
            return 0;
        }
        if (h >= 0) {                  /* FREE: the base written back (not held: nothing) */
            const int16_t b = ep_hold[layer][h].base;
            ep_drop_hold(layer, h);
            ep_write(layer, x, b, 2);
            ep_value_one(layer, x);
            ep_nfrees++;
        }
        return 0;
    }
}
EXPORT(rfm1_ep_watch) void rfm1_ep_watch(int32_t layer, int32_t page)
{
    uint32_t e, np = 0;
    if (!ep_open())
        return;
    for (e = 0; e < EPS->nengines; e++)
        np += EPS->engines[e].npages;
    ep_wpage = (int16_t)(page >= 0 && (uint32_t)page < np && layer >= 0 && layer < EPS->nlayers ? page : -1);
    ep_wlayer = (uint8_t)(ep_wpage >= 0 ? layer : 0);
    ep_cells();
}
#endif

/* the Move takeover (docs/RFM1-TAKEOVER.md, capability tk1): only in a module built with tk1 in its fm1_add_rfm1 WANTS
 * (RFM1_TK; its units get FM1_HOST_TK, so core-api/rfm1_move.h's calls reach the rfm1_tk__* below). One record at a
 * stable address, rfm1_tk() -> ptr, which the host reads after each rfm1_frame and rfm1_tk_input; rfm1_tk_input and
 * rfm1_tk_state call the ops the firmware registered. No import: a host without tk1 never calls them. The seq
 * counters move only on a real change (a screen or an LED that differs; each say), never 0 once moved. */
#ifdef RFM1_TK
#include <stddef.h>
#include <string.h>
#include "rfm1_move.h"
typedef struct {
    uint16_t version, size;            /* 1, 1468 */
    uint32_t claims;                   /* RFM1_TK_* bits; 0: no takeover now */
    uint32_t scr_seq, led_seq, say_seq;
    uint8_t say_flags;                 /* RFM1_TK_SAY_INTERRUPT */
    uint8_t reserved[3];
    char name[RFM1_TK_NAME_LEN];       /* printable ASCII, NUL-terminated */
    char say[RFM1_TK_SAY_LEN];
    uint8_t screen[RFM1_TK_SCREEN_BYTES];
    uint8_t led[RFM1_TK_L_N][3];
} rfm1_tk_rec_t;
/* section 4.2's offsets, exactly */
_Static_assert(offsetof(rfm1_tk_rec_t, version) == 0 && offsetof(rfm1_tk_rec_t, size) == 2, "tk1 record");
_Static_assert(offsetof(rfm1_tk_rec_t, claims) == 4 && offsetof(rfm1_tk_rec_t, scr_seq) == 8, "tk1 record");
_Static_assert(offsetof(rfm1_tk_rec_t, led_seq) == 12 && offsetof(rfm1_tk_rec_t, say_seq) == 16, "tk1 record");
_Static_assert(offsetof(rfm1_tk_rec_t, say_flags) == 20 && offsetof(rfm1_tk_rec_t, reserved) == 21, "tk1 record");
_Static_assert(offsetof(rfm1_tk_rec_t, name) == 24 && offsetof(rfm1_tk_rec_t, say) == 40, "tk1 record");
_Static_assert(offsetof(rfm1_tk_rec_t, screen) == 168 && offsetof(rfm1_tk_rec_t, led) == 1192, "tk1 record");
_Static_assert(sizeof(rfm1_tk_rec_t) == 1468, "tk1 record: 1468 bytes");
static rfm1_tk_rec_t TK = {1, sizeof(rfm1_tk_rec_t), 0, 0, 0, 0, 0, {0, 0, 0}, "", "", {0}, {{0}}};
static uint8_t tk_draw[RFM1_TK_SCREEN_BYTES];   /* the firmware draws here; screen_done copies a changed picture */
static const rfm1_tk_ops_t *tk_ops;
static int tk_on;

static void tk_txt(char *d, const char *s, uint32_t n)   /* printable ASCII only, cut at n - 1 */
{
    uint32_t i = 0;
    while (s && *s && i + 1 < n) {
        const unsigned char c = (unsigned char)*s++;
        d[i++] = c >= 32 && c < 127 ? (char)c : ' ';
    }
    while (i < n)
        d[i++] = 0;
}
static uint32_t tk_next(uint32_t s) { return s + 1u ? s + 1u : 1u; }

void rfm1_tk__register(const char *name, unsigned claims, const rfm1_tk_ops_t *ops)
{
    tk_txt(TK.name, name, sizeof TK.name);
    TK.claims = claims & RFM1_TK_ALL;
    tk_ops = ops;
}
uint8_t *rfm1_tk__screen(void) { return tk_draw; }
void rfm1_tk__screen_done(void)
{
    if (memcmp(TK.screen, tk_draw, sizeof tk_draw)) {
        memcpy(TK.screen, tk_draw, sizeof tk_draw);
        TK.scr_seq = tk_next(TK.scr_seq);
    }
}
void rfm1_tk__led(int i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i < 0 || i >= RFM1_TK_L_N || (TK.led[i][0] == r && TK.led[i][1] == g && TK.led[i][2] == b))
        return;
    TK.led[i][0] = r, TK.led[i][1] = g, TK.led[i][2] = b;
    TK.led_seq = tk_next(TK.led_seq);
}
void rfm1_tk__claims(unsigned claims) { TK.claims = claims & RFM1_TK_ALL; }
void rfm1_tk__say(const char *text, int interrupt)
{
    tk_txt(TK.say, text, sizeof TK.say);
    TK.say_flags = interrupt ? RFM1_TK_SAY_INTERRUPT : 0u;
    TK.say_seq = tk_next(TK.say_seq);
}
int rfm1_tk__shown(void) { return tk_on; }

EXPORT(rfm1_tk) uint32_t rfm1_tk(void) { return (uint32_t)(uintptr_t)&TK; }
EXPORT(rfm1_tk_input) int32_t rfm1_tk_input(uint32_t pkt)
{
    const uint8_t p[4] = {(uint8_t)pkt, (uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
    if (tk_ops && tk_ops->input)
        tk_ops->input(p);
    return 0;
}
EXPORT(rfm1_tk_state) void rfm1_tk_state(uint32_t on, uint32_t claims, uint32_t flags)
{
    tk_on = on != 0;
    if (tk_ops && tk_ops->state)
        tk_ops->state(tk_on, claims, flags);
}
#endif
