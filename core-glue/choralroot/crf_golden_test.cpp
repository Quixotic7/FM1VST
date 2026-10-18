// SPDX-License-Identifier: GPL-3.0-only
// The ChoralRoot family's golden check (CORES-BRIEF-PHASE1.md 4): a firmware's own headless emulator (the reference,
// built from the same pinned submodule by its own build script) and the core, driven by the same script, must show
// the same screen at every `shot` of it, pixel for pixel; with --audio their 44.1 kHz output must be the same sample
// for sample.
//
//   crf_golden_test --module M.fm1core --ref EMU --ref-cwd DIR --script S.txt --out DIR [--audio]
//
// The script is the emulator's grammar (tests/script_runner.h). Each `shot X` is rewritten to build/emu/crf/<stem>_<n>.ppm for
// the reference; the core's LCD at the same moment (emu.c: a shot at ms T runs after tick(T), before frame(T): the
// screen as ms T-1 left it) is converted as emu_img.c does (RGB565 big-endian -> RGB888) and compared. The
// script's `expect led` lines (ChoralRoot 0.25.1's four levels too) are checked by both runs. The environment: EMU_C1=thread is set before the core is loaded, and the process must have no more threads
// after the run than before it (common/env.h: the core reads no environment).
#include <mach/mach.h>
#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cores.h"
#include "device.h"
#include "script_runner.h"

namespace {

int fails = 0;
void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    fails += !c;
}

bool read_file(const std::string &path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int thread_count()
{
    thread_act_array_t th;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &th, &n) != KERN_SUCCESS)
        return -1;
    for (mach_msg_type_number_t i = 0; i < n; i++)
        mach_port_deallocate(mach_task_self(), th[i]);
    vm_deallocate(mach_task_self(), (vm_address_t)th, n * sizeof th[0]);
    return (int)n;
}

// emu.c wav_header's RIFF: the int16 interleaved samples of its data chunk
bool read_wav(const std::string &path, std::vector<int16_t> &pcm)
{
    std::string d;
    if (!read_file(path, d) || d.size() < 44 || d.compare(0, 4, "RIFF"))
        return false;
    size_t p = 12;
    while (p + 8 <= d.size()) {
        uint32_t len;
        std::memcpy(&len, &d[p + 4], 4);
        if (!d.compare(p, 4, "data")) {
            const size_t n = std::min<size_t>(len, d.size() - p - 8) / 2;
            pcm.resize(n);
            std::memcpy(pcm.data(), &d[p + 8], n * 2);
            return true;
        }
        p += 8 + len + (len & 1u);
    }
    return false;
}

void rgb_of(uint16_t be, uint8_t *p)             // emu_img.c rgb_of
{
    const uint16_t c = (uint16_t)((be >> 8) | (be << 8));
    const uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    p[0] = (uint8_t)((r << 3) | (r >> 2));
    p[1] = (uint8_t)((g << 2) | (g >> 4));
    p[2] = (uint8_t)((b << 3) | (b >> 2));
}

struct Shot {
    int line;
    uint32_t ms = 0;
    std::string path;
};

}  // namespace

