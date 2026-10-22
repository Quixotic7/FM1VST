// SPDX-License-Identifier: GPL-3.0-only
// The felucca_web family's golden check (fw_common.cmake fw_golden):
//
//   fw_golden_<id> MODULE.fm1core ID [OUT_DIR]
//
// The loaded core (through the engine's Device, as the plugin drives it: tick, frame / idle, audio blocks) and the
// firmware's own browser unit built natively (fw_ref.c: its web_* exports, one web_ms a device millisecond) run side
// by side from power-on with the same input script: notes, a button tap (ENV), knob turns, HOME, ALGORITHM, a chord,
// PLAY and PLAY again. At each checkpoint the two screens are compared pixel for pixel; the whole audio stream is
// compared sample for sample. Both must be identical (the core runs the firmware's own unit, one web_ms per device ms,
// with the input applied where web_buttons / web_keys / web_enc would apply it). The screens at the checkpoints are
// written to OUT_DIR/<id>_golden_<ms>.ppm.
// FW_GOLDEN_SYSEX=<file> (a sampler with no factory bank: PIGEON): a file of SysEx frames (tests/script_runner.h's
// `sysex`, e.g. a sample bank's upload) sent to a fresh copy of the core after its splash; both runs then power on
// from that copy's flash image (the reference: fw_ref_boot_from, the bytes in its nor[] before web_boot).
// FW_GOLDEN_DELAY_MS=<n>: the script n ms later (a splash longer than the script's first 1.5 s: PIGEON's 3 s, whose
// held-off panel would take the taps and turns all at once at the splash's end).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cores.h"
#include "device.h"
#include "ppm.h"
#include "script_runner.h"

extern "C" {
void fw_ref_boot(void);
void fw_ref_boot_from(const uint8_t *bytes, uint32_t n);
void fw_ref_ms(void);
void fw_ref_buttons(uint32_t labels);
void fw_ref_keys(uint32_t mask);
void fw_ref_enc(uint32_t role, int32_t n);
void fw_ref_master(uint32_t v);
const uint16_t *fw_ref_screen(void);
uint32_t fw_ref_now(void);
uint32_t fw_ref_audio_take(int16_t *out, uint32_t max_frames);
}

namespace {

int fails = 0;
void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    fails += !c;
}

void write_ppm(const std::string &path, const uint16_t *p) { fm1g_write_lcd_ppm(path.c_str(), p); }   // (common/ppm.h)

enum { EV_KEYS, EV_BTNS, EV_ENC, EV_CHECK };
struct Ev {
    uint32_t ms;
    int kind;
    uint32_t a;
    int32_t n;
};
constexpr uint32_t B(int label) { return 1u << label; }
constexpr uint32_t K(int key) { return 1u << key; }

