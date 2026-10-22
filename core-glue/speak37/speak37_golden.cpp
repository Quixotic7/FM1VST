// SPDX-License-Identifier: GPL-3.0-only
// The Speak37 core's golden check (core_speak37.cmake): Speak37's own FM-1 host test (tests/sp_screen.c -DT_FM1=1,
// REF) runs its whole check in OUT/ref and writes its FM-1 screens there; the core (MODULE, through the engine's
// Device as the plugin drives it) is powered on from a fresh flash and its screen compared at device ms 500 with the
// test's boot screen (fm1_boot.ppm: sp_boot_frame(500)), pixel for pixel in RGB565. (sp_screen writes a pixel as
// c << 3 / c << 2, common/ppm.h as c * 255 / 31: both are read back to the panel's 5-6-5 values.)
// Left out: the clock line under the title (y 40..63, above the glass at y 64: sp_boot_frame's "AC791N <n> MHZ").
// The device measures its CPU clock at power-on (hal/fm1_time.h fm1_cpu_mhz) and the core gives the FM-1's nominal
// 360 MHz (speak37_hal.h); sp_screen.c never sets sp_mhz, so its boot screen says "AC791N 0 MHZ".
//
//   speak37_golden MODULE REF OUT
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "cores.h"
#include "device.h"
#include "ppm.h"

namespace {

uint16_t rgb565(const uint8_t *px) { return (uint16_t)((px[0] >> 3) << 11 | (px[1] >> 2) << 5 | (px[2] >> 3)); }

// the core's screen against a sp_screen PPM: the pixels that differ (their bounding box in `why`)
constexpr int kClockY0 = 40, kClockY1 = 63;      // the clock line, left out (above)

int compare(const Device &d, const std::string &ref, std::string &why)
{
    std::vector<uint8_t> rgb(FM1G_RGB_BYTES);
    if (!fm1g_read_ppm(ref.c_str(), rgb.data())) {
        why = "cannot read " + ref;
        return -1;
    }
    const uint16_t *p = d.lcd();
    int n = 0, x0 = 999, y0 = 999, x1 = -1, y1 = -1;
    for (int y = 0; y < FM1P_LCD_H; y++)
        for (int x = 0; x < FM1P_LCD_W; x++) {
            if (y >= kClockY0 && y <= kClockY1)
                continue;
            const int i = y * FM1P_LCD_W + x;
            const uint16_t ours = (uint16_t)(p[i] >> 8 | p[i] << 8);   // (big-endian as sent)
            if (ours == rgb565(&rgb[3 * i]))
                continue;
            n++;
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
    why = std::to_string(n) + " pixels differ";
    if (n)
        why += " in (" + std::to_string(x0) + "," + std::to_string(y0) + ")..(" + std::to_string(x1) + "," +
               std::to_string(y1) + ")";
    return n;
}

}   // namespace

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: speak37_golden MODULE REF OUT\n");
        return 2;
    }
    const std::string module = argv[1], ref = argv[2], out = argv[3], rdir = out + "/ref";
    mkdir(out.c_str(), 0755);
    mkdir(rdir.c_str(), 0755);
    const std::string cmd = "'" + ref + "' '" + rdir + "' > '" + out + "/ref.log' 2>&1";
    const int rc = std::system(cmd.c_str());
    std::printf("the reference: %s (%s)\n", rc == 0 ? "speak screen test passed" : "FAILED", (out + "/ref.log").c_str());
    if (rc != 0)
        return 1;

    std::string err;
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
    if (!lc) {
        std::fprintf(stderr, "load: %s\n", err.c_str());
        return 1;
    }
    Device d(lc->core());
    d.boot_from(nullptr, 0);                       // a fresh unit, as the plugin powers one on
    const struct {
        uint32_t ms;
        const char *ref;
    } shots[] = {{500, "fm1_boot.ppm"}};
    int bad = 0;
    for (const auto &s : shots) {
        while (d.ms() < s.ms)
            d.run_ms();
        std::string why;
        const int n = compare(d, rdir + "/" + s.ref, why);
        std::printf("%6u ms: %s against %s: %s\n", (unsigned)s.ms, n ? "DIFFERS" : "same", s.ref, why.c_str());
        if (n) {
            fm1g_write_lcd_ppm((out + "/ours_" + std::to_string(s.ms) + ".ppm").c_str(), d.lcd());
            bad++;
        }
    }
    std::printf("speak37_golden: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
