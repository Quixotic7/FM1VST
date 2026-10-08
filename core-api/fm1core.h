/* SPDX-License-Identifier: GPL-3.0-only */
/* The core ABI (FM1-VST-PLAN.md 4.4): a firmware built as a loadable module ("core", a *.fm1core file) exports
 * exactly one symbol,
 *
 *     const fm1core_t *fm1core_get(uint32_t abi_version);
 *
 * which returns the core's descriptor, or NULL when the module was built for another FM1CORE_ABI. Nothing in the
 * host links a firmware symbol directly: the panel state (emu_hal_t, from the ChoralRoot emulator's emu_hooks.h)
 * and every entry point are reached through the descriptor. emu_hal_t's layout and the hook signatures are part of
 * the ABI: a change to emu_hooks.h bumps FM1CORE_ABI.
 *
 * THREADING CONTRACT
 *   - Every entry point of one core (and every read or write of its hal) is made from ONE thread at a time, in
 *     order; the core never starts a thread or a timer of its own. In the plugin that is the audio thread (the
 *     engine's Device: engine/device.h). Calls from different threads are allowed only when the caller serialises
 *     them (e.g. init on the message thread before the audio thread starts).
 *   - The device clock is the caller's: tick(ms) once per device millisecond with ms counting up from 0, then
 *     frame() every 15 ms (and at ms 0) or idle() otherwise, then audio() for each 128-frame block that has fallen
 *     due at 44100 Hz (web/emu_web.c run_ms, emu.c's headless loop). The caller sets hal->ready = 2 (simulated
 *     time) before init.
 *   - A core's globals exist once per loaded image: two instruments need two loaded copies (engine/cores.h).
 *
 * LIFETIME
 *   options, boot_options (optional), hal->ready = 2, init; then the clock; then shutdown() (flushes and saves the
 *   flash as the emulator's exit handler would, closes the flash file; the process keeps running), then the module
 *   is unloaded. A firmware that "reboots" or "exits" does not end the host: the core records it, halted() turns
 *   non-zero, the clock entry points become no-ops (audio: silence), and the host reloads the module to power on
 *   again. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "../cores/ChoralRootFM1/tools/emu/emu_hooks.h"

#define FM1CORE_ABI 2u
/* the flash_path to pass to options() to boot from the bytes given to flash_stage() (ABI 2) */
#define FM1CORE_FLASH_STAGED "fm1core:staged"
#define FM1CORE_SYMBOL "fm1core_get"
#define FM1CORE_SUFFIX ".fm1core"

/* one firmware parameter exposed to the host (plan 4.5): an absolute value with a range, read and written on the
 * clock's thread through the firmware's own path */
typedef struct {
    const char *name;               /* "Strum Rate", "Arp Division", "Chord Level" */
    int32_t min, max, def;          /* the firmware's own range and default */
    const char *const *names;       /* enum value names ("1/8", "UP", ..), max - min + 1 of them, or NULL */
    const char *unit;               /* "ms", "%", "dB", "" */
    int32_t (*get)(void);           /* the firmware's current value */
    void (*set)(int32_t v);         /* write it as a panel change would */
} fm1param_t;

typedef struct {
    uint32_t abi_version;           /* FM1CORE_ABI */
    const char *id;                 /* "choralroot": a stable identifier (folders, plugin state) */
    const char *name;               /* "ChoralRoot": for menus */
    const char *version;            /* the firmware's version */
    const char *source_url;         /* the firmware repo and the commit the core was built from */
    uint32_t flash_size;            /* bytes in the flash image (1 MiB) */
    const char *button_names[EMU_NB];   /* the firmware's own labels for the 14 buttons, EMU_B_* order */
    const fm1param_t *params;       /* the Tier 2 parameter map, nparams entries (may be NULL / 0) */
    uint32_t nparams;
    emu_hal_t *hal;                 /* the module's own panel state: keys, buttons, encoders, LEDs, the LCD */
    const int8_t *keymap;           /* [6][EMU_NCOL]: key id (0..13 buttons, 14..40 note keys) at (row, column),
                                     * -1 none (emu_keymap; LED rows 1..4) */

    /* the emu_fw_* hooks of emu_hooks.h (same meaning, same arguments) */
    void (*options)(const char *flash_path, int no_flash, int save_on_exit, int headless);
    int (*boot_options)(int fail, const char *reason, int stage);
    void (*init)(int demo);
    void (*tick)(uint32_t ms);
    void (*frame)(void);
    void (*idle)(void);
    void (*audio)(int16_t *stereo, uint32_t frames);   /* frames: a multiple of EMU_BLOCK; s16 interleaved */
    int (*midi_in)(uint32_t pkt);                      /* 0: no room, retry later */
    int (*midi_out_take)(uint32_t *pkt);               /* 0: none */
    void (*dump)(void);
    void (*stats)(uint32_t *shed, uint32_t *cpu_pct);
    void (*ui_info)(char *buf, uint32_t n);

    void (*shutdown)(void);         /* save / flush the flash as the emulator's exit would, without exiting;
                                     * idempotent */
    int (*halted)(void);            /* 0 while running; after the firmware called exit(code) (a reboot, UBOOT):
                                     * 0x100 | (code & 0xFF) */

    /* ---- the flash image (ABI 2). A core keeps its whole flash (flash_size bytes) as a RAM image; the host owns
     * persistence: it reads the image and stores it (plugin state, flash.bin, presets), and boots from bytes.
     * BOOTING FROM BYTES: flash_stage(bytes, n) before options(), then options(FM1CORE_FLASH_STAGED, 0, 0, 1) and
     * init: the power-on reads the staged bytes (short: padded with 0xFF; n = 0 or bytes = NULL: a fresh, erased
     * flash) and nothing is ever written to a file. (A real file path in options() still works as before: the
     * image is read from it and every erase / program written through.) */
    int (*flash_stage)(const uint8_t *bytes, uint32_t n);   /* copied; call before init; 1: staged */
    uint8_t *(*flash)(void);        /* the live image, flash_size bytes (valid for the module's lifetime; read it on
                                     * the clock's thread or with the clock stopped) */
    uint32_t (*flash_dirty)(void);  /* a counter that moves on every erase / program (persist when it changed) */
    int (*flash_sync)(void);        /* write what the firmware would save lazily (ChoralRoot: the settings record
                                     * after its quiet time) now, so the image is current before it is read;
                                     * 1: current, 0: a save is still deferred by the firmware (a loop playing).
                                     * May erase a sector (the emulated erase stall: ~45 ms of silence). */
} fm1core_t;

typedef const fm1core_t *(*fm1core_get_fn)(uint32_t abi_version);

#if defined(__GNUC__) || defined(__clang__)
#define FM1CORE_EXPORT __attribute__((visibility("default")))
#else
#define FM1CORE_EXPORT
#endif
FM1CORE_EXPORT const fm1core_t *fm1core_get(uint32_t abi_version);

#ifdef __cplusplus
}
#endif
