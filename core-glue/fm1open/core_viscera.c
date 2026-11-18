/* SPDX-License-Identifier: GPL-3.0-only */
/* Viscera 4-Track (https://github.com/De-Pattern/viscera, the pinned, unmodified submodule cores/viscera: GPL-3.0-only
 * with the FM1 SDK linking exception): the core unit. On the FM-1 Viscera is an APP on M-VAVE's FM1 Open SDK: its
 * portable core (core/: the engine, the parameters, the UI, MIDI in) under a bridge to the SDK (fm1/app/: screen.c
 * runs the panel, the UI and the screen from the SDK's control task; viscera_fm1.c renders the engine from the audio
 * interrupt and keeps the projects in the flash; audio_output.c applies MASTER). Its browser emulator runs the same
 * core with a platform layer of its own (web/vsc_wasm.c). This unit is that bridge on the ABI (core-api/fm1core.h),
 * the same statements where they apply; the core's four files are their own units (core_viscera.cmake):
 *   init      the NOR image (common/nor.c), then screen.c's start (groove_screen_init, start, run_engine): the engine
 *             (vsc_init), the FM-1's own settings from the flash, the UI, Bluetooth audio and MIDI "on", the project
 *             slots, the intro (its music, 32 sixteenths) and START WITH LAST (the project last saved or loaded)
 *   frame /   groove_screen_control, one control pass: the panel (held buttons and keys, the taps a press then a
 *   idle      release a pass later, the detents since the last pass, MASTER through groove_master_update), the UI
 *             (vsc_ui_update and its host requests: SAVE / LOAD into the project slots), the LEDs, the settings' save
 *             a second after a change while stopped; the screen painted band by band every 33 ms (fm1p_lcd_out_native:
 *             native RGB565, compared)
 *   audio     vsc_fm1_render then audio_output.c, per 128-frame block (VSC_BLOCK is the ABI's): the engine's float
 *             stereo, clipped to Q23, at MASTER's gain (moved 8 / 128 a block toward the knob's level), as int16
 *   audio in  (the .rfm1's ain1, fm1core_audio_in): the host's input is the FM-1's Bluetooth audio in, as the browser
 *             emulator gives the page's input (SOURCE BOTH / BT records it); a block with none is no input
 *   MIDI in   USB-MIDI packets: each channel or system message to vsc_midi_in (port 1, USB), as viscera_fm1.c gives
 *             every message the SDK's router sees; MMC STOP / PLAY (F0 7F .. 06 01 / 02 F7) as groove_screen_mmc
 *   LEDs      vsc_ui_leds by key id (FM1P_KEYMAP): lit 255; the backlight's glow (one scan frame in 6 / 3 / 2, LOW /
 *             MID / HIGH) FAINT / DIM / MID
 *   flash     viscera_fm1.c's store on the NOR image: the eight 8 KiB project slots and the settings sector at the top
 *             of fm1_open V14's sample-bank area (GROOVE_LIBRARY_OFFSET - 68 KiB = 0x6D000), the same checks
 * Not here (hardware, or nothing to drive it on a host): the start guard and SAFE MODE (OCT- + OCT+ at power-on, three
 * starts that never ran well), Bluetooth (its settings stay; scans and links find nothing), the render's CPU readout
 * and its guard (simulated time: no load, never eased), the battery (USB power: the bolt), USB audio in (the host's
 * input is the Bluetooth one), the stock firmware's sample bank and DX7 library (Viscera plays neither), the managers'
 * SysEx (M-UPGRADE's identity FM-1_8620600: the SDK's, not in the repository). Version: VERSION's "2.6.0" (VSC_VERSION,
 * as make_fm1_tree.py writes it into vsc_config.h). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1core.h"
#include "../common/version.h"
#include "exit.h"
#include "env.h"
#include "nor.h"
#include "vsc.h"
#include "exit_end.h"
#include "../common/panel.h"

static const int8_t FM1CORE_VS_KEYMAP[6][FM1P_NCOL] = FM1P_KEYMAP_INIT;   /* the SDK's key ids (fm1_led_set) */

