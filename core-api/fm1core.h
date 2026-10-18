/* SPDX-License-Identifier: GPL-3.0-only */
/* The core ABI (FM1-VST-PLAN.md 4.4, CORES-PLAN.md 3): a firmware built as a loadable module ("core", a *.fm1core
 * file) exports exactly one symbol,
 *
 *     const fm1core_t *fm1core_get(uint32_t abi_version);
 *
 * which returns the core's descriptor, or NULL when the module was built for another FM1CORE_ABI. Nothing in the
 * host links a firmware symbol directly: the panel state (fm1panel_t, below) and every entry point are reached
 * through the descriptor.
 *
 * ABI 5 (2026-10-18): the ABI owns its panel. This header includes no firmware header: fm1panel_t is FM1VST's own,
 * and each core's glue translates between it and its firmware's own HAL (emu_hal_t, plat.h, host.c's statics, ..;
 * core-glue/common/panel.h has the helpers). The LEDs are brightness levels (FM1P_LED_*), one per (column, line), so
 * every family's planes (lit / mid / dim / faint / glow / breath / backlight) map onto one array. fm1panel_t's layout
 * and the descriptor's are the ABI: a change to either bumps FM1CORE_ABI.
 *
 * ABI 6 (2026-10-18): the descriptor gains `credits` at its end (the firmware's credits and third-party notices, for
 * the plugin's firmware info). The descriptor carries no size, so only the version tells a host whether the field
 * is there: hence the bump. Nothing else moved, so an ABI 5 descriptor is ABI 6's prefix and the host still loads ABI
 * 5 modules (FM1CORE_ABI_MIN), without credits: read the field through fm1core_credits(), never directly.
 *
 * THREADING CONTRACT
 *   - Every entry point of one core (and every read or write of its panel) is made from ONE thread at a time, in
 *     order; the core never starts a thread or a timer of its own. In the plugin that is the audio thread (the
 *     engine's Device: engine/device.h). Calls from different threads are allowed only when the caller serialises
 *     them (e.g. init on the message thread before the audio thread starts).
 *   - The device clock is the caller's: tick(ms) once per device millisecond with ms counting up from 0, then
 *     frame() every 15 ms (and at ms 0) or idle() otherwise, then audio() for each 128-frame block that has fallen
 *     due at 44100 Hz (web/emu_web.c run_ms, emu.c's headless loop). The caller sets panel->ready = 2 (simulated
 *     time) before init.
 *   - The host writes the panel's input fields between entry points; the core reads them at its next entry point
 *     (tick, frame or idle: the input as of that call) and has written its output fields (LEDs, LCD) when init,
 *     frame and idle return.
 *   - A core's globals exist once per loaded image: two instruments need two loaded copies (engine/cores.h).
 *
 * LIFETIME
 *   options, boot_options (optional), panel->ready = 2, init; then the clock; then shutdown() (flushes and saves the
 *   flash as the emulator's exit handler would, closes the flash file; the process keeps running), then the module
 *   is unloaded. A firmware that "reboots" or "exits" does not end the host: the core records it, halted() turns
 *   non-zero, the clock entry points become no-ops (audio: silence), and the host reloads the module to power on
 *   again. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---- the panel's dimensions (the values of ChoralRoot's tools/emu/emu_hooks.h, which ABI 1..4 included) ---- */
#define FM1P_LCD_W 240
#define FM1P_LCD_H 240
#define FM1P_FS 44100        /* the device's sample rate */
#define FM1P_BLOCK 128       /* frames per audio() block (one I2S half buffer on the device) */
#define FM1P_NKEY 27         /* note keys: 0 = F3 (MIDI 53) .. 26 = G5 */
#define FM1P_NBTN 14         /* matrix buttons 0..13 */
#define FM1P_NENC 7          /* matrix encoders 0..6 */
#define FM1P_NCOL 11         /* LED / matrix columns (hal/fm1_input.h FM1_NCOL) */
#define FM1P_NLINE 4         /* LED lines per column: line 0..3 = the column's row bit 1..4 (keymap rows 1..4) */
#define FM1P_NOTE0 53        /* MIDI note of key 0 (F3) */

/* button labels, in the panel's order (the designer's BTN_IDS; SEL is Felucca's SCL): what btn_id[] is indexed by */
enum { FM1P_B_FX, FM1P_B_SEL, FM1P_B_ENV, FM1P_B_LFO, FM1P_B_EDIT, FM1P_B_GLO, FM1P_B_HOME, FM1P_B_SAVE,
       FM1P_B_ARP, FM1P_B_SEQ, FM1P_B_PLAY, FM1P_B_REC, FM1P_B_OCTDN, FM1P_B_OCTUP, FM1P_NB };
