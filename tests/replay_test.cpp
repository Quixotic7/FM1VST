// SPDX-License-Identifier: GPL-3.0-only
// The replay test (FM1-VST-PLAN.md phase 1): each script of the emulator's headless checks, run through a Device on
// the ChoralRoot module loaded by CoreLoader, must give the reference emulator's samples bit for bit
// (build/host/emu --headless --script ... --wav ...) and the same "expect" results; and the same run rendered at
// 48 kHz through Device::render must match the 44.1 kHz stream resampled independently (a windowed sinc) within a
// tolerance meant to catch dropouts, a wrong rate or a misplaced block, not to grade the interpolator.
#include <cmath>
#include <cstring>

#include "test_util.h"

static const char *const SCRIPTS[] = {"cr_dmaj.txt", "cr_allsynth.txt"};
static const double HOST_RATE = 48000.0;
static const double TOL_MAX_REL = 0.25;   // max |difference| / the signal's peak
static const double TOL_REL_DB = -30.0;   // rms(difference) / rms(signal), the whole run
static const double TOL_WIN_DB = -20.0;   // the same in every 10 ms window with signal in it (a dropout, a click)

// the 44.1 kHz stream at position p (input samples), Blackman-windowed sinc, 64 taps (no band limiting needed:
// 48 > 44.1 kHz)
static double sinc_at(const std::vector<int16_t> &pcm, size_t frames, int ch, double p)
{
    const int HALF = 32;
    const long i0 = (long)std::floor(p);
    double acc = 0;
    for (long i = i0 - HALF + 1; i <= i0 + HALF; i++) {
        if (i < 0 || (size_t)i >= frames)
            continue;
        const double x = p - (double)i;
        const double s = std::fabs(x) < 1e-12 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
        const double u = (x + HALF) / (2.0 * HALF);    // 0..1 across the window
        const double w = 0.42 - 0.5 * std::cos(2 * M_PI * u) + 0.08 * std::cos(4 * M_PI * u);
        acc += pcm[2 * (size_t)i + ch] * s * w;
    }
    return acc / 32768.0;
}

