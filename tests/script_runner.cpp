// SPDX-License-Identifier: GPL-3.0-only
// The emulator's script interpreter on a Device: a port of emu.c's load_script / run_script / key_action and
// keymap.c's table (the data only, by name; no SDL). See script_runner.h.
#include "script_runner.h"

#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

enum { KM_KEY, KM_BTN, KM_OCTBOTH, KM_SELECT, KM_CYCLE, KM_SHIFT, KM_TURN, KM_SHOT, KM_RECORD, KM_DUMP, KM_LCDVIEW };
enum { OP_DOWN, OP_UP, OP_TURN, OP_SHOT, OP_WINDOW, OP_DUMP, OP_REC, OP_QUIT, OP_MASTER, OP_EXPECT_LED,
       OP_EXPECT_SND, OP_MIDI };
enum { SRC_SCRIPT = 16, SRC_FINE = 32 };

struct KeyMap {
    int kind, idx;
    const char *cap;
};
// keymap.c KEYMAP, in its order (kind, idx, cap)
const KeyMap KEYMAP[] = {
    {KM_KEY, 1, "F1"}, {KM_KEY, 3, "F2"}, {KM_KEY, 5, "F3"}, {KM_KEY, 8, "F4"},
    {KM_KEY, 0, "2"}, {KM_KEY, 2, "3"}, {KM_KEY, 4, "4"}, {KM_KEY, 7, "5"},
    {KM_KEY, 6, "TAB"},
    {KM_KEY, 7, "A"}, {KM_KEY, 9, "S"}, {KM_KEY, 11, "D"}, {KM_KEY, 12, "F"}, {KM_KEY, 14, "G"},
    {KM_KEY, 16, "H"}, {KM_KEY, 18, "J"}, {KM_KEY, 19, "K"}, {KM_KEY, 21, "L"}, {KM_KEY, 23, ";"},
    {KM_KEY, 24, "'"}, {KM_KEY, 26, "]"},
    {KM_KEY, 8, "W"}, {KM_KEY, 10, "E"}, {KM_KEY, 13, "T"}, {KM_KEY, 15, "Y"}, {KM_KEY, 17, "U"},
    {KM_KEY, 20, "O"}, {KM_KEY, 22, "P"},
    {KM_BTN, EMU_B_OCTDN, "Z"}, {KM_BTN, EMU_B_OCTUP, "X"}, {KM_BTN, EMU_B_OCTDN, "ESC"},
    {KM_BTN, EMU_B_OCTUP, "RETURN"}, {KM_OCTBOTH, 0, "END"},
    {KM_BTN, EMU_B_FX, "F5"}, {KM_BTN, EMU_B_SEL, "F6"}, {KM_BTN, EMU_B_ENV, "F7"}, {KM_BTN, EMU_B_LFO, "F8"},
    {KM_BTN, EMU_B_EDIT, "F9"}, {KM_BTN, EMU_B_GLO, "F10"},
    {KM_BTN, EMU_B_HOME, "7"}, {KM_BTN, EMU_B_SAVE, "8"}, {KM_BTN, EMU_B_ARP, "9"}, {KM_BTN, EMU_B_SEQ, "0"},
    {KM_BTN, EMU_B_PLAY, "-"}, {KM_BTN, EMU_B_REC, "="},
    {KM_CYCLE, +1, "PGDN"}, {KM_CYCLE, -1, "PGUP"},
    {KM_SHIFT, 0, "LSHIFT"}, {KM_SHIFT, 0, "RSHIFT"},
    {KM_TURN, +1, "UP"}, {KM_TURN, -1, "DOWN"},
    {KM_LCDVIEW, 0, "`"}, {KM_SHOT, 0, "PRTSC"}, {KM_SHOT, 0, "F13"}, {KM_RECORD, 0, "INSERT"},
    {KM_DUMP, 0, "F12"},
};
const int KEYMAP_N = (int)(sizeof KEYMAP / sizeof KEYMAP[0]);