/* ---- fm1_open V14's flash layout (app/instrument.h at 4c7496d, the commit make_fm1_tree.py pins) and Viscera's store
 * in it (fm1/app/viscera_fm1.h) ---- */
#define FM1CORE_VS_SECTOR 4096u                                    /* FM1_FLASH_SECTOR_BYTES */
#define FM1CORE_VS_SAMPLE_OFFSET 69632u                            /* GROOVE_SAMPLE_OFFSET */
#define FM1CORE_VS_FLASH_BYTES (FM1CORE_VS_SAMPLE_OFFSET + 466944u) /* GROOVE_FLASH_BYTES */
#define FM1CORE_VS_LIBRARY_OFFSET (FM1CORE_VS_FLASH_BYTES - 4096u - 4u * 4096u)   /* GROOVE_LIBRARY_OFFSET */
#define FM1CORE_VS_SAMPLE_LIMIT (FM1CORE_VS_LIBRARY_OFFSET - FM1CORE_VS_SAMPLE_OFFSET)   /* GROOVE_SAMPLE_LIMIT */
#define VSC_STORE_BYTES 69632u
#define VSC_STORE_OFFSET (FM1CORE_VS_LIBRARY_OFFSET - VSC_STORE_BYTES)
#define VSC_SAMPLE_LIMIT (FM1CORE_VS_SAMPLE_LIMIT - VSC_STORE_BYTES)
#define SLOT_BYTES 8192u
#define DEVICE_OFFSET (VSC_STORE_OFFSET + 8u * SLOT_BYTES)
_Static_assert(sizeof(struct vsc_project) <= SLOT_BYTES, "a project must fit its slot");
_Static_assert(sizeof(struct vsc_device) <= FM1CORE_VS_SECTOR - 16u, "device settings fit one sector");
_Static_assert(VSC_BLOCK == FM1P_BLOCK, "Viscera's block is the ABI's");

/* the SDK's fm1_flash_* on the NOR image (as core_dx747.c's) */
static int fm1core_vs_flash_read(uint32_t at, void *data, size_t bytes)
{
    if (!data || at > FM1CORE_VS_FLASH_BYTES || bytes > FM1CORE_VS_FLASH_BYTES - at)
        return -1;
    memcpy(data, fm1nor_image + at, bytes);
    return 0;
}
static int fm1core_vs_flash_write(uint32_t at, const void *data, size_t bytes)
{
    const uint8_t *s = data;
    size_t i;
    if (!data || at > FM1CORE_VS_FLASH_BYTES || bytes > FM1CORE_VS_FLASH_BYTES - at)
        return -1;
    for (i = 0; i < bytes; ++i)
        if ((fm1nor_image[at + i] & s[i]) != s[i])
            return -2;                    /* (FM1_FLASH_NEEDS_ERASE) */
    if (bytes) {
        memcpy(fm1nor_image + at, data, bytes);
        fm1nor_written(at, (uint32_t)bytes);
    }
    return 0;
}
static int fm1core_vs_flash_erase(uint32_t at)
{
    if (at % FM1CORE_VS_SECTOR || at > FM1CORE_VS_FLASH_BYTES - FM1CORE_VS_SECTOR)
        return -1;
    return fm1nor_erase(at) ? -1 : 0;
}

