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

EXPORT(rfm1_get) uint32_t rfm1_get(uint32_t abi)
{
    const fm1core_t *c = fm1core_get(abi);
    if (c)
        C = c;
    return (uint32_t)(uintptr_t)c;
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