/* knob roles (Felucca's EN_* order), then the MASTER pot: what enc_id[] / enc_dir[] / knob_target() take */
enum { FM1P_E_SELECT, FM1P_E_ALGO, FM1P_E_PRESETS, FM1P_E_K1, FM1P_E_K2, FM1P_E_K3, FM1P_E_K4, FM1P_E_MASTER,
       FM1P_NE };

/* The host's names for the same values, as emu_hooks.h spells them (EMU_*). A glue unit that includes a firmware's
 * own emu_hooks.h (which defines these names too) defines FM1CORE_NO_EMU_NAMES before including this header, or
 * includes the firmware first: the names are skipped when EMU_LCD_W is already defined (the values are identical;
 * the static asserts below check it). */
#if !defined(FM1CORE_NO_EMU_NAMES) && !defined(EMU_LCD_W)
#define EMU_LCD_W FM1P_LCD_W
#define EMU_LCD_H FM1P_LCD_H
#define EMU_FS FM1P_FS
#define EMU_BLOCK FM1P_BLOCK
#define EMU_NKEY FM1P_NKEY
#define EMU_NBTN FM1P_NBTN
#define EMU_NENC FM1P_NENC
#define EMU_NCOL FM1P_NCOL
enum { EMU_B_FX, EMU_B_SEL, EMU_B_ENV, EMU_B_LFO, EMU_B_EDIT, EMU_B_GLO, EMU_B_HOME, EMU_B_SAVE,
       EMU_B_ARP, EMU_B_SEQ, EMU_B_PLAY, EMU_B_REC, EMU_B_OCTDN, EMU_B_OCTUP, EMU_NB };
enum { EMU_E_SELECT, EMU_E_ALGO, EMU_E_PRESETS, EMU_E_K1, EMU_E_K2, EMU_E_K3, EMU_E_K4, EMU_E_MASTER, EMU_NE };
#endif
#if defined(EMU_LCD_W) && !defined(__cplusplus)
_Static_assert(EMU_LCD_W == FM1P_LCD_W && EMU_LCD_H == FM1P_LCD_H && EMU_BLOCK == FM1P_BLOCK && EMU_FS == FM1P_FS,
               "a firmware's emu_hooks.h disagrees with fm1core.h");
#endif

/* LED brightness levels: what a glue writes for each of its firmware's planes (lit = LIT; ChoralRoot's mid / dim /
 * faint = MID / DIM / FAINT; Felucca's glow = DIM, its backlight = FAINT; ..). Any value 0..255 is allowed (a
 * firmware with PWM levels writes its own); the host draws the brightness and classes it with fm1p_led_class(). */
enum { FM1P_LED_OFF = 0, FM1P_LED_FAINT = 12, FM1P_LED_DIM = 40, FM1P_LED_MID = 96, FM1P_LED_LIT = 255 };
/* the host's five classes of a level (RemoteFM1's LEDS planes, the panel's text colours) */
enum { FM1P_LC_OFF, FM1P_LC_FAINT, FM1P_LC_DIM, FM1P_LC_MID, FM1P_LC_LIT };
#define FM1P_LED_LIT_MIN 176 /* "lit" from here up: halfway between MID and LIT */
static inline int fm1p_led_class(uint8_t v)
{
    return v >= FM1P_LED_LIT_MIN ? FM1P_LC_LIT : v >= FM1P_LED_MID ? FM1P_LC_MID : v >= FM1P_LED_DIM ? FM1P_LC_DIM
         : v ? FM1P_LC_FAINT : FM1P_LC_OFF;
}

