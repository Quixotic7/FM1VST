// SPDX-License-Identifier: GPL-3.0-only
// The device: one loaded core run on the caller's single-thread clock (FM1-VST-PLAN.md 4.1, 4.2).
//
// run_ms() is web/emu_web.c's run_ms (= one millisecond of emu.c's headless loop): tick(ms); a UI frame at ms 0
// and every 15 ms, idle() otherwise; then every 128-frame audio block that has fallen due at 44100 Hz, into a ring
// of int16 stereo frames. render() pulls the device only as far as the host block needs (no lookahead beyond one
// internal block and the resampler's two samples) and resamples to the host rate; at exactly 44100 Hz it is
// bypassed and the floats are the device's int16 samples / 32768.
//
// Threading: all calls on one thread (the audio thread in the plugin), as core-api/fm1core.h requires.
#pragma once
#include <cstdint>
#include <functional>
#include <utility>
#include "fm1core.h"
#include "resampler.h"

class Device {
public:
    explicit Device(const fm1core_t *core);

    const fm1core_t *core() const { return c_; }
    emu_hal_t *hal() const { return c_->hal; }

    // power-on: options (flash_path NULL or ram_only: a RAM-only flash, fresh every boot), the default boot guard,
    // hal->ready = 2 (simulated time, before init, exactly as web_boot), init
    void boot(const char *flash_path, bool ram_only);
    // power-on from a flash image in memory (ABI 2 flash_stage): n bytes (short: padded with 0xFF; nullptr / 0: a
    // fresh flash); nothing is written to any file afterwards, the image lives in the core (flash())
    void boot_from(const uint8_t *bytes, uint32_t n);
    bool booted() const { return booted_; }
    bool halted() const { return c_->halted && c_->halted() != 0; }

    // ---- input (web/emu_web.c's exports) ----
    void keys(uint32_t mask);               // bit n: note key n held (0 = F3 .. 26 = G5); a key newly down taps
    void keys_tap(uint32_t mask);           // a press shorter than 1 ms
    void buttons(uint32_t label_mask);      // bit i: label EMU_B_i held (FX SEL .. OCT+), via hal->btn_id
    void buttons_tap(uint32_t label_mask);
    void enc(int role, int32_t detents);    // EMU_E_* role, + clockwise; MASTER: detents x 16 of 1023
    // every detent enc() gave a knob role since power-on (SELECT .. KNOB4; 0 for MASTER): whoever turned it (the
    // plugin tells its own turns from the host's by this)
    int32_t enc_total(int role) const { return role >= 0 && role < EMU_NE - 1 ? enc_total_[role] : 0; }
    void master(int32_t v);                 // the MASTER pot, 0..1023
    int midi_in(uint32_t pkt);              // one USB-MIDI packet in; 0: no room (retry later)
    int midi_out_take(uint32_t *pkt);       // one USB-MIDI packet out; 0: none

    // ---- output ----
    const uint16_t *lcd() const { return c_->hal->lcd; }   // 240 x 240 RGB565, big-endian, as sent
    uint32_t lcd_writes() const { return c_->hal->lcd_writes; }
    uint8_t led(uint32_t col) const { return col < EMU_NCOL ? c_->hal->led[col] : 0; }
    uint8_t led_dim(uint32_t col) const { return col < EMU_NCOL ? c_->hal->led_dim[col] : 0; }
    int led_of(int id) const;               // key id (0..13 matrix button, 14 + n note key): 0 off, 1 dim, 2 lit
    int led_key(int key) const { return led_of(14 + key); }
    int led_button(int label) const;        // EMU_B_* label
    int led_play_green() const;             // PLAY's green LED: 0 or 2

    // ---- the flash image (ABI 2) ----
    uint8_t *flash() const { return c_->flash ? c_->flash() : nullptr; }      // flash_size() bytes, the live image
    uint32_t flash_size() const { return c_->flash ? c_->flash_size : 0; }
    uint32_t flash_dirty() const { return c_->flash_dirty ? c_->flash_dirty() : 0; }   // moves on erase / program
    bool flash_sync() { return booted_ && c_->flash_sync ? c_->flash_sync() != 0 : true; }   // see fm1core.h

    // ---- the clock ----
    uint32_t ms() const { return dev_ms_; }
    // one device millisecond. between (optional) runs after the tick and before the frame / idle, with this ms and
    // whether a frame follows: where emu.c's headless loop applies the script (tick, run_script, frame).
    // Without an argument the one set by set_between (if any) runs: so whoever drives the clock (run_ms or
    // render) applies the same input at the same device ms.
    using Between = std::function<void(uint32_t ms, bool frame)>;
    void run_ms(const Between &between = nullptr);
    void set_between(Between b) { between_ = std::move(b); }
    uint64_t frames_produced() const { return frames_done_; }   // device frames rendered since power-on

    // device frames (44.1 kHz) produced and not yet taken
    uint32_t avail() const { return ring_w_ - ring_r_; }
    // up to max_frames device frames, int16 interleaved stereo, as emu.c's --wav writes them; returns frames taken
    uint32_t take_s16(int16_t *stereo, uint32_t max_frames);

    // n host frames at host_rate: the device runs as far as needed, then the 44.1 kHz stream is resampled
    // (bypassed at exactly 44100). Changing host_rate restarts the resampler.
    void render(float *L, float *R, uint32_t n, double host_rate);
    // what render buffers ahead, in host frames: one internal block (128 frames at 44.1 kHz) plus the
    // resampler's lookahead (2 input frames) when it is not bypassed
    static uint32_t latency_frames(double host_rate);

    static constexpr uint32_t RING = 8192;  // stereo frames

private:
    void pop(float &l, float &r);

    const fm1core_t *c_;
    bool booted_ = false;
    uint32_t dev_ms_ = 0, last_frame_ = 0;
    uint64_t frames_done_ = 0;
    int32_t enc_total_[EMU_NE - 1] = {};
    int16_t ring_[RING * 2];
    uint32_t ring_w_ = 0, ring_r_ = 0;
    Lagrange4 rs_;
    double rs_rate_ = 0;                    // the host rate rs_ is set up for (0: none)
    Between between_;
};