const char *const BTN_NAME[EMU_NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                      "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
const char *const ENC_NAME[EMU_NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB1", "KNOB2", "KNOB3", "KNOB4", "MASTER"};

std::string note_name(int key)          // keymap.c emu_note_name: 0 = F3 (MIDI 53)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int m = 53 + key;
    return std::string(N[m % 12]) + std::to_string(m / 12 - 1);
}

bool find_control(const char *s, int &kind, int &idx)
{
    for (int i = 0; i < EMU_NKEY; i++)
        if (!strcasecmp(s, note_name(i).c_str())) { kind = KM_KEY; idx = i; return true; }
    for (int i = 0; i < EMU_NB; i++)
        if (!strcasecmp(s, BTN_NAME[i])) { kind = KM_BTN; idx = i; return true; }
    if (!strcasecmp(s, "SCL")) { kind = KM_BTN; idx = EMU_B_SEL; return true; }
    if (!strcasecmp(s, "ALGO")) { kind = KM_SELECT; idx = EMU_E_ALGO; return true; }
    for (int i = 0; i < EMU_NE; i++)
        if (!strcasecmp(s, ENC_NAME[i])) { kind = KM_SELECT; idx = i; return true; }
    return false;
}
bool find_key(const char *s, int &km, int &kind, int &idx)
{
    for (int i = 0; i < KEYMAP_N; i++)
        if (!strcasecmp(s, KEYMAP[i].cap)) {
            km = i;
            kind = KEYMAP[i].kind;
            idx = KEYMAP[i].idx;
            return true;
        }
    if (!strcasecmp(s, "SHIFT") || !strcasecmp(s, "ENTER") || !strcasecmp(s, "BACKSPACE"))   // (emu.c's aliases)
        return find_key(s[0] == 'S' || s[0] == 's' ? "RSHIFT" : s[0] == 'E' || s[0] == 'e' ? "RETURN" : "BKSP",
                        km, kind, idx);
    return false;
}
bool resolve(const char *s, int &km, int &kind, int &idx, bool controls_first)
{
    km = -1;
    if (!strncasecmp(s, "note:", 5))
        return find_control(s + 5, kind, idx) && kind == KM_KEY;
    if (controls_first && find_control(s, kind, idx))
        return true;
    return find_key(s, km, kind, idx) || find_control(s, kind, idx);
}
bool is_num(const char *s)
{
    if (*s == '+' || *s == '-')
        s++;
    return *s >= '0' && *s <= '9';
}
bool parse_dur(const char *s, uint32_t def, uint32_t &out)   // "200ms", "200", "1.5s"; false: not a duration
{
    char *e;
    if (!s || !*s) {
        out = def;
        return true;
    }
    double v = strtod(s, &e);
    if (e == s)
        return false;
    if (*e == 's' || *e == 'S')
        v *= 1000;
    out = (uint32_t)(v + .5);
    return true;
}

}  // namespace

