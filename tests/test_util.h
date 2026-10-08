// SPDX-License-Identifier: GPL-3.0-only
// Shared by the engine tests: paths (from CMake), the reference emulator's WAV and log, a script run on a
// freshly loaded core.
#pragma once
#include <sys/stat.h>
#include <sys/wait.h>

#include <algorithm>
#include <cstring>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cores.h"
#include "device.h"
#include "script_runner.h"

#ifndef FM1_CR_DIR
#error "FM1_CR_DIR: the ChoralRootFM1 submodule (CMake)"
#endif

static inline std::string cr_dir() { return FM1_CR_DIR; }
static inline std::string core_module() { return FM1_CORE_MODULE; }
static inline std::string script_path(const std::string &name) { return cr_dir() + "/tools/emu/scripts/" + name; }

static inline bool read_file(const std::string &path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// emu.c wav_header: RIFF / fmt (PCM, 2 ch, 44100, 16 bit) / data; the int16 interleaved samples
static inline bool read_wav_s16(const std::string &path, std::vector<int16_t> &pcm, std::string &err)
{
    std::string d;
    if (!read_file(path, d)) {
        err = "cannot read " + path;
        return false;
    }
    if (d.size() < 12 || d.compare(0, 4, "RIFF") || d.compare(8, 4, "WAVE")) {
        err = path + ": not a RIFF WAVE";
        return false;
    }
    size_t p = 12;
    uint16_t ch = 0, bits = 0;
    uint32_t rate = 0;
    while (p + 8 <= d.size()) {
        uint32_t len;
        std::memcpy(&len, &d[p + 4], 4);
        const std::string id = d.substr(p, 4);
        if (id == "fmt " && len >= 16) {
            std::memcpy(&ch, &d[p + 10], 2);
            std::memcpy(&rate, &d[p + 12], 4);
            std::memcpy(&bits, &d[p + 22], 2);
        } else if (id == "data") {
            if (ch != 2 || bits != 16 || rate != 44100) {
                err = path + ": not s16 stereo 44100";
                return false;
            }
            const size_t n = std::min<size_t>(len, d.size() - p - 8) / 2;
            pcm.resize(n);
            std::memcpy(pcm.data(), &d[p + 8], n * 2);
            return true;
        }
        p += 8 + len + (len & 1u);
    }
    err = path + ": no data chunk";
    return false;
}

static inline long mtime(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) ? -1 : (long)st.st_mtime;
}

// the reference: build/host/emu --headless --script tools/emu/scripts/NAME --wav OUT/STEM.wav (run from the
// submodule: its scripts write shots under build/emu/test). Re-run when the WAV is missing or older than the
// emulator or the script. Returns the emulator's exit status (0, 1 = an expect failed) or -1.
static inline int ensure_reference(const std::string &name, std::string &wav, std::string &log)
{
    const std::string stem = name.substr(0, name.rfind('.'));
    wav = std::string(FM1_REF_OUT) + "/" + stem + ".wav";
    log = std::string(FM1_REF_OUT) + "/" + stem + ".log";
    const std::string emu = FM1_REF_EMU, scr = script_path(name), status = wav + ".status";
    const long tw = mtime(wav);
    std::string st;
    if (tw >= 0 && tw >= mtime(emu) && tw >= mtime(scr) && read_file(status, st))
        return std::atoi(st.c_str());
    if (mtime(emu) < 0) {
        std::printf("reference: %s missing (cmake --build: target reference_emu)\n", emu.c_str());
        return -1;
    }
    const std::string cmd = "cd '" + cr_dir() + "' && mkdir -p build/emu/test '" + std::string(FM1_REF_OUT) +
                            "' && '" + emu + "' --headless --script 'tools/emu/scripts/" + name + "' --wav '" + wav +
                            "' > '" + log + "' 2>&1";
    std::printf("reference: %s\n", cmd.c_str());
    const int rc = std::system(cmd.c_str());
    const int ex = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
    if (ex == 0 || ex == 1) {
        std::ofstream(status) << ex << "\n";
    }
    return ex == 0 || ex == 1 ? ex : -1;
}

// the reference log's "expect ..." lines (emu.c run_script's wording)
static inline std::vector<std::string> log_expects(const std::string &log)
{
    std::vector<std::string> v;
    std::string text, line;
    if (!read_file(log, text))
        return v;
    std::istringstream in(text);
    while (std::getline(in, line))
        if (line.compare(0, 7, "expect ") == 0)
            v.push_back(line);
    return v;
}

// a run of a script on a freshly loaded core, RAM-only flash, its 44.1 kHz output
struct Run {
    std::vector<int16_t> pcm;
    uint32_t ms = 0;
    int ok = 0, fail = 0;
    std::vector<std::string> log;
};
static inline bool run_script_fresh(const Script &s, Run &out, std::string &err)
{
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(core_module(), &err);
    if (!lc)
        return false;
    Device dev(lc->core());
    dev.boot(nullptr, true);
    ScriptRunner r(dev, s);
    r.run();
    out.pcm = r.pcm();
    out.ms = r.ms_run();
    out.ok = r.expect_ok();
    out.fail = r.expect_fail();
    out.log = r.log();
    return true;
}

// first index where a and b differ over the overlap, or -1
static inline long first_diff(const std::vector<int16_t> &a, const std::vector<int16_t> &b)
{
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return (long)i;
    return -1;
}
