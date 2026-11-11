/* SPDX-License-Identifier: GPL-3.0-only */
/* DX747 (https://github.com/esbenvb/fm1-dx747, the pinned submodule cores/fm1-dx747): the core unit. DX747 is
 * M-VAVE's open APP layout (app/ over sdk/): the browser build compiles app/ with web/engine's open engine and
 * web/engine/Simulator.c, the browser's platform layer (in-memory LCD, LEDs, flash, MIDI; the controls as a
 * struct). This unit is Simulator.c, unmodified, and the glue between its harness calls (sim_boot, sim_control,
 * sim_render) and the ABI's panel (core-api/fm1core.h):
 *   init      the NOR (common/nor.c: a fresh one gets the factory sample bank, as sim_boot(1) writes it), then
 *             sim_boot with the device's boot animation; MIDI out on the USB port only (Bluetooth: none)
 *   tick      the panel into fm1_controls (sim_control: each held change, each tap a press then a release, the
 *             detents, MASTER as the pot), the control task run after each change as Simulator.c does
 *   audio     sim_render, one 128-frame block (its one block of latency, its 10 ms control poll on the frame clock):
 *             the engine's 24-bit stereo as int16
 *   MIDI in   USB-MIDI packets: a channel or system message to fm1_app_midi_received (the router), a SysEx frame
 *             (up to 1024 bytes, as the transports assemble them) to fm1_app_sysex
 *   MIDI out  Simulator.c's capture of fm1_midi_send (the USB port), as USB-MIDI packets (DX747 sends SysEx replies)
 *   LEDs, LCD the 41 lamp levels 0..8 by key id (FM1P_KEYMAP), the native RGB565 pixels when the frame revision moved
 *   flash     fm1_flash_* on common/nor.c's image (Simulator.c's own array is left unused): the firmware's
 *             GROOVE_FLASH_BYTES at 0, the rest of the 1 MiB erased; no erase stall (none in DX747's simulator)
 * Not here (hardware, or nothing to drive it on a host): Bluetooth (status reads as configured, scans and links
 * fail as in the browser), the DIN/TRS port, USB and Bluetooth audio in (silence), the battery (900), the safe-boot
 * chord. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "../common/version.h"            /* (by its path: app/version.h is DX747's) */
#include "exit.h"
#include "env.h"
#include "nor.h"

/* Simulator.c's flash is replaced by the NOR's (below); its wasm export names are not exported from an .rfm1 */
#define fm1_flash_size fm1core_dx_sim_flash_size
#define fm1_flash_read fm1core_dx_sim_flash_read
#define fm1_flash_write fm1core_dx_sim_flash_write
#define fm1_flash_erase fm1core_dx_sim_flash_erase
#define fm1_flash_map fm1core_dx_sim_flash_map
#define fm1_flash_invalidate fm1core_dx_sim_flash_invalidate
#define export_name(n) unused
#include "Simulator.c"
#undef export_name
#undef fm1_flash_size
#undef fm1_flash_read
#undef fm1_flash_write
#undef fm1_flash_erase
#undef fm1_flash_map
#undef fm1_flash_invalidate

#include "exit_end.h"
#include "../common/panel.h"