/* ---- fm1/app/viscera_fm1.c: storage (the same statements on the NOR image) ---- */
static int vsc_fm1_storage_ok(void)
{
    uint32_t header[3];
    if (fm1core_vs_flash_read(FM1CORE_VS_SAMPLE_OFFSET, header, sizeof header))
        return 0;
    if (header[0] == 0x53314d46u && header[2] > VSC_SAMPLE_LIMIT)   /* a stock bank running into the store */
        return 0;
    return 1;
}
static uint32_t slot_offset(unsigned slot) { return VSC_STORE_OFFSET + slot * SLOT_BYTES; }
static int head_ok(const uint32_t *head)
{
    return head[0] == VSC_MAGIC && ((head[1] == VSC_FORMAT && head[2] == sizeof(struct vsc_project)) ||
                                    (head[1] == 10u && head[2] == VSC_FORMAT_10_BYTES));
}
static int vsc_fm1_slot_info(unsigned slot, char name[12])
{
    uint32_t head[4];
    if (fm1core_vs_flash_read(slot_offset(slot), head, sizeof head))
        return 0;
    if (!head_ok(head))
        return 0;
    if (fm1core_vs_flash_read(slot_offset(slot) + 16u, name, 12))
        return 0;
    return 1;
}
static int vsc_fm1_save(unsigned slot)
{
    char name[12] = "PROJECT  ";
    uint32_t base, o;
    if (slot >= VSC_PROJECTS || !vsc_fm1_storage_ok())
        return -1;
    if (vsc_meter.playing)
        return -2;
    vsc.magic = VSC_MAGIC;
    vsc.version = VSC_FORMAT;
    vsc.bytes = sizeof vsc;
    name[8] = (char)('1' + slot);
    name[9] = 0;
    vsc_memcpy(vsc.name, name, 12);
    vsc.crc = vsc_crc32(&vsc.name, sizeof vsc - offsetof(struct vsc_project, name));
    base = slot_offset(slot);
    for (o = 0; o < SLOT_BYTES; o += FM1CORE_VS_SECTOR)
        if (fm1core_vs_flash_erase(base + o))
            return -3;
    if (fm1core_vs_flash_write(base, &vsc, sizeof vsc))
        return -3;
    if (vsc_dev.last_slot != slot + 1u) {   /* START WITH LAST */
        vsc_dev.last_slot = (uint8_t)(slot + 1u);
        ++vsc_dev_changed;
    }
    return 0;
}
static int vsc_fm1_load(unsigned slot)
{
    uint32_t base, head[4], c = 0xffffffffu, at = offsetof(struct vsc_project, name), end;
    uint8_t buf[256];
    if (slot >= VSC_PROJECTS)
        return -1;
    base = slot_offset(slot);
    if (fm1core_vs_flash_read(base, head, sizeof head))
        return -3;
    if (!head_ok(head))
        return -4;
    end = head[2];
    while (at < end) {
        uint32_t n = end - at < sizeof buf ? end - at : sizeof buf, i;
        int k;
        if (fm1core_vs_flash_read(base + at, buf, n))
            return -3;
        for (i = 0; i < n; ++i) {
            c ^= buf[i];
            for (k = 0; k < 8; ++k)
                c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
        }
        at += n;
    }
    if (~c != head[3])
        return -4;
    vsc_event(EV_STOP, 0, 0);
    vsc_event(EV_PANIC, 0, 0);
    if (fm1core_vs_flash_read(base, &vsc, end))
        return -3;
    if (head[1] != VSC_FORMAT && !vsc_project_upgrade(&vsc)) {
        vsc_project_defaults(&vsc);
        return -4;
    }
    vsc_learn_release();
    if (vsc_dev.last_slot != slot + 1u) {   /* START WITH LAST */
        vsc_dev.last_slot = (uint8_t)(slot + 1u);
        ++vsc_dev_changed;
    }
    return 0;
}
static void vsc_fm1_device_load(void)
{
    if (!vsc_fm1_storage_ok() || fm1core_vs_flash_read(DEVICE_OFFSET, &vsc_dev, sizeof vsc_dev) ||
        !vsc_device_valid(&vsc_dev))
        vsc_device_defaults();
}
static int vsc_fm1_device_save(void)
{
    if (!vsc_fm1_storage_ok())
        return -1;
    vsc_device_seal();
    if (fm1core_vs_flash_erase(DEVICE_OFFSET))
        return -3;
    if (fm1core_vs_flash_write(DEVICE_OFFSET, &vsc_dev, sizeof vsc_dev))
        return -3;
    return 0;
}