// the script (device ms; held masks as they become; a check compares the screens after that ms)
const Ev SCRIPT[] = {
    {1500, EV_CHECK, 0, 0},
    {1600, EV_KEYS, K(9), 0},                         // D4
    {1700, EV_CHECK, 0, 0},
    {1800, EV_KEYS, 0, 0},
    {2000, EV_BTNS, B(FM1P_B_ENV), 0},
    {2080, EV_BTNS, 0, 0},
    {2300, EV_ENC, FM1P_E_K1, 3},
    {2400, EV_ENC, FM1P_E_K2, -2},
    {2600, EV_CHECK, 0, 0},
    {2700, EV_BTNS, B(FM1P_B_HOME), 0},
    {2780, EV_BTNS, 0, 0},
    {3000, EV_ENC, FM1P_E_ALGO, 1},
    {3300, EV_CHECK, 0, 0},
    {3400, EV_KEYS, K(7) | K(11) | K(14), 0},        // C4 E4 G4
    {3500, EV_BTNS, B(FM1P_B_PLAY), 0},
    {3580, EV_BTNS, 0, 0},
    {3900, EV_KEYS, 0, 0},
    {4000, EV_CHECK, 0, 0},
    {4500, EV_BTNS, B(FM1P_B_PLAY), 0},
    {4580, EV_BTNS, 0, 0},
    {4600, EV_ENC, FM1P_E_SELECT, 2},
    {5000, EV_CHECK, 0, 0},
};

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::printf("usage: fw_golden MODULE.fm1core ID [OUT_DIR]\n");
        return 2;
    }
    const std::string module = argv[1], id = argv[2], out = argc > 3 ? argv[3] : ".";
    std::string err;
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
    std::printf("fw_golden %s: %s, against the firmware's own web/emu/felucca_web.c\n", id.c_str(), module.c_str());
    check(lc != nullptr, "the core loads" + (lc ? std::string() : ": " + err));
    if (!lc)
        return 1;
    std::vector<uint8_t> image;
    if (const char *sx = std::getenv("FW_GOLDEN_SYSEX")) {
        std::unique_ptr<LoadedCore> pre = CoreLoader::load(module, &err);
        Script s;
        bool ok = pre && parse_script_text("wait 3200\nsysex " + std::string(sx) + "\nwait 300\nquit\n", sx, s, err);
        if (ok) {
            Device p(pre->core());
            p.boot_from(nullptr, 0);
            ScriptRunner r(p, s);
            r.keep_pcm = false;
            r.run();
            for (const std::string &l : r.log())
                std::printf("    %s\n", l.c_str());
            ok = r.expect_fail() == 0 && r.expect_ok() > 0;
            p.flash_sync();
            image.assign(p.flash(), p.flash() + p.flash_size());
        }
        check(ok, std::string("FW_GOLDEN_SYSEX ") + sx + ": uploaded into a fresh copy; both power on from its image");
    }
    Device d(lc->core());
    if (image.empty()) {
        d.boot_from(nullptr, 0);
        fw_ref_boot();
    } else {
        d.boot_from(image.data(), (uint32_t)image.size());
        fw_ref_boot_from(image.data(), (uint32_t)image.size());
    }

    std::vector<int16_t> pc, pr;                      // the two audio streams
    int16_t buf[2 * 1024];
    const uint32_t delay = std::getenv("FW_GOLDEN_DELAY_MS") ? (uint32_t)std::atoi(std::getenv("FW_GOLDEN_DELAY_MS")) : 0;
    const uint32_t end = SCRIPT[sizeof SCRIPT / sizeof SCRIPT[0] - 1].ms + delay;
    size_t ev = 0;
    int checks = 0, same = 0;
    for (uint32_t ms = 0; ms <= end; ms++) {
        std::vector<const Ev *> chk;
        for (; ev < sizeof SCRIPT / sizeof SCRIPT[0] && SCRIPT[ev].ms + delay == ms; ev++) {
            const Ev &e = SCRIPT[ev];
            switch (e.kind) {
            case EV_KEYS: d.keys(e.a); fw_ref_keys(e.a); break;
            case EV_BTNS: d.buttons(e.a); fw_ref_buttons(e.a); break;
            case EV_ENC: d.enc((int)e.a, e.n); fw_ref_enc(e.a, e.n); break;
            case EV_CHECK: chk.push_back(&e); break;
            default: break;
            }
        }
        d.run_ms();
        fw_ref_ms();
        for (uint32_t n; (n = d.take_s16(buf, 1024)) > 0;)
            pc.insert(pc.end(), buf, buf + 2 * n);
        for (uint32_t n; (n = fw_ref_audio_take(buf, 1024)) > 0;)
            pr.insert(pr.end(), buf, buf + 2 * n);
        for (const Ev *e : chk) {
            (void)e;
            const uint16_t *a = d.lcd(), *b = fw_ref_screen();
            int diff = 0, lit = 0;
            for (int i = 0; i < FM1P_LCD_W * FM1P_LCD_H; i++) {
                diff += a[i] != b[i];
                lit += a[i] != a[0];
            }
            write_ppm(out + "/" + id + "_golden_" + std::to_string(ms) + ".ppm", a);
            checks++;
            same += diff == 0;
            check(diff == 0 && fw_ref_now() == d.ms(),
                  "ms " + std::to_string(ms) + ": the screens are identical (" + std::to_string(diff) +
                      " pixels differ; " + std::to_string(lit) + " not the corner's colour; device ms " +
                      std::to_string(d.ms()) + " / " + std::to_string(fw_ref_now()) + ")");
        }
    }
    // the audio: the core's blocks (the ABI's 128-frame blocks due by each ms) are a prefix of the unit's stream
    const size_t n = std::min(pc.size(), pr.size());
    size_t first = n, peak = 0;
    for (size_t i = 0; i < n; i++) {
        if (pc[i] != pr[i] && first == n)
            first = i;
        peak = std::max<size_t>(peak, (size_t)std::abs((int)pc[i]));
    }
    check(n > 44100u * 4u && first == n && peak > 64,
          "audio: " + std::to_string(n / 2) + " frames compared, " +
              (first == n ? std::string("identical") : "the first difference at frame " + std::to_string(first / 2)) +
              " (peak " + std::to_string(peak) + ")");
    std::printf("fw_golden %s: %s (%d of %d screens identical)\n", id.c_str(), fails ? "FAIL" : "PASS", same, checks);
    return fails ? 1 : 0;
}