static const int8_t FM1CORE_DX_KEYMAP[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;   /* app/board/fm1_input.h's fm1_key_pin */

/* ---- the flash: the firmware's region of the NOR image ---- */
uint32_t fm1_flash_size(void) { return GROOVE_FLASH_BYTES; }
int fm1_flash_read(uint32_t at, void *data, size_t bytes)
{
    if (!data || at > GROOVE_FLASH_BYTES || bytes > GROOVE_FLASH_BYTES - at)
        return FM1_FLASH_INVALID;
    memcpy(data, fm1nor_image + at, bytes);
    return 0;
}
int fm1_flash_write(uint32_t at, const void *data, size_t bytes)
{
    const uint8_t *s = data;
    size_t i;
    if (!data || at > GROOVE_FLASH_BYTES || bytes > GROOVE_FLASH_BYTES - at)
        return FM1_FLASH_INVALID;
    for (i = 0; i < bytes; ++i)
        if ((fm1nor_image[at + i] & s[i]) != s[i])
            return FM1_FLASH_NEEDS_ERASE;
    if (bytes) {
        memcpy(fm1nor_image + at, data, bytes);
        fm1nor_written(at, (uint32_t)bytes);
    }
    return 0;
}
int fm1_flash_erase(uint32_t at)
{
    if (at % FM1_FLASH_SECTOR_BYTES || at > GROOVE_FLASH_BYTES - FM1_FLASH_SECTOR_BYTES)
        return FM1_FLASH_INVALID;
    return fm1nor_erase(at) ? FM1_FLASH_INVALID : 0;
}
const uint8_t *fm1_flash_map(uint32_t at, size_t bytes)
{
    return at <= GROOVE_FLASH_BYTES && bytes <= GROOVE_FLASH_BYTES - at ? fm1nor_image + at : NULL;
}
int fm1_flash_invalidate(uint32_t at, size_t bytes)
{
    return at <= GROOVE_FLASH_BYTES && bytes <= GROOVE_FLASH_BYTES - at ? 0 : FM1_FLASH_INVALID;
}

/* ---- the panel ---- */
fm1panel_t fm1core_dx_panel;
static fm1p_edges_t fm1core_dx_keys_e, fm1core_dx_btn_e;
static uint32_t fm1core_dx_btn_now, fm1core_dx_key_now;   /* what fm1_controls holds */
static int32_t fm1core_dx_master = 1023;
static uint32_t fm1core_dx_lcd_rev;
static uint8_t fm1core_dx_led[41], fm1core_dx_up;

static void fm1core_dx_in(void)
{
    fm1panel_t *p = &fm1core_dx_panel;
    uint32_t down, up, bt, kt, i;
    int32_t m;
    /* a tap is a press now and its release at once (Simulator.c's tap: the control task sees both) */
    bt = fm1p_take_button_taps(p);
    kt = fm1p_take_key_taps(p);
    fm1p_edges(&fm1core_dx_btn_e, p->buttons | bt, 0, &down, &up);
    for (i = 0; i < FM1P_NBTN; i++) {
        if (down & (1u << i))
            sim_control(0, i, 1);
        if (up & (1u << i))
            sim_control(0, i, 0);
    }
    fm1p_edges(&fm1core_dx_keys_e, p->keys | kt, 0, &down, &up);
    for (i = 0; i < FM1P_NKEY; i++) {
        if (down & (1u << i))
            sim_control(1, i, 1);
        if (up & (1u << i))
            sim_control(1, i, 0);
    }
    for (i = 0; i < FM1P_NBTN; i++)       /* the taps' releases */
        if ((bt & ~p->buttons) & (1u << i)) {
            sim_control(0, i, 0);
            fm1core_dx_btn_e.prev &= ~(1u << i);
        }
    for (i = 0; i < FM1P_NKEY; i++)
        if ((kt & ~p->keys) & (1u << i)) {
            sim_control(1, i, 0);
            fm1core_dx_keys_e.prev &= ~(1u << i);
        }
    fm1core_dx_btn_now = controls.buttons;
    fm1core_dx_key_now = controls.keys;
    for (i = 0; i < FM1P_NENC; i++) {
        int32_t d = fm1p_take_enc(p, i);
        while (d) {
            const int32_t s = d > 127 ? 127 : d < -127 ? -127 : d;
            sim_control(2, i, s);
            d -= s;
        }
    }
    m = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    if (m != fm1core_dx_master) {
        fm1core_dx_master = m;
        sim_control(3, 0, m);
    }
}

static void fm1core_dx_out(void)
{
    fm1panel_t *p = &fm1core_dx_panel;
    unsigned i;
    for (i = 0; i < sizeof leds; i++)
        if (leds[i] != fm1core_dx_led[i]) {
            fm1core_dx_led[i] = leds[i];
            fm1p_led_id(p, &FM1CORE_DX_KEYMAP[0][0], (int)i, (uint8_t)(leds[i] >= 8 ? 255 : leds[i] * 32));
        }
    if (frame_revision != fm1core_dx_lcd_rev) {
        fm1core_dx_lcd_rev = frame_revision;
        fm1p_lcd_out_native(p, pixels);
    }
}

int fm1core_dx_halted(void) { return fm1core_halted(); }
void fm1core_dx_shutdown(void) { fm1core_run_exit(); }

void fm1core_dx_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (every write goes through: nothing kept to save at exit) */
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}