/* ---- fm1/app/screen.c: the control task ---- */
fm1panel_t fm1core_vs_panel;
static uint32_t fm1core_vs_ms;            /* the device's ms (tick) */
static uint8_t fm1core_vs_up;
static uint16_t fm1core_vs_screen[VSC_SCREEN_W * VSC_SCREEN_H];
static uint32_t frame_at, status_at, slots_at, dev_saved_gen, dev_seen_gen, dev_changed_at;
static uint8_t bt_audio_on = 1, bt_midi_on = 1;   /* configure_bt(FM1_BT_AUDIO | FM1_BT_MIDI) at start */
static uint8_t fm1core_vs_led[41];
static int32_t fm1core_vs_knob = 1023;    /* the MASTER pot (0..1023) */
/* groove_master_update (fm1_open's engine, web/engine/sound.c): the knob's ADC to the output level 0..127 */
static unsigned master_adc = 0xFFFFu, output_level;
static void groove_master_update(unsigned adc)
{
    unsigned distance;
    if (adc > 1023u)
        return;
    distance = adc > master_adc ? adc - master_adc : master_adc - adc;
    if (master_adc <= 1023u && adc > 4u && adc < 1019u && distance < 4u)
        return;
    output_level = adc <= 4u ? 0u : adc >= 1019u ? 127u : (adc * 127u + 511u) / 1023u;
    master_adc = adc;
}

static void refresh_slots(void)
{
    unsigned s, i;
    vsc_host.storage_ok = (uint8_t)vsc_fm1_storage_ok();
    for (s = 0; s < VSC_PROJECTS; ++s) {
        char name[12];
        vsc_host.slot_used[s] = (uint8_t)vsc_fm1_slot_info(s, name);
        for (i = 0; i < 12; ++i)
            vsc_host.slot_name[s][i] = vsc_host.slot_used[s] ? name[i] : 0;
        vsc_host.slot_name[s][11] = 0;
    }
}
static void refresh_status(void)          /* (no radio: Bluetooth's switches, nothing connected but the host's input) */
{
    vsc_host.bt_audio = bt_audio_on;
    vsc_host.bt_midi = bt_midi_on;
    vsc_host.usb_connected = 1;           /* a host is there */
    vsc_host.ble_role = 0;
    vsc_host.ble_state = 0;
    vsc_host.ble_count = 0;
    vsc_host.battery = 4;                 /* (on USB power: the bolt) */
    vsc_host.relief = vsc_relief;
}
static void host(unsigned req, unsigned arg)
{
    int r;
    switch (req) {
    case HOST_BT_AUDIO: bt_audio_on = (uint8_t)(arg != 0); break;
    case HOST_BT_MIDI: bt_midi_on = (uint8_t)(arg != 0); break;
    case HOST_SAVE:
        r = vsc_fm1_save(arg);
        vsc_ui_notice(r == 0 ? "SAVED" : r == -2 ? "STOP TO SAVE" : "SAVE FAILED", 1500);
        refresh_slots();
        break;
    case HOST_LOAD:
        r = vsc_fm1_load(arg);
        vsc_ui_notice(r == 0 ? "LOADED" : "LOAD FAILED", 1500);
        break;
    default: break;                       /* (BLE role, scan, connect, forget, disconnect: no radio) */
    }
}
static char open_msg[32];
static void open_last(void)               /* START WITH LAST (no start guard here: every start ran well) */
{
    unsigned s = vsc_dev.last_slot, i = 0, k;
    const char *a = "OPENED ";
    int r;
    if (vsc_dev.start_blank || !s || s > VSC_PROJECTS)
        return;
    r = vsc_fm1_load(s - 1u);
    if (r == 0) {
        while (*a)
            open_msg[i++] = *a++;
        open_msg[i++] = (char)('0' + s);
        open_msg[i++] = ' ';
        for (k = 0; k < 11 && vsc.name[k]; ++k)
            open_msg[i++] = vsc.name[k];
        open_msg[i] = 0;
        vsc_ui_notice(open_msg, 6000);
    } else if (r == -4) {
        vsc_ui_notice("LAST PROJECT DAMAGED: NEW", 6000);
    }
}
static void run_engine(void)              /* screen.c run_engine, with viscera_fm1.c vsc_fm1_start */
{
    vsc_ui_notice("", 0);
    vsc_init();                           /* (vsc_clock_now unset: no CPU readout) */
    vsc_project_defaults(&vsc);
    vsc_fm1_device_load();
    vsc_ui_init();
    bt_audio_on = bt_midi_on = 1;
    refresh_status();
    refresh_slots();
    vsc_ui_intro();
    open_last();
}
static void leds(void)                    /* screen.c leds: lit, the backlight's glow (one scan frame in div) */
{
    static const uint8_t glow[4] = {FM1P_LED_OFF, FM1P_LED_FAINT, FM1P_LED_DIM, FM1P_LED_MID};   /* OFF LOW MID HIGH */
    uint8_t want[41];
    unsigned i;
    vsc_ui_leds(want);
    for (i = 0; i < 41; ++i) {
        const uint8_t v = want[i] == 2 ? FM1P_LED_LIT : want[i] == 1 ? glow[vsc_dev.backlight & 3u] : FM1P_LED_OFF;
        if (v != fm1core_vs_led[i]) {
            fm1core_vs_led[i] = v;
            fm1p_led_id(&fm1core_vs_panel, &FM1CORE_VS_KEYMAP[0][0], (int)i, v);
        }
    }
}
static void device_settings(uint32_t now) /* the FM-1's own settings: a second after the last change, while stopped */
{
    uint32_t g = vsc_dev_changed;
    if (g != dev_seen_gen) {
        dev_seen_gen = g;
        dev_changed_at = now;
    }
    if (g == dev_saved_gen || vsc_meter.playing || now - dev_changed_at < 1000u)
        return;
    dev_saved_gen = g;
    if (vsc_fm1_device_save() != 0)
        vsc_ui_notice("NOT SAVED", 1200);
}
static void paint(uint32_t now)           /* a new frame every ~33 ms, all its bands */
{
    unsigned y;
    if (now - frame_at < 33u)
        return;
    frame_at = now;
    vsc_ui_frame(now);
    for (y = 0; y < VSC_SCREEN_H; y += VSC_BAND)
        vsc_ui_paint_band(y, fm1core_vs_screen + y * VSC_SCREEN_W);
    fm1p_lcd_out_native(&fm1core_vs_panel, fm1core_vs_screen);
}
static void fm1core_vs_control(void)      /* groove_screen_control: one pass */
{
    fm1panel_t *p = &fm1core_vs_panel;
    struct vsc_ui_input in;
    const uint32_t now = fm1core_vs_ms, bt = fm1p_take_button_taps(p), kt = fm1p_take_key_taps(p);
    unsigned arg = 0, req, i;
    int32_t m;
    /* the panel as fm1_controls_read gives it: held, and a tap down this pass (up the next: not held then) */
    in.buttons = p->buttons | bt;
    in.keys = p->keys | kt;
    in.now_ms = now;
    for (i = 0; i < 7; ++i) {
        int32_t d = fm1p_take_enc(p, i);
        in.enc[i] = (int16_t)(d > 32767 ? 32767 : d < -32768 ? -32768 : d);
    }
    m = p->master < 0 ? 0 : p->master > 1023 ? 1023 : p->master;
    fm1core_vs_knob = m;
    groove_master_update((unsigned)m);
    req = vsc_ui_update(&in, &arg);
    if (req)
        host(req, arg);
    leds();
    device_settings(now);
    if (now - status_at >= 250u) {
        status_at = now;
        refresh_status();
    }
    if (now - slots_at >= 3000u) {
        slots_at = now;
        refresh_slots();
    }
    paint(now);
}

