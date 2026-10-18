// SPDX-License-Identifier: GPL-3.0-only
// The golden check of a plat / fm1-base core (core-glue/plat/README.md "Golden checks"): the firmware's own host
// simulator (host/x0x_host.c, host/omni_host.c, base/host/plat_host.c ..., built from the unmodified submodule) and
// the core, loaded as the plugin loads it, run the same input script; at each `shot` the core's screen is compared
// with the PNG the simulator wrote, pixel for pixel.
//
//   plat_golden_test MODULE.fm1core HOSTSIM SCRIPT OUT_DIR [ALLOWED_PIXELS]
//
// SCRIPT is in the simulators' own grammar (the subset they share): wait MS | press BTN | release BTN | tap BTN |
// key K down|up | tapkey K | turn ENC N [MS] | strum A B [MS] | shot FILE.png ('#' comments). BTN: FX SEL ENV LFO
// EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+; K: 0..26, wN (white key N), bN (black key N); ENC: SELECT ALGO
// PRESET K1..K4. The core is driven with each command's timing as the simulator gives it (x0x_host.c main): its
// device ms m is the simulator's now_ms m + 1 (the simulator's first frame is at now_ms 1, the core's at ms 0), so
// both draw the same frames from the same input. ALLOWED_PIXELS (default 0): how many may differ (explained in the
// core's cmake where it is not 0).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cores.h"
#include "ppm.h"
#include "device.h"

namespace {

const char *const BTN[14] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                             "OCT-", "OCT+"};
const char *const ENC[7] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
const int WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
const int BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

int index_of(const char *const *t, int n, const std::string &s)
{
    for (int i = 0; i < n; i++)
        if (s == t[i])
            return i;
    return -1;
}
int key_of(const std::string &s)                   // x0x_host.c key_of
{
    if (!s.empty() && s[0] == 'w')
        return WHITE[std::atoi(s.c_str() + 1) & 15];
    if (!s.empty() && s[0] == 'b')
        return BLACK[std::atoi(s.c_str() + 1) % 11];
    return std::atoi(s.c_str());
}

// the simulators' PNG (x0x_host.c png_write: RGB, 8 bit, stored deflate blocks, filter 0): 240 x 240 x 3 bytes
bool read_png(const std::string &path, std::vector<uint8_t> &rgb)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 8 + 25)
        return false;
    std::vector<uint8_t> z;
    for (size_t p = 8; p + 8 <= b.size();) {
        const uint32_t len = (uint32_t)b[p] << 24 | (uint32_t)b[p + 1] << 16 | (uint32_t)b[p + 2] << 8 | b[p + 3];
        if (p + 12 + len > b.size())
            return false;
        if (!std::memcmp(&b[p + 4], "IDAT", 4))
            z.insert(z.end(), b.begin() + (long)p + 8, b.begin() + (long)p + 8 + len);
        p += 12 + len;
    }
    std::vector<uint8_t> raw;
    size_t q = 2;                                  // (the zlib header)
    for (;;) {
        if (q + 5 > z.size() || (z[q] & 6) != 0)   // stored blocks only
            return false;
        const bool last = z[q] & 1;
        const uint32_t n = z[q + 1] | (uint32_t)z[q + 2] << 8;
        q += 5;
        if (q + n > z.size())
            return false;
        raw.insert(raw.end(), z.begin() + (long)q, z.begin() + (long)q + n);
        q += n;
        if (last)
            break;
    }
    if (raw.size() != 240u * (1u + 240u * 3u))
        return false;
    rgb.clear();
    for (int y = 0; y < 240; y++) {
        if (raw[(size_t)y * 721] != 0)
            return false;
        rgb.insert(rgb.end(), raw.begin() + (long)y * 721 + 1, raw.begin() + (long)y * 721 + 721);
    }
    return true;
}