// ================================================================= parse ===
bool parse_script_text(const std::string &text, const std::string &name, Script &out, std::string &err)
{
    out = Script();
    out.path = name;
    std::vector<ScriptEvent> &ev = out.ev;
    std::istringstream in(text);
    std::string sline;
    uint32_t cur = 0;
    int ln = 0, errors = 0;
    std::ostringstream emsg;
    auto add = [&](uint32_t ms, int op, int km, int kind, int idx, int n, const std::string &nm) {
        ScriptEvent e;
        e.ms = ms;
        e.seq = (uint32_t)ev.size();
        e.op = op;
        e.km = km;
        e.kind = kind;
        e.idx = idx;
        e.n = n;
        e.name = nm;
        e.line = ln;
        ev.push_back(e);
    };
    auto bad = [&](const std::string &m) {
        emsg << name << ": script line " << ln << ": " << m << "\n";
        errors++;
    };
    while (std::getline(in, sline)) {
        char line[256];
        std::snprintf(line, sizeof line, "%s", sline.c_str());
        ln++;
        for (char *h = line; (h = std::strchr(h, '#')) != nullptr; h++)   // '#' at a word's start (F#4 is a name)
            if (h == line || h[-1] == ' ' || h[-1] == '\t') {
                *h = 0;
                break;
            }
        char w[5][96];
        std::memset(w, 0, sizeof w);
        const int nw = std::sscanf(line, "%95s %95s %95s %95s %95s", w[0], w[1], w[2], w[3], w[4]);
        if (nw < 1)
            continue;
        int t = 0;
        if (w[0][0] >= '0' && w[0][0] <= '9') {   // "<ms> command": an absolute time
            cur = (uint32_t)std::strtoul(w[0], nullptr, 10);
            t = 1;
        }
        const char *cmd = w[t], *arg = w[t + 1], *arg2 = w[t + 2], *arg3 = w[t + 3];
        int km = -1, kind = -1, idx = 0;
        if (!std::strcmp(cmd, "wait")) {
            uint32_t d;
            if (!parse_dur(arg, 0, d))
                bad(std::string("wait: a duration (200, 200ms, 1.5s), not \"") + arg + "\"");
            else
                cur += d;
            continue;
        }
        if (!std::strcmp(cmd, "frames")) { cur += 15u * (uint32_t)std::atoi(arg); continue; }
        if (!std::strcmp(cmd, "shot") || !std::strcmp(cmd, "window") || !std::strcmp(cmd, "dump") ||
            !std::strcmp(cmd, "rec")) {
            const int op = cmd[0] == 's' ? OP_SHOT : cmd[0] == 'w' ? OP_WINDOW : cmd[0] == 'd' ? OP_DUMP : OP_REC;
            add(cur, op, -1, 0, 0, 0, arg);
            out.ignored.push_back("line " + std::to_string(ln) + ": " + cmd);
            continue;
        }
        if (!std::strcmp(cmd, "quit")) { add(cur, OP_QUIT, -1, 0, 0, 0, ""); continue; }
        if (!std::strcmp(cmd, "midi")) {          // (a USB-MIDI packet: CIN, status, d1, d2)
            unsigned b[3] = {0, 0, 0}, nb = 0;
            const char *ws[3] = {arg, arg2, arg3};
            bool okb = true;
            for (unsigned i = 0; i < 3u && ws[i][0] && ws[i][0] != 'x'; i++, nb++) {
                char *e;
                b[i] = (unsigned)std::strtoul(ws[i], &e, 16);
                if (*e || b[i] > 255u) {
                    bad(std::string("midi: \"") + ws[i] + "\" is not a hex byte");
                    okb = false;
                    break;
                }
            }
            if (!okb)
                continue;
            if (!nb || b[0] < 0x80u) { bad("midi: a status byte (80..FF) first"); continue; }
            const uint32_t pkt = (b[0] >= 0xF0u ? 0x0Fu : b[0] >> 4) | b[0] << 8 | (b[1] & 0x7Fu) << 16 |
                                 (b[2] & 0x7Fu) << 24;
            if (nb < 3u && ws[nb][0] == 'x') {        // xN MS: a train
                const int cnt = std::atoi(ws[nb] + 1);
                const double step = nb + 1u < 3u ? std::strtod(ws[nb + 1], nullptr) : 0;
                double at = cur;
                for (int k = 0; k < cnt; k++, at += step)
                    add((uint32_t)(at + .5), OP_MIDI, -1, 0, 0, (int)pkt, "");
                if (!t)
                    cur = (uint32_t)(at + .5);
            } else {
                add(cur, OP_MIDI, -1, 0, 0, (int)pkt, "");
            }
            continue;
        }
        if (!std::strcmp(cmd, "master")) { add(cur, OP_MASTER, -1, 0, 0, std::atoi(arg), ""); continue; }
        if (!std::strcmp(cmd, "expect")) {
            if (!std::strcmp(arg, "sound") || !std::strcmp(arg, "silence")) {
                add(cur, OP_EXPECT_SND, -1, 0, 0, !std::strcmp(arg, "sound"), "");
            } else if (!std::strcmp(arg, "led") && (!strcasecmp(arg2, "GREEN") ||
                       (resolve(arg2, km, kind, idx, true) && (kind == KM_KEY || kind == KM_BTN)))) {
                if (!strcasecmp(arg2, "GREEN"))
                    kind = -1;                    // PLAY's green LED
                const int lv = !std::strcmp(arg3, "on") || !std::strcmp(arg3, "lit") ? 2
                             : !std::strcmp(arg3, "dim") ? 1 : !std::strcmp(arg3, "off") ? 0 : -1;
                if (lv < 0)
                    bad(std::string("expect led ") + arg2 + ": on, dim or off, not \"" + arg3 + "\"");
                else
                    add(cur, OP_EXPECT_LED, -1, kind, idx, lv, std::string("led ") + arg2 + " " + arg3);
            } else {
                bad("expect: \"sound\", \"silence\" or \"led KEY on|dim|off\" (a key or a button)");
            }
            continue;
        }
        if (!std::strcmp(cmd, "key") || !std::strcmp(cmd, "btn") || !std::strcmp(cmd, "press") ||
            !std::strcmp(cmd, "tap") || !std::strcmp(cmd, "hold") || !std::strcmp(cmd, "down") ||
            !std::strcmp(cmd, "release") || !std::strcmp(cmd, "up")) {
            const bool controls = !std::strcmp(cmd, "btn");
            const char *mode = arg2;
            if (!arg[0] || !resolve(arg, km, kind, idx, controls)) {
                bad(std::string("unknown ") + (controls ? "control" : "key") + " \"" + arg + "\"");
                continue;
            }
            if (!std::strcmp(cmd, "hold") || !std::strcmp(cmd, "down"))
                mode = "down";
            else if (!std::strcmp(cmd, "release") || !std::strcmp(cmd, "up"))
                mode = "up";
            if (!std::strcmp(mode, "down")) {
                add(cur, OP_DOWN, km, kind, idx, 0, arg);
            } else if (!std::strcmp(mode, "up")) {
                add(cur, OP_UP, km, kind, idx, 0, arg);
            } else {                              // a press of MS
                uint32_t d;
                if (!parse_dur(mode, !std::strcmp(cmd, "tap") ? 60u : 100u, d)) {
                    bad(std::string(cmd) + " " + arg + ": down, up or a duration, not \"" + mode + "\"");
                    continue;
                }
                add(cur, OP_DOWN, km, kind, idx, 0, arg);
                add(cur + d, OP_UP, km, kind, idx, 0, arg);
                if (!t)
                    cur += d;
            }
            continue;
        }
        if (!std::strcmp(cmd, "knob") || !std::strcmp(cmd, "turn")) {
            if (!arg[0] || !resolve(arg, km, kind, idx, true) || kind != KM_SELECT) {
                bad(std::string("\"") + arg + "\" is not a knob (SELECT ALGORITHM PRESETS KNOB1..KNOB4 MASTER)");
                continue;
            }
            if (arg2[0] && !is_num(arg2)) {
                bad(std::string("knob ") + arg + ": a number of detents, not \"" + arg2 + "\"");
                continue;
            }
            add(cur, OP_TURN, -1, kind, idx, arg2[0] ? std::atoi(arg2) : 1, "");
            continue;
        }
        bad(std::string("unknown command \"") + cmd + "\"");
    }
    std::stable_sort(ev.begin(), ev.end(), [](const ScriptEvent &a, const ScriptEvent &b) {
        return a.ms != b.ms ? a.ms < b.ms : a.seq < b.seq;
    });
    // emu.c: --headless without --frames runs to the last command's ms + 15 (else 9 s), ms 0..end inclusive
    out.end_ms = ev.empty() ? 9000u : ev.back().ms + 15u;
    if (errors) {
        err = emsg.str();
        return false;
    }
    return true;
}