/* ---- the ABI ---- */
int fm1core_vs_halted(void) { return fm1core_halted(); }
void fm1core_vs_shutdown(void) { fm1core_run_exit(); }

void fm1core_vs_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    (void)save_on_exit;                   /* (every write goes through: nothing kept to save at exit) */
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_options(flash_path, no_flash, headless, "build/" FM1CORE_ID "/flash.bin");
}

void fm1core_vs_init(int demo)
{
    fm1panel_t *p = &fm1core_vs_panel;
    struct vsc_ui_input none;
    unsigned arg = 0;
    (void)demo;
    fm1core_exit_cleanup = fm1nor_close;
    fm1nor_open();
    fm1p_panel_default(p);                /* Viscera's BTN_* are the matrix ids: OCT- OCT+ FX SEL .. REC */
    fm1core_vs_ms = 0;
    frame_at = 0u - 1000u;
    status_at = slots_at = 0;
    master_adc = 0xFFFFu;
    output_level = 0;
    fm1core_vs_knob = p->master > 0 ? (p->master > 1023 ? 1023 : p->master) : 1023;
    p->master = fm1core_vs_knob;
    groove_master_update((unsigned)fm1core_vs_knob);
    memset(fm1core_vs_led, 0xFF, sizeof fm1core_vs_led);
    /* groove_screen_init, then the first control pass's start (no start guard, no safe mode) */
    vsc_project_defaults(&vsc);
    vsc_ui_init();
    run_engine();
    dev_saved_gen = dev_seen_gen = vsc_dev_changed;
    memset(&none, 0, sizeof none);
    (void)vsc_ui_update(&none, &arg);     /* (the intro's clock starts at this pass, as the emulator's intro export) */
    fm1core_vs_up = 1;
    p->ready = 1;
    leds();
    paint(0);
}

