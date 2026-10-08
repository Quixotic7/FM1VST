// SPDX-License-Identifier: GPL-3.0-only
// The emulator's script grammar (cores/ChoralRootFM1/tools/emu/README.md "Script grammar", emu.c load_script /
// run_script / bench) on an engine Device, so the headless scripts become the engine's regression tests.
//
// Supported: wait MS|MSms|Ss, frames N, key NAME down|up|[MS], btn NAME down|up|[MS], press / tap / hold / down /
// release / up (the older forms), knob|turn NAME +-N, master ADC, midi HEX [HEX [HEX]] [xN MS], expect led NAME
// on|lit|dim|off, expect sound|silence, quit, a leading "<ms>" (absolute time), '#' comments. The computer keys
// are keymap.c's table by name (cap) with emu.c's aliases (SHIFT, ENTER), including SELECT / PAGE DOWN / PAGE UP /
// UP / DOWN and Shift + Up / Down (the fine turn: GLO held around the detent, emu.c fine_turn / fine_frame).
// Parsed and ignored (they only look, or need a window): shot, window, dump, rec, and the keys F12 (dump),
// PRTSC / F13 (shot), INSERT (rec), ` (LCD view).
//
// Timing is emu.c's headless loop exactly: the commands at ms T run after the device's tick(T) and before its frame
// / idle (Device::run_ms's "between"); "expect sound" counts the samples of the blocks before ms T's; the run lasts
// until the last command's ms + 15 inclusive (emu.c: --headless without --frames), or stops after the ms of quit.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "device.h"

struct ScriptEvent {
    uint32_t ms = 0, seq = 0;
    int op = 0, kind = 0, idx = 0, n = 0;
    int km = -1;                // index in the keyboard map, or -1 (a panel control)
    std::string name;
    int line = 0;
};

struct Script {
    std::string path;
    std::vector<ScriptEvent> ev;   // sorted by (ms, seq), as emu.c's sev_cmp
    uint32_t end_ms = 0;           // the last ms run (inclusive) when no quit comes first
    std::vector<std::string> ignored;   // "line N: shot" ...: what was parsed and not acted on
};

// parse; false and a message (every error, "script line N: ...") on a script error
bool parse_script_text(const std::string &text, const std::string &name, Script &out, std::string &err);
bool load_script(const std::string &path, Script &out, std::string &err);

class ScriptRunner {
public:
    ScriptRunner(Device &dev, const Script &s);
    ~ScriptRunner();              // (detaches from the device if attached)
    ScriptRunner(const ScriptRunner &) = delete;
    ScriptRunner &operator=(const ScriptRunner &) = delete;
    bool done() const { return done_; }
    void step();                  // one device millisecond (and the commands due in it), its audio collected
    void run();                   // to the end
    // instead of step(): the script rides on whoever runs the device's clock (Device::set_between), e.g.
    // Device::render; no audio is collected (the expects on sound are then meaningless)
    void attach();
    void on_ms(uint32_t ms, bool frame);   // the commands of device ms (after its tick, before its frame)

    const std::vector<int16_t> &pcm() const { return pcm_; }   // int16 interleaved stereo, 44.1 kHz
    uint32_t ms_run() const { return ms_run_; }
    int expect_ok() const { return expect_ok_; }
    int expect_fail() const { return expect_fail_; }
    const std::vector<std::string> &log() const { return log_; }   // one line per expect, emu.c's wording
    bool keep_pcm = true;         // false: only count non-zero samples (the expects still work)
    uint64_t nonzero() const { return nz_; }

private:
    void apply(uint32_t ms);
    void key_action(int km, bool down);
    void hold_key(int k, int src, bool down);
    void hold_btn(int b, int src, bool down);
    void publish();
    void turn(int role, int steps);
    void fine_turn(int role, int steps);
    void fine_frame();
    void drain();

    Device &dev_;
    const Script &s_;
    size_t i_ = 0;
    bool done_ = false, quit_ = false, attached_ = false;
    uint32_t ms_run_ = 0;
    std::vector<int16_t> pcm_;
    uint64_t nz_ = 0, nz_mark_ = 0;
    int expect_ok_ = 0, expect_fail_ = 0;
    std::vector<std::string> log_;
    uint8_t key_src_[EMU_NKEY] = {}, btn_src_[EMU_NB] = {};
    int sel_knob_ = EMU_E_PRESETS;
    bool shift_held_ = false;
    int fine_steps_ = 0, fine_role_ = 0, fine_stage_ = 0;
};