bool load_script(const std::string &path, Script &out, std::string &err)
{
    std::ifstream f(path);
    if (!f) {
        err = "script: cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return parse_script_text(ss.str(), path, out, err);
}

// ================================================================== run ===
ScriptRunner::ScriptRunner(Device &dev, const Script &s) : dev_(dev), s_(s) {}
ScriptRunner::~ScriptRunner()
{
    if (attached_)
        dev_.set_between(nullptr);
}

void ScriptRunner::publish()                       // emu.c hal_publish (through the Device's input calls)
{
    uint32_t k = 0, b = 0;
    for (int i = 0; i < EMU_NKEY; i++)
        if (key_src_[i])
            k |= 1u << i;
    for (int i = 0; i < EMU_NB; i++)
        if (btn_src_[i])
            b |= 1u << i;
    dev_.keys(k);                                  // (a key newly down taps, as hold_key)
    dev_.buttons(b);
}
void ScriptRunner::hold_key(int k, int src, bool down)
{
    if (k < 0 || k >= EMU_NKEY)
        return;
    key_src_[k] = (uint8_t)(down ? key_src_[k] | src : key_src_[k] & ~src);
    publish();
}
void ScriptRunner::hold_btn(int b, int src, bool down)
{
    if (b < 0 || b >= EMU_NB)
        return;
    btn_src_[b] = (uint8_t)(down ? btn_src_[b] | src : btn_src_[b] & ~src);
    publish();
}
void ScriptRunner::turn(int role, int steps)        // emu.c turn (MASTER: steps x 16)
{
    if (!steps || role < 0 || role >= EMU_NE)
        return;
    dev_.enc(role, steps);
}
void ScriptRunner::fine_turn(int role, int steps)   // emu.c fine_turn: GLO down now, the detent next frame
{
    if (fine_stage_ && role != fine_role_)
        return;
    fine_role_ = role;
    fine_steps_ += steps;
    if (!fine_stage_) {
        hold_btn(EMU_B_GLO, SRC_FINE, true);
        fine_stage_ = 1;
    } else if (fine_stage_ == 2) {
        fine_stage_ = 1;
    }
}
void ScriptRunner::fine_frame()                     // emu.c fine_frame: before each UI frame
{
    if (fine_stage_ == 1) {
        turn(fine_role_, fine_steps_);
        fine_steps_ = 0;
        fine_stage_ = 2;
    } else if (fine_stage_ == 2) {
        hold_btn(EMU_B_GLO, SRC_FINE, false);
        fine_stage_ = 0;
    }
}
void ScriptRunner::key_action(int kmi, bool down)   // emu.c key_action (src = the script)
{
    const KeyMap &m = KEYMAP[kmi];
    switch (m.kind) {
    case KM_KEY: hold_key(m.idx, SRC_SCRIPT, down); break;
    case KM_BTN: hold_btn(m.idx, SRC_SCRIPT, down); break;
    case KM_OCTBOTH:
        hold_btn(EMU_B_OCTDN, SRC_SCRIPT, down);
        hold_btn(EMU_B_OCTUP, SRC_SCRIPT, down);
        break;
    case KM_SELECT: if (down) sel_knob_ = m.idx; break;
    case KM_CYCLE:
        if (down) {
            static const int CYC[5] = {EMU_E_SELECT, EMU_E_K1, EMU_E_K2, EMU_E_K3, EMU_E_K4};
            int at = m.idx > 0 ? -1 : 0;           // off the cycle: Page Down -> SELECT, Page Up -> KNOB4
            for (int j = 0; j < 5; j++)
                if (CYC[j] == sel_knob_)
                    at = j;
            sel_knob_ = CYC[((at + m.idx) % 5 + 5) % 5];
        }
        break;
    case KM_SHIFT: shift_held_ = down; break;
    case KM_TURN:
        if (down && shift_held_ && sel_knob_ != EMU_E_MASTER)
            fine_turn(sel_knob_, m.idx);
        else if (down)
            turn(sel_knob_, sel_knob_ == EMU_E_MASTER ? 2 * m.idx : m.idx);
        break;
    default: break;                                // shot, record, dump, LCD view: nothing to do headless here
    }
}

void ScriptRunner::apply(uint32_t ms)              // emu.c run_script(ms)
{
    char buf[200];
    while (i_ < s_.ev.size() && s_.ev[i_].ms <= ms) {
        const ScriptEvent &e = s_.ev[i_++];
        switch (e.op) {
        case OP_DOWN:
        case OP_UP:
            if (e.km >= 0)
                key_action(e.km, e.op == OP_DOWN);
            else if (e.kind == KM_KEY)
                hold_key(e.idx, SRC_SCRIPT, e.op == OP_DOWN);
            else if (e.kind == KM_BTN)
                hold_btn(e.idx, SRC_SCRIPT, e.op == OP_DOWN);
            else if (e.kind == KM_SELECT && e.op == OP_DOWN)
                sel_knob_ = e.idx;
            break;
        case OP_TURN: turn(e.idx, e.n); break;
        case OP_MASTER: dev_.hal()->master = e.n; break;   // (emu.c: as given, no clamp)
        case OP_MIDI:
            if (!dev_.midi_in((uint32_t)e.n)) {
                std::snprintf(buf, sizeof buf, "script: midi in full at %u ms", (unsigned)ms);
                log_.push_back(buf);
            }
            break;
        case OP_EXPECT_LED: {
            const int lv = e.kind < 0 ? dev_.led_play_green()
                         : dev_.led_of(e.kind == KM_KEY ? 14 + e.idx : dev_.hal()->btn_id[e.idx]);
            const bool ok = lv == e.n;
            std::snprintf(buf, sizeof buf, "expect %s at %u ms: %s (the LED is %s)", e.name.c_str(), (unsigned)ms,
                          ok ? "ok" : "FAILED", lv == 2 ? "lit" : lv == 1 ? "dim" : "off");
            log_.push_back(buf);
            (ok ? expect_ok_ : expect_fail_)++;
            break;
        }
        case OP_EXPECT_SND: {
            const uint64_t nz = nz_ - nz_mark_;
            const bool ok = e.n ? nz > 0 : nz == 0;
            std::snprintf(buf, sizeof buf, "expect %s at %u ms: %s (%llu non-zero samples since the last check)",
                          e.n ? "sound" : "silence", (unsigned)ms, ok ? "ok" : "FAILED", (unsigned long long)nz);
            log_.push_back(buf);
            nz_mark_ = nz_;
            (ok ? expect_ok_ : expect_fail_)++;
            break;
        }
        case OP_QUIT: quit_ = true; break;
        default: break;                            // shot, window, dump, rec: ignored here
        }
    }
}

void ScriptRunner::drain()
{
    int16_t buf[EMU_BLOCK * 2 * 4];
    uint32_t n;
    while ((n = dev_.take_s16(buf, EMU_BLOCK * 4)) > 0) {
        for (uint32_t i = 0; i < 2 * n; i++)
            nz_ += buf[i] != 0;
        if (keep_pcm)
            pcm_.insert(pcm_.end(), buf, buf + 2 * n);
    }
}

void ScriptRunner::on_ms(uint32_t ms, bool frame)
{
    if (done_)
        return;
    apply(ms);
    if (frame)
        fine_frame();                              // (emu.c ui_frame: before emu_fw_frame)
    ms_run_++;
    if (quit_ || ms >= s_.end_ms)                  // emu.c bench: ms 0..end, or the ms of quit is the last
        done_ = true;
}

void ScriptRunner::step()
{
    if (done_)
        return;
    dev_.run_ms([this](uint32_t ms, bool frame) { on_ms(ms, frame); });
    drain();
}

void ScriptRunner::attach()
{
    attached_ = true;
    dev_.set_between([this](uint32_t ms, bool frame) { on_ms(ms, frame); });
}

void ScriptRunner::run()
{
    while (!done_)
        step();
}