// the core's screen as the simulator converts its frame buffer (big-endian RGB565 -> RGB888: common/ppm.h)
std::vector<uint8_t> core_rgb(const Device &d)
{
    std::vector<uint8_t> out(FM1G_RGB_BYTES);
    fm1g_lcd_rgb(d.lcd(), out.data());
    return out;
}
void write_ppm(const std::string &path, const std::vector<uint8_t> &rgb) { fm1g_write_ppm(path.c_str(), rgb.data()); }

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 5) {
        std::printf("usage: plat_golden_test MODULE.fm1core HOSTSIM SCRIPT OUT_DIR [ALLOWED_PIXELS]\n");
        return 2;
    }
    const std::string module = argv[1], hostsim = argv[2], script = argv[3], out = argv[4];
    const long allowed = argc > 5 ? std::atol(argv[5]) : 0;
    std::string cmd = "mkdir -p '" + out + "/host' && '" + hostsim + "' '" + script + "' '" + out +
                      "/host' > '" + out + "/host/log.txt' 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        std::printf("FAIL  the host simulator: %s (see %s/host/log.txt)\n", cmd.c_str(), out.c_str());
        return 1;
    }
    std::string err;
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
    if (!lc) {
        std::printf("FAIL  load %s: %s\n", module.c_str(), err.c_str());
        return 1;
    }
    Device d(lc->core());
    d.boot_from(nullptr, 0);
    uint32_t held_btn = 0, held_keys = 0;
    auto run = [&](uint32_t ms) {
        for (uint32_t i = 0; i < ms; i++)
            d.run_ms();
    };
    std::ifstream in(script);
    std::string line;
    int fails = 0, shots = 0, ln = 0;
    while (std::getline(in, line)) {
        ln++;
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ss(line);
        std::string c, a, b, e;
        ss >> c >> a >> b >> e;
        if (c.empty())
            continue;
        if (c == "wait") {
            run((uint32_t)std::atoi(a.c_str()));
        } else if (c == "press" || c == "release" || c == "tap") {
            const int i = index_of(BTN, 14, a);
            if (i < 0) {
                std::printf("FAIL  line %d: no button %s\n", ln, a.c_str());
                return 1;
            }
            if (c[0] != 'r')
                d.buttons(held_btn |= 1u << i);
            if (c[0] == 't')
                run(40);
            if (c[0] != 'p')
                d.buttons(held_btn &= ~(1u << i));
            run(c[0] == 't' ? 40 : 2);
        } else if (c == "key" || c == "tapkey") {
            const int k = key_of(a);
            if (c == "tapkey") {
                d.keys(held_keys |= 1u << k);
                run(60);
                d.keys(held_keys &= ~(1u << k));
                run(30);
            } else {
                d.keys(b == "down" ? (held_keys |= 1u << k) : (held_keys &= ~(1u << k)));
                run(2);
            }
        } else if (c == "turn") {
            const int r = index_of(ENC, 7, a);
            int n = std::atoi(b.c_str());
            const int s = n > 0 ? 1 : -1;
            const uint32_t gap = e.empty() ? 80u : (uint32_t)std::atoi(e.c_str());
            if (r < 0) {
                std::printf("FAIL  line %d: no encoder %s\n", ln, a.c_str());
                return 1;
            }
            for (; n; n -= s) {
                d.enc(r, s);
                run(gap);
            }
        } else if (c == "strum") {
            int x = std::atoi(a.c_str());
            const int y = std::atoi(b.c_str()), s = y >= x ? 1 : -1;
            const uint32_t gap = e.empty() ? 25u : (uint32_t)std::atoi(e.c_str());
            for (;; x += s) {
                d.keys(held_keys |= 1u << WHITE[x & 15]);
                run(gap);
                d.keys(held_keys &= ~(1u << WHITE[x & 15]));
                if (x == y)
                    break;
            }
            run(2);
        } else if (c == "shot") {
            std::vector<uint8_t> want;
            const std::string hp = out + "/host/" + a;
            const std::vector<uint8_t> got = core_rgb(d);
            shots++;
            if (!read_png(hp, want)) {
                std::printf("FAIL  %s: the simulator's screen is not readable\n", hp.c_str());
                fails++;
                continue;
            }
            long diff = 0;
            for (size_t i = 0; i < got.size(); i += 3)
                diff += got[i] != want[i] || got[i + 1] != want[i + 1] || got[i + 2] != want[i + 2];
            const std::string base = out + "/" + a.substr(0, a.rfind('.'));
            write_ppm(base + "_core.ppm", got);
            write_ppm(base + "_host.ppm", want);
            const bool ok = diff <= allowed;
            std::printf("%s  %s at core ms %u: %ld of 57600 pixels differ (allowed %ld)\n", ok ? "ok  " : "FAIL",
                        a.c_str(), (unsigned)d.ms(), diff, allowed);
            fails += !ok;
        } else {
            std::printf("FAIL  line %d: \"%s\" is not in the golden grammar\n", ln, c.c_str());
            return 1;
        }
    }
    if (!shots) {
        std::printf("FAIL  no shot in %s\n", script.c_str());
        return 1;
    }
    std::printf("plat_golden_test %s: %s (%d shots)\n", lc->core()->id, fails ? "FAIL" : "PASS", shots);
    return fails ? 1 : 0;
}
