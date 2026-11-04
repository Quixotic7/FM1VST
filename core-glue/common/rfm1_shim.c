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
EXPORT(rfm1_tick) void rfm1_tick(uint32_t ms)
{
    if (core() && C->tick)
        C->tick(ms);
}
EXPORT(rfm1_frame) void rfm1_frame(void)
{
    if (core() && C->frame)
        C->frame();
}
EXPORT(rfm1_idle) void rfm1_idle(void)
{
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
#if RFM1_META_LABELS
/* with the author's static labels (RemoteFM1Metadata.json: RFM1_META_SLOT, knobs and buttons): served while the core
 * has no live labels of its own (no symbols, or its epoch 0); once its epoch is not 0 everything is the core's. The
 * epoch here is the shim's own, moved at each switch between the two and at each move of the core's, so a host never
 * misses a change */
static uint32_t meta_epoch, meta_core_epoch;
static int meta_src;
EXPORT(rfm1_labels_epoch) uint32_t rfm1_labels_epoch(void)
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
EXPORT(rfm1_label) uint32_t rfm1_label(int32_t which)
{
    if (which < 0 || which >= FM1CORE_LBL_N)
        return 0u;
    if (fm1core_labels_epoch && fm1core_labels_epoch())
        return fm1core_label ? (uint32_t)(uintptr_t)fm1core_label(which) : 0u;
    return (uint32_t)(uintptr_t)RFM1_META_SLOT[which];
}
#else
EXPORT(rfm1_labels_epoch) uint32_t rfm1_labels_epoch(void)
{
    return fm1core_labels_epoch ? fm1core_labels_epoch() : 0u;
}
EXPORT(rfm1_label) uint32_t rfm1_label(int32_t which)
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