void fm1core_dx_init(int demo)
{
    fm1panel_t *p = &fm1core_dx_panel;
    unsigned bytes, i, blank = 1;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_set_stall_ms(0);
    fm1nor_open();
    for (i = 0; i < GROOVE_FLASH_BYTES && blank; i++)
        blank = fm1nor_image[i] == 0xFFu;
    if (blank) {                          /* a fresh unit: the factory sample bank (sim_boot(1)) */
        const uint8_t *factory = sampledrum_factory(&bytes);
        if (bytes <= GROOVE_SAMPLE_BYTES)
            fm1nor_write(GROOVE_SAMPLE_OFFSET, factory, bytes);
    }
    fm1p_panel_default(p);                /* DX747's ids are the FM-1's (ui_data.inc BTN_*: OCT- OCT+ FX .. REC) */
    sim_boot(2);                          /* the device's start-up animation, the flash kept */
    sim_midi_ports(2, 0);                 /* MIDI out: USB (port 1) only */
    capture_on = 1;
    fm1core_dx_master = 1023;
    if (p->master > 0)
        sim_control(3, 0, fm1core_dx_master = p->master > 1023 ? 1023 : p->master);
    p->master = fm1core_dx_master;
    memset(&fm1core_dx_keys_e, 0, sizeof fm1core_dx_keys_e);
    memset(&fm1core_dx_btn_e, 0, sizeof fm1core_dx_btn_e);
    memset(fm1core_dx_led, 0xFF, sizeof fm1core_dx_led);
    fm1core_dx_lcd_rev = frame_revision - 1u;
    fm1core_dx_up = 1;
    p->ready = 1;
    fm1core_dx_out();
}

void fm1core_dx_tick(uint32_t ms)
{
    (void)ms;                             /* (the firmware's clock is the audio's frame count: sim_render) */
    if (fm1core_halted() || !fm1core_dx_up)
        return;
    fm1core_dx_in();
}
void fm1core_dx_frame(void)
{
    if (fm1core_halted() || !fm1core_dx_up)
        return;
    fm1core_dx_in();
    fm1core_dx_out();
}
void fm1core_dx_idle(void) { fm1core_dx_frame(); }

void fm1core_dx_audio(int16_t *out, uint32_t frames)
{
    uint32_t f, i;
    for (f = 0; f + FM1P_BLOCK <= frames; f += FM1P_BLOCK) {
        int16_t *o = out + 2u * f;
        if (fm1core_halted() || !fm1core_dx_up) {
            memset(o, 0, FM1P_BLOCK * 2u * sizeof *o);
            continue;
        }
        for (i = 0; i < 2u * FM1P_BLOCK; i++) {   /* the block sim_render hands out now (its one block of latency) */
            const int32_t v = previous[i] >> 8;
            o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        (void)sim_render();
    }
    if (f < frames)
        memset(out + 2u * f, 0, (size_t)(frames - f) * 2u * sizeof *out);
}
_Static_assert(FM1_BLOCK_FRAMES == FM1P_BLOCK && FM1_BLOCK_WORDS == 2 * FM1P_BLOCK, "DX747's block is the ABI's");

/* ---- MIDI in: USB-MIDI packets (CIN in bits 0..3, the bytes from bit 8) ---- */
static uint8_t fm1core_dx_sx[1024];
static uint32_t fm1core_dx_sxn, fm1core_dx_sx_on, fm1core_dx_sx_lost;
static void fm1core_dx_sx_byte(uint8_t b)
{
    if (b == 0xF0u) {
        fm1core_dx_sx_on = 1;
        fm1core_dx_sxn = 0;
    }
    if (!fm1core_dx_sx_on)
        return;
    if (fm1core_dx_sxn == sizeof fm1core_dx_sx) {   /* too long: discarded, as the transports do */
        fm1core_dx_sx_on = 0;
        fm1core_dx_sx_lost++;
        return;
    }
    fm1core_dx_sx[fm1core_dx_sxn++] = b;
    if (b == 0xF7u) {
        fm1core_dx_sx_on = 0;
        midi_source = FM1_MIDI_USB;
        fm1_app_sysex(fm1core_dx_sx, fm1core_dx_sxn);
        fm1_web_control();
    }
}
int fm1core_dx_midi_in(uint32_t pkt)
{
    const uint32_t cin = pkt & 15u;
    uint8_t b[3];
    uint32_t n = 0, i;
    if (fm1core_halted() || !fm1core_dx_up)
        return 1;
    b[0] = (uint8_t)(pkt >> 8);
    b[1] = (uint8_t)(pkt >> 16);
    b[2] = (uint8_t)(pkt >> 24);
    if (cin >= 4u && cin <= 7u && (fm1core_dx_sx_on || b[0] == 0xF0u)) {
        n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n; i++)
            fm1core_dx_sx_byte(b[i]);
        return 1;
    }
    if (b[0] < 0x80u)
        return 1;
    if (b[0] < 0xF0u)
        n = (b[0] & 0xE0u) == 0xC0u ? 2u : 3u;
    else if (b[0] == 0xF1u || b[0] == 0xF3u)
        n = 2u;
    else if (b[0] == 0xF2u)
        n = 3u;
    else if (b[0] != 0xF0u && b[0] != 0xF7u)
        n = 1u;
    if (n && (b[0] < 0xF8u))
        fm1core_dx_sx_on = 0;             /* (a status byte ends a SysEx frame unfinished) */
    if (n)
        fm1_app_midi_received(FM1_MIDI_USB, b, n);
    return 1;
}