/* The panel: one per loaded core (the descriptor's panel), owned by the core's module. */
typedef struct {
    /* host -> core (written by the host between entry points; read by the core at its next tick / frame / idle) */
    volatile uint32_t keys, buttons;            /* bit k: note key k held; bit i: matrix button i held */
    volatile uint32_t keys_tap, buttons_tap;    /* went down and up since the last tick (shorter than 1 ms): the
                                                 * core takes (clears) them */
    volatile int32_t enc[FM1P_NENC];            /* detents not yet taken, matrix encoder order, + = clockwise: the
                                                 * core takes (zeroes) them */
    volatile int32_t master;                    /* the MASTER pot, 0..1023 (0 before init: the core's default) */
    volatile uint8_t key_vel[FM1P_NKEY];        /* a held key's velocity 1..127; 0 = none given (the core's default)
                                                 * (set before the key's bit) */
    volatile uint8_t key_lean[FM1P_NKEY];       /* aftertouch 0..100 % */
    /* core -> host */
    uint8_t led[FM1P_NCOL][FM1P_NLINE];         /* brightness 0..255 of the LED on (column, line), the panel's own
                                                 * wiring (hal/fm1_input.h: line 0..3 = row bit 1..4) */
    uint8_t led_play_green;                     /* PLAY's green LED, 0..255 */
    uint16_t lcd[FM1P_LCD_W * FM1P_LCD_H];      /* the panel's RAM, RGB565 big-endian as sent */
    volatile uint32_t lcd_writes;               /* bumped by every change to lcd */
    /* the panel as this firmware labels it (filled by the core at init) */
    uint8_t btn_id[FM1P_NB];                    /* label (FM1P_B_*) -> matrix button id */
    uint8_t enc_id[FM1P_NE - 1];                /* role (FM1P_E_*) -> matrix encoder (MASTER is the pot) */
    int8_t enc_dir[FM1P_NE - 1];                /* +1 / -1: a clockwise turn of the role is enc[enc_id] += dir */
    uint8_t ready;                              /* 2: simulated time (set by the host before init); the core may
                                                 * change it during init (1: up) */
} fm1panel_t;

#define FM1CORE_ABI 6u
#define FM1CORE_ABI_MIN 5u   /* the oldest the host loads: ABI 5 is ABI 6 without credits */
/* the flash_path to pass to options() to boot from the bytes given to flash_stage() (ABI 2) */
#define FM1CORE_FLASH_STAGED "fm1core:staged"
#define FM1CORE_SYMBOL "fm1core_get"
#define FM1CORE_SUFFIX ".fm1core"

/* one firmware parameter exposed to the host (plan 4.5, the Tier 2 map): an absolute value with a range, read and
 * written through the firmware's own path.
 * THREADS: get, set and target run on the clock's thread between device milliseconds (in the plugin: the audio
 * thread; the device clock is stopped while they run): they are cheap and never block. text only formats its
 * argument from constant tables and may be called from any thread. */
enum {
    FM1P_ENUM = 1u,                 /* names[] holds max - min + 1 value names (also text()'s) */
    FM1P_META = 2u,                 /* stands for another entry (or for itself, rewritten), which target() names; it
                                     * changes when the core's param_epoch() moves (Felucca: "E1".."E8", the selected
                                     * part's engine parameters, whose name / range / names are rewritten from the
                                     * engine's descriptors). get / set act on the current target. */
    FM1P_PERSIST = 4u,              /* the firmware's settings record holds it: a change reaches the flash image */
    FM1P_EDITOR = 8u,               /* a sound editor parameter (the part's sound; persisted only by SAVE on the
                                     * device) */
    FM1P_SOUND = 16u,               /* the value comes with the part's sound (a sound load sets it): def is the
                                     * parameter's own, not what a fresh unit's default sound brings */
    FM1P_HIDDEN = 32u,              /* (ABI 4) a knob target only: the host gives it no slot of its own (an editor
                                     * page's parameter, an Options entry, a cell rewritten per screen); knob_target()
                                     * may name it. Every other field is valid as for any entry. */
};
typedef struct {
    const char *name;               /* "Strum Rate", "Arp Division", "Chord Level" (<= 16 characters) */
    int32_t min, max, def;          /* the firmware's own range (integer steps) and default */
    const char *const *names;       /* enum value names ("1/8", "UP", ..), max - min + 1 of them, or NULL */
    const char *unit;               /* "ms", "%", "dB", "": informational (text() already carries it) */
    int32_t (*get)(void);           /* the firmware's current value */
    void (*set)(int32_t v);         /* write it as a panel change would (clamped to min..max) */
    /* ABI 3 */
    void (*text)(int32_t v, char *buf, uint32_t n);   /* the firmware's own value text for v ("1/8", "120 ms", "Up"),
                                                       * NUL-terminated in n bytes */
    int32_t (*target)(void);        /* FM1P_META: the index in params of the entry it stands for now, -1 none; NULL
                                     * otherwise */
    uint32_t flags;                 /* FM1P_* */
    const char *path;               /* documentation: how set() reaches the firmware ("perf: cu_layer_knob ..") */
} fm1param_t;