static bool test_script(const char *name)
{
    std::string err, wav, log;
    Script s;
    if (!load_script(script_path(name), s, err)) {
        std::printf("%s", err.c_str());
        return false;
    }
    const int ref_rc = ensure_reference(name, wav, log);
    std::vector<int16_t> ref;
    if (ref_rc < 0 || !read_wav_s16(wav, ref, err)) {
        std::printf("replay %s: no reference (%s)\n", name, err.c_str());
        return false;
    }
    Run run;
    if (!run_script_fresh(s, run, err)) {
        std::printf("replay %s: %s\n", name, err.c_str());
        return false;
    }
    bool ok = true;
    // ---- 44.1 kHz: bit for bit
    const size_t n = std::min(run.pcm.size(), ref.size());
    const long d = first_diff(run.pcm, ref);
    if (d >= 0) {
        ok = false;
        const long f = d / 2;
        std::printf("replay %s: FIRST DIFFERENCE at sample %ld (frame %ld, %.3f ms):\n", name, d, f, f * 1000.0 / 44100);
        for (long i = std::max(0L, d - 8); i < std::min((long)n, d + 16); i++)
            std::printf("  [%ld] engine %6d  reference %6d%s\n", i, run.pcm[i], ref[i], run.pcm[i] != ref[i] ? "  <" : "");
    }
    if (run.pcm.size() != ref.size()) {
        ok = false;
        std::printf("replay %s: LENGTH engine %zu reference %zu samples\n", name, run.pcm.size(), ref.size());
    }
    // ---- the expects: the same lines as the reference printed, all ok
    const std::vector<std::string> rex = log_expects(log);
    if (rex != run.log) {
        ok = false;
        std::printf("replay %s: the expect lines differ from the reference's:\n", name);
        for (size_t i = 0; i < std::max(rex.size(), run.log.size()); i++)
            std::printf("  ref    %s\n  engine %s\n", i < rex.size() ? rex[i].c_str() : "-",
                        i < run.log.size() ? run.log[i].c_str() : "-");
    }
    if (run.fail || ref_rc != 0)
        ok = false;
    uint64_t nz = 0;
    for (int16_t v : run.pcm)
        nz += v != 0;
    std::printf("replay %s: %u ms run, %zu samples compared (%zu frames, %llu non-zero), %s; expects %d ok %d failed "
                "(reference exit %d)\n",
                name, run.ms, n, n / 2, (unsigned long long)nz, d < 0 && run.pcm.size() == ref.size() ? "bit-exact" : "DIFFERENT",
                run.ok, run.fail, ref_rc);

    // ---- 48 kHz through render, the script riding on render's clock, uneven host blocks
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(core_module(), &err);
    if (!lc) {
        std::printf("replay %s: %s\n", name, err.c_str());
        return false;
    }
    Device dev(lc->core());
    dev.boot(nullptr, true);
    ScriptRunner r(dev, s);
    r.attach();
    const size_t ref_frames = ref.size() / 2;
    const double step = 44100.0 / HOST_RATE;
    const size_t host_frames = (size_t)((double)(ref_frames - 40) / step);   // (the sinc's right half inside)
    static const uint32_t BLOCKS[] = {64, 480, 1000, 37, 512, 128, 1, 2048};
    std::vector<float> L(4096), R(4096);
    std::vector<float> outL, outR;
    outL.reserve(host_frames + 4096);
    outR.reserve(host_frames + 4096);
    double ahead_max = 0;
    for (size_t bi = 0; outL.size() < host_frames; bi++) {
        const uint32_t b = BLOCKS[bi % (sizeof BLOCKS / sizeof BLOCKS[0])];
        dev.render(L.data(), R.data(), b, HOST_RATE);
        outL.insert(outL.end(), L.begin(), L.begin() + b);
        outR.insert(outR.end(), R.begin(), R.begin() + b);
        // how far the device ran ahead of what the host has been given, in host frames
        const double ahead = ((double)dev.frames_produced() - (double)outL.size() * step) / step;
        if (ahead > ahead_max)
            ahead_max = ahead;
    }
    double dmax = 0, se = 0, ss = 0, peak = 0, win_worst = -300, win_at = 0;
    size_t at = 0;
    const size_t WIN = 480;                       // 10 ms at 48 kHz
    double wse = 0, wss = 0;
    for (size_t k = 0; k < host_frames; k++) {
        const double p = (double)k * step;
        for (int ch = 0; ch < 2; ch++) {
            const double want = sinc_at(ref, ref_frames, ch, p), got = ch ? outR[k] : outL[k];
            const double e = std::fabs(got - want);
            if (e > dmax) {
                dmax = e;
                at = k;
            }
            peak = std::max(peak, std::fabs(want));
            se += e * e;
            ss += want * want;
            wse += e * e;
            wss += want * want;
        }
        if ((k + 1) % WIN == 0) {
            if (wss / (2.0 * WIN) > 1e-7) {           // (a window with signal: rms above -70 dBFS)
                const double db = 10 * std::log10((wse + 1e-30) / wss);
                if (db > win_worst) {
                    win_worst = db;
                    win_at = (double)(k + 1 - WIN) / HOST_RATE;
                }
            }
            wse = wss = 0;
        }
    }
    const double rel_db = ss > 0 ? 10 * std::log10(se / ss) : -300;
    const uint32_t lat = Device::latency_frames(HOST_RATE);
    const bool ok48 = peak > 0 && dmax <= TOL_MAX_REL * peak && rel_db <= TOL_REL_DB && win_worst <= TOL_WIN_DB &&
                      ahead_max <= lat;
    std::printf("replay %s @ 48 kHz: %zu host frames vs a windowed-sinc resampling of the reference (peak %.4f): "
                "max |diff| %.5f = %.1f %% of the peak (at %.3f s; tolerance %.0f %%), rms diff %.1f dB re signal "
                "(tolerance %.0f dB), worst 10 ms window %.1f dB (at %.2f s; tolerance %.0f dB); render ran the "
                "device at most %.1f host frames ahead (reported latency %u): %s\n",
                name, host_frames, peak, dmax, 100 * dmax / peak, at / HOST_RATE, 100 * TOL_MAX_REL, rel_db,
                TOL_REL_DB, win_worst, win_at, TOL_WIN_DB, ahead_max, lat, ok48 ? "ok" : "FAILED");
    return ok && ok48;
}

int main()
{
    int fails = 0;
    for (const char *name : SCRIPTS)
        fails += !test_script(name);
    std::printf("replay_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
