// SPDX-License-Identifier: GPL-3.0-only
// The device clock (web/emu_web.c run_ms), its input and output, and the host-rate render.
#include "device.h"

#include <cmath>
#include <cstring>

Device::Device(const fm1core_t *core) : c_(core) { std::memset(ring_, 0, sizeof ring_); }

void Device::boot(const char *flash_path, bool ram_only)
{
    if (booted_)
        return;
    const bool ram = ram_only || !flash_path;
    c_->options(ram ? nullptr : flash_path, ram ? 1 : 0, 0, 1);   // (headless = 1: never the default file)
    if (c_->boot_options)
        c_->boot_options(-1, nullptr, -1);  // a power-on with a clean guard record (emu.c's defaults)
    c_->hal->ready = 2;                     // simulated time (fm1_ticks = the device clock), before init
    c_->init(0);
    booted_ = true;
}

void Device::boot_from(const uint8_t *bytes, uint32_t n)
{
    if (booted_)
        return;
    c_->flash_stage(bytes, bytes ? n : 0u);
    c_->options(FM1CORE_FLASH_STAGED, 0, 0, 1);
    if (c_->boot_options)
        c_->boot_options(-1, nullptr, -1);
    c_->hal->ready = 2;
    c_->init(0);
    booted_ = true;
}

// ------------------------------------------------------------------ input ---
void Device::keys(uint32_t m)
{
    emu_hal_t *h = c_->hal;
    m &= (1u << EMU_NKEY) - 1u;
    h->keys_tap |= m & ~h->keys;
    h->keys = m;
}
void Device::keys_tap(uint32_t m) { c_->hal->keys_tap |= m & ((1u << EMU_NKEY) - 1u); }

static uint32_t btn_matrix(const emu_hal_t *h, uint32_t m)   // label bits (EMU_B_*) -> matrix bits
{
    uint32_t b = 0;
    for (uint32_t i = 0; i < EMU_NB; i++)
        if (m & (1u << i))
            b |= 1u << h->btn_id[i];
    return b;
}
void Device::buttons(uint32_t m)
{
    emu_hal_t *h = c_->hal;
    const uint32_t b = btn_matrix(h, m);
    h->buttons_tap |= b & ~h->buttons;
    h->buttons = b;
}
void Device::buttons_tap(uint32_t m) { c_->hal->buttons_tap |= btn_matrix(c_->hal, m); }

void Device::enc(int role, int32_t n)
{
    emu_hal_t *h = c_->hal;
    if (role == EMU_E_MASTER) {
        const int32_t m = h->master + n * 16;
        h->master = m < 0 ? 0 : m > 1023 ? 1023 : m;
    } else if (role >= 0 && role < EMU_NE - 1) {
        h->enc[h->enc_id[role]] += n * h->enc_dir[role];
    }
}
void Device::master(int32_t v) { c_->hal->master = v < 0 ? 0 : v > 1023 ? 1023 : v; }
int Device::midi_in(uint32_t pkt) { return booted_ ? c_->midi_in(pkt) : 0; }
int Device::midi_out_take(uint32_t *pkt) { return booted_ ? c_->midi_out_take(pkt) : 0; }

// ----------------------------------------------------------------- output ---
int Device::led_of(int id) const              // emu_web.c led_of
{
    const emu_hal_t *h = c_->hal;
    for (int r = 1; r < 5; r++)
        for (int col = 0; col < EMU_NCOL; col++)
            if (c_->keymap[r * EMU_NCOL + col] == id)
                return (h->led[col] >> r) & 1u ? 2 : (h->led_dim[col] >> r) & 1u ? 1 : 0;
    return 0;
}
int Device::led_button(int label) const
{
    return label >= 0 && label < EMU_NB ? led_of(c_->hal->btn_id[label]) : 0;
}
int Device::led_play_green() const
{
    const uint32_t q = c_->hal->led_play_green;
    return (int)(((c_->hal->led[q >> 3] >> (q & 7u)) & 1u) * 2u);
}

// ------------------------------------------------------------------ clock ---
void Device::run_ms(const Between &between)   // emu_web.c run_ms (emu.c bench: tick, script, frame, audio)
{
    const uint32_t ms = dev_ms_++;
    c_->tick(ms);
    const bool frame = ms == 0 || ms - last_frame_ >= 15u;
    if (between)
        between(ms, frame);
    else if (between_)
        between_(ms, frame);
    if (frame) {
        last_frame_ = ms;
        c_->frame();
    } else {
        c_->idle();
    }
    while (frames_done_ + EMU_BLOCK <= (uint64_t)ms * EMU_FS / 1000u) {
        int16_t blk[EMU_BLOCK * 2];
        c_->audio(blk, EMU_BLOCK);
        if (ring_w_ - ring_r_ > RING - EMU_BLOCK)
            ring_r_ = ring_w_ - (RING - EMU_BLOCK);   // (overrun: nobody takes the audio; the oldest goes)
        for (uint32_t k = 0; k < EMU_BLOCK; k++) {
            const uint32_t i = ring_w_++ % RING;
            ring_[2 * i] = blk[2 * k];
            ring_[2 * i + 1] = blk[2 * k + 1];
        }
        frames_done_ += EMU_BLOCK;
    }
}

uint32_t Device::take_s16(int16_t *out, uint32_t max_frames)
{
    uint32_t n = 0;
    while (n < max_frames && ring_r_ != ring_w_) {
        const uint32_t i = ring_r_++ % RING;
        out[2 * n] = ring_[2 * i];
        out[2 * n + 1] = ring_[2 * i + 1];
        n++;
    }
    return n;
}

void Device::pop(float &l, float &r)          // one device frame, the clock run until it exists
{
    while (ring_r_ == ring_w_)
        run_ms();
    const uint32_t i = ring_r_++ % RING;
    l = (float)ring_[2 * i] * (1.0f / 32768.0f);
    r = (float)ring_[2 * i + 1] * (1.0f / 32768.0f);
}

void Device::render(float *L, float *R, uint32_t n, double host_rate)
{
    if (!booted_) {
        std::memset(L, 0, n * sizeof *L);
        std::memset(R, 0, n * sizeof *R);
        return;
    }
    if (host_rate == (double)EMU_FS) {        // bypass: the device's samples as they are
        rs_rate_ = 0;
        for (uint32_t k = 0; k < n; k++)
            pop(L[k], R[k]);
        return;
    }
    if (rs_rate_ != host_rate) {
        rs_.reset((double)EMU_FS, host_rate);
        rs_rate_ = host_rate;
    }
    for (uint32_t k = 0; k < n; k++) {
        while (rs_.need()) {
            float l, r;
            pop(l, r);
            rs_.push(l, r);
        }
        rs_.take(L[k], R[k]);
    }
}

uint32_t Device::latency_frames(double host_rate)
{
    if (host_rate == (double)EMU_FS)
        return EMU_BLOCK;
    return (uint32_t)std::ceil((double)(EMU_BLOCK + Lagrange4::LOOKAHEAD) * host_rate / (double)EMU_FS);
}