void fm1core_vs_tick(uint32_t ms) { fm1core_vs_ms = ms; }
void fm1core_vs_frame(void)
{
    if (fm1core_halted() || !fm1core_vs_up)
        return;
    fm1core_vs_control();
}
void fm1core_vs_idle(void) { fm1core_vs_frame(); }

/* the host's audio input (ain1): the next audio() call's frames, as the FM-1's Bluetooth audio in */
static float fm1core_vs_in[4096u * 2u];
static uint32_t fm1core_vs_in_n;          /* frames given for the next audio(); 0: none */
void fm1core_audio_in(const int16_t *stereo, uint32_t frames)
{
    uint32_t i;
    if (frames > 4096u)
        frames = 4096u;
    for (i = 0; i < 2u * frames; i++)
        fm1core_vs_in[i] = (float)stereo[i] * (1.f / 32768.f);
    fm1core_vs_in_n = frames;
}
void fm1core_audio_in_info(uint32_t state, uint32_t latency_frames)
{
    (void)latency_frames;
    if (!state)
        fm1core_vs_in_n = 0;
}

static unsigned output_gain;              /* audio_output.c: MASTER's gain, 0..127, moved 8 a block */
void fm1core_vs_audio(int16_t *out, uint32_t frames)
{
    static float mix[VSC_BLOCK * 2];
    const uint32_t in_n = fm1core_vs_in_n;
    uint32_t f, i;
    fm1core_vs_in_n = 0;
    for (f = 0; f + VSC_BLOCK <= frames; f += VSC_BLOCK) {
        int16_t *o = out + 2u * f;
        struct vsc_inputs in = {0, 0};
        int delta;
        if (fm1core_halted() || !fm1core_vs_up || fm1nor_stall_take()) {   /* (a flash erase: the audio IRQ off) */
            memset(o, 0, VSC_BLOCK * 2u * sizeof *o);
            continue;
        }
        if (bt_audio_on && f + VSC_BLOCK <= in_n)
            in.bt = fm1core_vs_in + 2u * f;
        vsc_host.bt_audio_connected = vsc_host.bt_audio_playing = (uint8_t)(in.bt != 0);
        vsc_render(&in, mix);
        delta = (int)output_level - (int)output_gain;
        output_gain = (unsigned)((int)output_gain + (delta > 8 ? 8 : delta < -8 ? -8 : delta));
        for (i = 0; i < 2u * VSC_BLOCK; i++) {
            float v = mix[i];
            int32_t q;
            if (v > 1.f)
                v = 1.f;
            else if (v < -1.f)
                v = -1.f;
            q = (int32_t)(((int64_t)(int32_t)(v * 8388607.f) * output_gain) >> 7);   /* Q23 at MASTER */
            o[i] = (int16_t)(q >> 8);
        }
    }
    if (f < frames)
        memset(out + 2u * f, 0, (size_t)(frames - f) * 2u * sizeof *out);
}