/* ---- MIDI out: Simulator.c's capture (fm1_midi_send_port) as USB-MIDI packets ---- */
static uint32_t fm1core_dx_out_at;        /* the byte of the captured message at capture_read */
int fm1core_dx_midi_out_take(uint32_t *pkt)
{
    const uint8_t *d;
    uint32_t n, k, cin, i;
    if (fm1core_halted() || !fm1core_dx_up)
        return 0;
    while (capture_read != capture_write && midi_capture[capture_read & 63u].port != FM1_MIDI_USB) {
        capture_read++;
        fm1core_dx_out_at = 0;
    }
    if (capture_read == capture_write)
        return 0;
    d = midi_capture[capture_read & 63u].data;
    n = midi_capture[capture_read & 63u].bytes;
    if (d[0] == 0xF0u || d[0] == 0xF7u || d[0] < 0x80u || fm1core_dx_out_at) {   /* SysEx (or a part of one): CIN 4
                                                                                    * continues, 5 / 6 / 7 end at F7 */
        k = n - fm1core_dx_out_at < 3u ? n - fm1core_dx_out_at : 3u;
        cin = fm1core_dx_out_at + k == n && d[n - 1u] == 0xF7u ? 4u + k : 4u;
        if (cin == 4u && k < 3u)
            cin = 0xFu;                   /* (a part of a frame that does not end it: single bytes) */
        if (cin == 0xFu)
            k = 1u;
        *pkt = cin;
        for (i = 0; i < k; i++)
            *pkt |= (uint32_t)d[fm1core_dx_out_at + i] << (8u + 8u * i);
        fm1core_dx_out_at += k;
        if (fm1core_dx_out_at >= n) {
            fm1core_dx_out_at = 0;
            capture_read++;
        }
        return 1;
    }
    cin = d[0] < 0xF0u ? d[0] >> 4 : n == 1u ? (d[0] >= 0xF8u ? 0xFu : 5u) : n == 2u ? 2u : 3u;
    *pkt = cin | (uint32_t)d[0] << 8 | (n > 1u ? (uint32_t)d[1] << 16 : 0u) | (n > 2u ? (uint32_t)d[2] << 24 : 0u);
    capture_read++;
    return 1;
}

/* ---- diagnostics ---- */
void fm1core_dx_dump(void)
{
    uint8_t v[8];
    groove_screen_snapshot(v);
    printf("dx747: %u ms, %llu frames; screen %u, part %u; buttons %04X keys %07X; master %d\n", (unsigned)now_ms,
           (unsigned long long)frames, (unsigned)v[1], (unsigned)v[0], (unsigned)fm1core_dx_btn_now,
           (unsigned)fm1core_dx_key_now, (int)fm1core_dx_master);
    printf("  flash: %s, %u writes; sysex in lost %u; midi out queued %u\n",
           fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty(), (unsigned)fm1core_dx_sx_lost,
           (unsigned)(capture_write - capture_read));
}
void fm1core_dx_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = 0;
}
void fm1core_dx_ui_info(char *buf, uint32_t n)
{
    uint8_t v[8];
    groove_screen_snapshot(v);
    snprintf(buf, n, "DX747, part %u", (unsigned)v[0] + 1u);
}
const int8_t *fm1core_dx_keymap(void) { return &FM1CORE_DX_KEYMAP[0][0]; }

/* ---- the flash (ABI 2): common/nor.c ---- */
uint8_t *fm1core_dx_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_dx_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_dx_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_dx_flash_sync(void) { return 1; }   /* (the firmware writes its storage as it goes) */