int main(int argc, char **argv)
{
    std::string module, ref, ref_cwd, script, out;
    bool audio = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--module") module = next();
        else if (a == "--ref") ref = next();
        else if (a == "--ref-cwd") ref_cwd = next();
        else if (a == "--script") script = next();
        else if (a == "--out") out = next();
        else if (a == "--audio") audio = true;
    }
    if (module.empty() || ref.empty() || script.empty() || out.empty()) {
        std::printf("usage: crf_golden_test --module M --ref EMU --ref-cwd DIR --script S --out DIR [--audio]\n");
        return 2;
    }
    std::string stem = script.substr(script.rfind('/') + 1);
    stem = stem.substr(0, stem.rfind('.'));
    std::printf("crf_golden_test %s: %s against %s\n", stem.c_str(), module.c_str(), ref.c_str());

    // ---- the script: the reference's (shots into OUT), the core's
    std::string text;
    if (!read_file(script, text)) {
        std::printf("cannot read %s\n", script.c_str());
        return 1;
    }
    std::vector<Shot> shots;
    std::ostringstream rs, cs;
    {
        std::istringstream in(text);
        std::string line;
        int ln = 0;
        while (std::getline(in, line)) {
            ln++;
            char w[4][96] = {};
            std::string body = line.substr(0, line.find(" #") == std::string::npos ? line.size() : line.find(" #"));
            const int nw = std::sscanf(body.c_str(), "%95s %95s %95s %95s", w[0], w[1], w[2], w[3]);
            if (nw >= 1 && !std::strcmp(w[0], "shot")) {
                Shot s;
                s.line = ln;
                /* (emu.c reads a script word into 96 bytes: the reference writes into its own build/emu, gitignored
                 * in every tree, by a short relative path) */
                const std::string rel = "build/emu/crf/" + stem + "_" + std::to_string(shots.size()) + ".ppm";
                s.path = ref_cwd + "/" + rel;
                shots.push_back(s);
                rs << "shot " << rel << "\n";
                cs << line << "\n";
                continue;
            }
            rs << line << "\n";
            cs << line << "\n";
        }
    }
    const std::string ref_script = out + "/" + stem + ".ref.txt", ref_wav = out + "/" + stem + ".ref.wav",
                      ref_log = out + "/" + stem + ".ref.log";
    std::ofstream(ref_script) << rs.str();
    Script s;
    std::string err;
    if (!parse_script_text(cs.str(), script, s, err)) {
        std::printf("%s", err.c_str());
        return 1;
    }
    // the ms of each shot: the event parsed from its line (a shot is kept as an ignored event)
    for (const ScriptEvent &e : s.ev)
        for (Shot &sh : shots)
            if (sh.line == e.line)
                sh.ms = e.ms;

    // ---- the reference
    {
        std::string cmd = "cd '" + ref_cwd + "' && mkdir -p build/emu/crf && '" + ref + "' --headless --script '" +
                          ref_script + "'" + (audio ? " --wav '" + ref_wav + "'" : std::string()) + " > '" + ref_log +
                          "' 2>&1";
        std::printf("reference: %s\n", cmd.c_str());
        const int rc = std::system(cmd.c_str());
        const int ex = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
        check(ex == 0, "the reference ran its script (exit " + std::to_string(ex) + ", log " + ref_log + ")");
    }

    // ---- the core, the environment's test switch set
    setenv("EMU_C1", "thread", 1);
    const int th0 = thread_count();
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
    if (!lc) {
        std::printf("load: %s\n", err.c_str());
        return 1;
    }
    std::vector<int16_t> pcm;
    {
        Device dev(lc->core());
        dev.boot(nullptr, true);
        ScriptRunner r(dev, s);
        const fm1panel_t *p = lc->core()->panel;
        while (!r.done()) {
            const uint32_t m = r.ms_run();
            for (const Shot &sh : shots) {
                if (sh.ms != m)
                    continue;
                std::string ppm;
                const bool have = read_file(sh.path, ppm);
                size_t diff = 0, first = (size_t)-1;
                const char *hdr = "P6\n240 240\n255\n";
                if (have && ppm.size() == std::strlen(hdr) + 240u * 240u * 3u && !ppm.compare(0, std::strlen(hdr), hdr)) {
                    const uint8_t *q = (const uint8_t *)ppm.data() + std::strlen(hdr);
                    for (size_t i = 0; i < 240u * 240u; i++) {
                        uint8_t c[3];
                        rgb_of(p->lcd[i], c);
                        if (std::memcmp(c, q + 3 * i, 3)) {
                            if (!diff)
                                first = i;
                            diff++;
                        }
                    }
                } else {
                    diff = (size_t)-1;
                }
                // the core's own picture beside the reference's
                std::string mine = out + "/" + sh.path.substr(sh.path.rfind('/') + 1);
                mine = mine.substr(0, mine.size() - 4) + ".core.ppm";
                if (FILE *f = std::fopen(mine.c_str(), "wb")) {
                    std::fprintf(f, "P6\n240 240\n255\n");
                    for (size_t i = 0; i < 240u * 240u; i++) {
                        uint8_t c[3];
                        rgb_of(p->lcd[i], c);
                        std::fwrite(c, 1, 3, f);
                    }
                    std::fclose(f);
                }
                check(diff == 0, "shot at " + std::to_string(m) + " ms (line " + std::to_string(sh.line) + "): " +
                                     (diff == (size_t)-1 ? std::string("no reference picture ") + sh.path
                                      : diff ? std::to_string(diff) + " pixels differ, the first at (" +
                                                   std::to_string(first % 240) + ", " + std::to_string(first / 240) + ")"
                                             : std::string("the same 57600 pixels")));
            }
            r.step();
        }
        pcm = r.pcm();
        check(r.expect_fail() == 0, std::to_string(r.expect_ok()) + " expects ok, " + std::to_string(r.expect_fail()) +
                                        " failed (the core's run)");
    }
    const int th1 = thread_count();
    check(th1 == th0, "EMU_C1=thread in the environment: no thread started (" + std::to_string(th0) + " before, " +
                          std::to_string(th1) + " after the run)");

    if (audio) {
        std::vector<int16_t> rw;
        if (!read_wav(ref_wav, rw)) {
            check(false, "the reference's WAV " + ref_wav);
        } else {
            const size_t n = std::min(rw.size(), pcm.size());
            size_t d = 0, first = (size_t)-1;
            for (size_t i = 0; i < n; i++)
                if (rw[i] != pcm[i] && !d++)
                    first = i;
            check(d == 0 && rw.size() == pcm.size(),
                  "audio: " + std::to_string(pcm.size()) + " samples (reference " + std::to_string(rw.size()) + "), " +
                      (d ? std::to_string(d) + " differ, the first at " + std::to_string(first) : "bit-exact"));
        }
    }
    std::printf("crf_golden_test %s: %s\n", stem.c_str(), fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