/* ---- MIDI in: USB-MIDI packets (CIN in bits 0..3, the bytes from bit 8) ---- */
static uint8_t fm1core_vs_sx[16];
static uint32_t fm1core_vs_sxn, fm1core_vs_sx_on;
static void fm1core_vs_sx_byte(uint8_t b)  /* MMC only (fm1_open transfer.c: F0 7F <dev> 06 <cmd> F7) */
{
    if (b == 0xF0u) {
        fm1core_vs_sx_on = 1;
        fm1core_vs_sxn = 0;
    }
    if (!fm1core_vs_sx_on)
        return;
    if (fm1core_vs_sxn == sizeof fm1core_vs_sx) {   /* longer than MMC: not ours */
        fm1core_vs_sx_on = 0;
        return;
    }
    fm1core_vs_sx[fm1core_vs_sxn++] = b;
    if (b == 0xF7u) {
        fm1core_vs_sx_on = 0;
        if (fm1core_vs_sxn == 6u && fm1core_vs_sx[1] == 0x7Fu && fm1core_vs_sx[3] == 0x06u) {
            if (fm1core_vs_sx[4] == 1u)
                vsc_event(EV_STOP, 0, 0);
            else if (fm1core_vs_sx[4] == 2u && !vsc_meter.playing)
                vsc_event(EV_PLAY, 1, 0);
        }
    }
}
int fm1core_vs_midi_in(uint32_t pkt)
{
    const uint32_t cin = pkt & 15u;
    uint8_t b[3];
    uint32_t n = 0, i;
    if (fm1core_halted() || !fm1core_vs_up)
        return 1;
    b[0] = (uint8_t)(pkt >> 8);
    b[1] = (uint8_t)(pkt >> 16);
    b[2] = (uint8_t)(pkt >> 24);
    if (cin >= 4u && cin <= 7u && (fm1core_vs_sx_on || b[0] == 0xF0u)) {
        n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n; i++)
            fm1core_vs_sx_byte(b[i]);
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
    if (n && b[0] < 0xF8u)
        fm1core_vs_sx_on = 0;
    if (n)
        vsc_midi_in(1, b, n, fm1core_vs_ms);   /* (viscera_fm1.c vsc_fm1_midi: port 1, USB) */
    return 1;
}
int fm1core_vs_midi_out_take(uint32_t *pkt)   /* (Viscera sends no MIDI) */
{
    (void)pkt;
    return 0;
}

/* ---- diagnostics ---- */
void fm1core_vs_dump(void)
{
    printf("viscera: %u ms; transport %u recording %X; loop %u pos %u; master %d (level %u)\n", (unsigned)fm1core_vs_ms,
           (unsigned)vsc_meter.transport, (unsigned)vsc_meter.recording, (unsigned)vsc_meter.loop,
           (unsigned)vsc_meter.pos, (int)fm1core_vs_knob, output_level);
    printf("  flash: %s, %u writes\n", fm1nor_path()[0] ? fm1nor_path() : "RAM only", (unsigned)fm1nor_dirty());
}
void fm1core_vs_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = 0;
    *cpu_pct = 0;
}
void fm1core_vs_ui_info(char *buf, uint32_t n)
{
    snprintf(buf, n, "transport %u rec %X tracks %u%u%u%u loop %u in %u", (unsigned)vsc_meter.transport,
             (unsigned)vsc_meter.recording, (unsigned)vsc_meter.has[0], (unsigned)vsc_meter.has[1],
             (unsigned)vsc_meter.has[2], (unsigned)vsc_meter.has[3], (unsigned)vsc_meter.loop,
             (unsigned)vsc_host.bt_audio_playing);
}
const int8_t *fm1core_vs_keymap(void) { return &FM1CORE_VS_KEYMAP[0][0]; }

/* ---- the flash (ABI 2): common/nor.c ---- */
uint8_t *fm1core_vs_flash(void) { return fm1nor_flash(); }
uint32_t fm1core_vs_flash_dirty(void) { return fm1nor_dirty(); }
int fm1core_vs_flash_stage(const uint8_t *bytes, uint32_t n) { return fm1nor_stage(bytes, n); }
int fm1core_vs_flash_sync(void)           /* the settings' pending save without its second; deferred while playing */
{
    if (dev_saved_gen == vsc_dev_changed)
        return 1;
    if (vsc_meter.playing)
        return 0;
    dev_saved_gen = dev_seen_gen = vsc_dev_changed;
    return vsc_fm1_device_save() == 0;
}