typedef struct {
    uint32_t abi_version;           /* FM1CORE_ABI */
    const char *id;                 /* "choralroot": a stable identifier (folders, plugin state); the module's file
                                     * name is <id>.fm1core */
    const char *name;               /* "ChoralRoot": for menus */
    const char *version;            /* the firmware's version */
    const char *source_url;         /* the firmware repo and the commit the core was built from */
    /* ABI 5 */
    const char *family;             /* the glue family: "choralroot", "felucca", "melodee", "felucca_web", "plat",
                                     * "sloop", "dinghy", .. (core-glue/<family>/) */
    const char *license;            /* the firmware's SPDX expression: "GPL-3.0-only", "MPL-2.0", .. */
    const char *gated;              /* NULL, or one line: why this core stays out of public builds (CORES-PLAN.md 6;
                                     * FM1_PUBLIC_BUILD leaves it out) */
    uint32_t flash_size;            /* bytes in the flash image (1 MiB) */
    const char *button_names[FM1P_NB]; /* the firmware's own labels for the 14 buttons, FM1P_B_* order */
    const fm1param_t *params;       /* the Tier 2 parameter map, nparams entries (may be NULL / 0) */
    uint32_t nparams;
    fm1panel_t *panel;              /* the module's own panel: keys, buttons, encoders, velocity, LEDs, the LCD */
    const int8_t *keymap;           /* [6][FM1P_NCOL]: key id (0..13 matrix buttons, 14..40 note keys 0..26) at
                                     * (row, column), -1 none (hal/fm1_input.h FM1_KEYMAP; LED lines are rows 1..4) */

    /* the entry points (the emu_fw_* hooks of ChoralRoot's emu_hooks.h: same meaning, same arguments) */
    /* the flash file before init: flash_path NULL = the default (headless: none, a fresh flash each power-on),
     * FM1CORE_FLASH_STAGED = the staged bytes; no_flash = RAM only; save_on_exit = everything saved at shutdown */
    void (*options)(const char *flash_path, int no_flash, int save_on_exit, int headless);
    /* the boot guard (optional, NULL: none): fail < 0 a clean record (else the failed boots counted before this
     * reset), reason NULL a power-on, stage < 0 the default; 0 = a bad reason */
    int (*boot_options)(int fail, const char *reason, int stage);
    void (*init)(int demo);         /* the power-on; fills panel->btn_id / enc_id / enc_dir, draws the first screen */
    void (*tick)(uint32_t ms);
    void (*frame)(void);
    void (*idle)(void);
    void (*audio)(int16_t *stereo, uint32_t frames);   /* frames: a multiple of FM1P_BLOCK; s16 interleaved */
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

    /* ---- the parameter map's epoch (ABI 3): a counter that moves when an FM1P_META entry's target or descriptor
     * changed, or (ABI 4) when what any physical knob controls changed (knob_target: a screen, layer, page, mode,
     * part or popup came or went). The host re-reads target() / knob_target() and relabels. Clock thread. NULL: the
     * map has no meta entries and no knob targets. */
    uint32_t (*param_epoch)(void);

    /* ---- the physical knobs (ABI 4): what each one does on the screen showing now. role: FM1P_E_SELECT ..
     * FM1P_E_K4, FM1P_E_MASTER. Returns the index in params of the entry that knob turns now (visible or
     * FM1P_HIDDEN), or -1: no value there (a navigation knob: a menu cursor, a sound list, an action), the host
     * turns it as a relative control (panel->enc detents). MASTER is the panel's absolute pot (panel->master): -1. Valid
     * as of the last param_epoch() call (call that first). Clock thread. NULL: every knob relative. */
    int32_t (*knob_target)(int role);

    /* ---- ABI 6 ---- */
    /* NULL, or the firmware's credits and the notices its licences ask a binary to carry (a credit line its author
     * requests, a BSD / MIT notice for code or data it compiles in), as plain text: lines separated by '\n', a blank
     * line between notices. The host shows it in its firmware info. Read it with fm1core_credits(). */
    const char *credits;
} fm1core_t;

/* the descriptor's credits, NULL for none or for an ABI 5 core (whose descriptor ends before the field) */
static inline const char *fm1core_credits(const fm1core_t *c)
{
#ifdef __cplusplus
    return c && c->abi_version >= 6u ? c->credits : nullptr;
#else
    return c && c->abi_version >= 6u ? c->credits : (const char *)0;
#endif
}

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
