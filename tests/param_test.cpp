// SPDX-License-Identifier: GPL-3.0-only
// The Tier 2 parameter map of the ChoralRoot core (FM1-VST-PLAN.md 4.5), headless, on a Device and the loaded module
// (no JUCE): every entry driven to min, max and its default through set() and read back with get(); the value texts;
// the CR_TRACE lines a panel turn prints (one parameter per group, stdout captured); the flash image changes for the
// entries the settings record holds (FM1P_PERSIST) and only for those; the meta entries follow the perform mode.
// Prints the map as a table. FM1EMU_HOME points under the build (the per-instance copy of the module goes there).
#include <fcntl.h>
#include <unistd.h>

#include <set>

#include "test_util.h"

static int fails = 0;
static void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    fails += !c;
}

// stdout (the firmware's CR_TRACE printf) into a file while it lives; text() gives what was printed
struct Capture {
    std::string path;
    int saved = -1;
    explicit Capture(const std::string &p) : path(p)
    {
        std::fflush(stdout);
        saved = dup(1);
        const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        dup2(fd, 1);
        close(fd);
    }
    std::string text()
    {
        std::fflush(stdout);
        if (saved >= 0) {
            dup2(saved, 1);
            close(saved);
            saved = -1;
        }
        std::string s;
        read_file(path, s);
        return s;
    }
    ~Capture() { text(); }
};

static void run(Device &d, int ms)
{
    for (int i = 0; i < ms; i++)
        d.run_ms();
    int16_t buf[1024];
    while (d.take_s16(buf, 512))
        ;
}

static std::string txt(const fm1param_t &p, int32_t v)
{
    char b[64];
    b[0] = 0;
    if (p.text)
        p.text(v, b, sizeof b);
    return b;
}

static std::string flags(uint32_t f)
{
    std::string s;
    s += f & FM1P_META ? 'M' : '-';
    s += f & FM1P_ENUM ? 'E' : '-';
    s += f & FM1P_PERSIST ? 'P' : '-';
    s += f & FM1P_SOUND ? 'S' : '-';
    s += f & FM1P_EDITOR ? 'X' : '-';
    return s;
}

int main()
{
    const std::string home = FM1_SCRATCH_DIR;
    mkdir(home.c_str(), 0755);
    setenv("FM1EMU_HOME", home.c_str(), 1);
    std::string err;
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(core_module(), &err);
    if (!lc) {
        std::printf("param_test: %s\n", err.c_str());
        return 1;
    }
    const fm1core_t *c = lc->core();
    const fm1param_t *P = c->params;
    const uint32_t n = c->nparams;
    check(P && n > 0 && c->param_epoch, "the descriptor has a map (" + std::to_string(n) + " entries) and param_epoch");
    if (!P || !n)
        return 1;
    auto find = [&](const char *name) -> int {
        for (uint32_t i = 0; i < n; i++)
            if (!std::strcmp(P[i].name, name))
                return (int)i;
        return -1;
    };
    auto range = [&](uint32_t i, int32_t &lo, int32_t &hi, int32_t &def) {   // a meta entry: its target's
        const fm1param_t *e = &P[i];
        if ((e->flags & FM1P_META) && e->target && e->target() >= 0)
            e = &P[e->target()];
        lo = e->min, hi = e->max, def = e->def;
    };

    // ---- the table, the names
    std::printf("\n  %-3s %-16s %6s %6s %6s %-5s %s\n", "#", "name", "min", "max", "def", "flag", "path (set)");
    std::set<std::string> names;
    size_t longest = 0;
    bool fnOk = true;
    for (uint32_t i = 0; i < n; i++) {
        std::printf("  %-3u %-16s %6d %6d %6d %-5s %s\n", i, P[i].name, P[i].min, P[i].max, P[i].def,
                    flags(P[i].flags).c_str(), P[i].path ? P[i].path : "");
        names.insert(P[i].name);
        longest = std::max(longest, std::strlen(P[i].name));
        fnOk = fnOk && P[i].get && P[i].set && P[i].text && P[i].min <= P[i].def && P[i].def <= P[i].max &&
               (!(P[i].flags & FM1P_META) == !P[i].target);
    }
    std::printf("  %u parameters (FM1_EXPOSE_EDITOR %s)\n\n", n, n > 80 ? "on" : "off");
    check(names.size() == n, "the names are unique");
    check(longest <= 16, "no name longer than 16 characters (longest " + std::to_string(longest) + ")");
    check(fnOk, "every entry has get / set / text, min <= def <= max, target exactly on the meta entries");

    // ---- boot
    Device dev(c);
    dev.boot_from(nullptr, 0);
    run(dev, 600);
    check(dev.booted() && !dev.halted(), "booted on a fresh flash, 600 ms");

    // ---- the defaults are what the firmware powers on with
    {
        std::string off;
        for (uint32_t i = 0; i < n; i++) {
            int32_t lo, hi, def;
            range(i, lo, hi, def);
            if (P[i].get() != def && !(P[i].flags & FM1P_SOUND))
                off += std::string(" ") + P[i].name + "=" + std::to_string(P[i].get()) + "(def " + std::to_string(def) + ")";
        }
        check(off.empty(), "a fresh unit reads its defaults (FM1P_SOUND entries: the default sound's)" + (off.empty() ? std::string() : ":" + off));
    }

    // ---- min, max, def (the firmware's trace lines go to a log, not the test's output)
    const std::string fwlog = home + "/firmware.log";
    {
        std::string bad;
        Capture quiet(fwlog);
        for (uint32_t i = 0; i < n; i++) {
            int32_t lo, hi, def;
            range(i, lo, hi, def);
            for (int32_t v : {lo, hi, def}) {
                P[i].set(v);
                const int32_t now = P[i].get();          // (at once: the queued engine events counted)
                run(dev, 50);
                const int32_t later = P[i].get();
                if (now != v || later != v)
                    bad += std::string(" ") + P[i].name + ":" + std::to_string(v) + "->" + std::to_string(now) + "/" +
                           std::to_string(later);
            }
        }
        quiet.text();
        check(bad.empty(), "every entry: set(min / max / def), get() equals it at once and 50 ms later" +
                               (bad.empty() ? std::string() : ":" + bad));
        Capture quiet2(fwlog + ".2");
        // values in between (not on a knob's step grid): one detent lands on them exactly
        bad.clear();
        for (const char *nm : {"Strum Rate", "Arp Gate", "Slop Amount", "Chord Reverb", "Bass Level", "Reverb Size",
                               "Click Level", "Tempo", "Voicing", "Transpose", "Key Tonic"}) {
            const int i = find(nm);
            if (i < 0) {
                bad += std::string(" ") + nm + " missing";
                continue;
            }
            const int32_t v = P[i].min + (P[i].max - P[i].min) * 3 / 7 + 1;
            P[i].set(v);
            run(dev, 20);
            if (P[i].get() != v)
                bad += std::string(" ") + nm + ":" + std::to_string(v) + "->" + std::to_string(P[i].get());
            P[i].set(P[i].def);
            run(dev, 20);
        }
        quiet2.text();
        check(bad.empty(), "off-grid targets land exactly (rate 7 of steps of 5 ...)" + (bad.empty() ? std::string() : ":" + bad));
    }

    // ---- the texts
    {
        std::string bad;
        for (uint32_t i = 0; i < n; i++) {
            if (!(P[i].flags & FM1P_ENUM))
                continue;
            std::set<std::string> seen;
            for (int32_t v = P[i].min; v <= P[i].max; v++) {
                const std::string t = txt(P[i], v);
                if (t.empty() || !seen.insert(t).second)
                    bad += std::string(" ") + P[i].name + "[" + std::to_string(v) + "]=\"" + t + "\"";
            }
        }
        check(bad.empty(), "enum entries: text() of every value non-empty and distinct" + (bad.empty() ? std::string() : ":" + bad));
        bad.clear();
        for (uint32_t i = 0; i < n; i++)
            if (txt(P[i], P[i].min).empty() || txt(P[i], P[i].max).empty())
                bad += std::string(" ") + P[i].name;
        check(bad.empty(), "every entry has a text at min and max" + (bad.empty() ? std::string() : ":" + bad));
        auto show = [&](const char *nm, int32_t v) {
            const int i = find(nm);
            return i < 0 ? std::string("?") : txt(P[i], v);
        };
        std::printf("        (texts: Strum Rate 40 \"%s\", Arp Division 6 \"%s\", Arp Dir 2 \"%s\", Pattern Type 2 \"%s\", "
                    "Chord Level 92 \"%s\", Delay Time 1 \"%s\", Chorus Rate 40 \"%s\", Tempo 120 \"%s\", Voicing -3 \"%s\")\n",
                    show("Strum Rate", 40).c_str(), show("Arp Division", 6).c_str(), show("Arp Dir", 2).c_str(),
                    show("Pattern Type", 2).c_str(), show("Chord Level", 92).c_str(), show("Delay Time", 1).c_str(),
                    show("Chorus Rate", 40).c_str(), show("Tempo", 120).c_str(), show("Voicing", -3).c_str());
        check(show("Arp Division", 6) == "1/8" && show("Strum Rate", 120) == "120 ms" && show("Strum Dir", 0) == "Up",
              "the knob row's texts: \"1/8\", \"120 ms\", \"Up\"");
    }

    // ---- the trace: one parameter per group, the line a panel turn prints
    {
        struct T { const char *name; int32_t v; const char *line; } cases[] = {
            {"Strum Rate", 7, "perf: knob Strum rate 7"},          // the current mode, KNOB 1
            {"Arp Division", 3, "perf: knob Arp division 3"},      // another mode: switched for the call
            {"Pattern Rotate", 2, "perf: knob Pattern rotate 2"},  // on no knob: cu_param_turn
            {"Transpose", 5, "key: knob 3 transpose 5"},
            {"Key Tonic", 2, "key: knob 1 tonic D"},
            {"Bass Mode", 2, "bass: behaviour Bass Single Notes"},
            {"Bass Level", 50, "bass: knob 4 level 50"},
            {"Bass Register", 1, "bass: knob 2 register +1"},
            {"Loop Quantize", 3, "loop: knob 2 quantize 1/8T"},
            {"Click Level", 35, "metro: knob 1 click 35"},
            {"Chord Reverb", 64, "fx: knob 4 Reverb amount 64"},
            {"Reverb Size", 33, "fx: knob 1 Reverb size 33"},
            {"Delay Time", 3, "fx: knob 1 Delay time 3"},
            {"Chord Level", 77, "level: part 0 77"},
            {"Bass Reverb", 20, "send: part 1 Reverb 20"},
            {"Bass", 1, "bass: on"},
        };
        const std::string cap = home + "/trace.txt";
        for (const T &t : cases) {
            const int i = find(t.name);
            if (i < 0) {
                check(false, std::string("trace: ") + t.name + " is in the map");
                continue;
            }
            std::string out;
            {
                Capture cp(cap);
                P[i].set(t.v);
                run(dev, 30);
                out = cp.text();
            }
            const bool hit = out.find(t.line) != std::string::npos;
            std::string first = out.substr(0, out.find('\n'));
            check(hit && P[i].get() == t.v, std::string("trace: ") + t.name + " " + std::to_string(t.v) + " prints \"" +
                                                t.line + "\"" + (hit ? "" : " (got \"" + first + "\")"));
            P[i].set(P[i].def);
            run(dev, 30);
        }
    }

    // ---- the meta entries follow the perform mode
    {
        const int pm = find("Perform Mode"), k1 = find("Perf Knob 1"), ad = find("Arp Division"), sr = find("Strum Rate");
        const uint32_t e0 = c->param_epoch();
        const int t0 = P[k1].target();
        P[pm].set(3);                                     // Arpeggiate
        run(dev, 30);
        const uint32_t e1 = c->param_epoch();
        const int t1 = P[k1].target();
        P[ad].set(8);
        run(dev, 20);
        const int32_t viaMeta = P[k1].get();
        P[k1].set(4);
        run(dev, 20);
        const int32_t viaEntry = P[ad].get();
        check(t0 == sr && t1 == ad && e1 != e0 && viaMeta == 8 && viaEntry == 4,
              "Perf Knob 1: Strum Rate, then (Perform Mode Arpeggiate, the epoch moved " + std::to_string(e0) + " -> " +
                  std::to_string(e1) + ") Arp Division; get / set act on it");
        P[pm].set(4);                                     // Arp 2 Octaves: the same mode
        run(dev, 30);
        check(c->param_epoch() == e1, "Arp 2 Octaves (the same mode): the epoch stays");
        Capture quiet(fwlog + ".4");
        P[ad].set(P[ad].def);
        P[pm].set(0);
        run(dev, 30);
        P[find("Perform")].set(0);
        P[find("Arp Range")].set(P[find("Arp Range")].def);   // (Arp 2 Octaves set the range)
        run(dev, 30);
    }
    std::printf("        (the firmware's own lines: %s*)\n", fwlog.c_str());

    // ---- the flash: a change reaches the image exactly for the FM1P_PERSIST entries
    {
        auto image = [&]() { return std::vector<uint8_t>(dev.flash(), dev.flash() + dev.flash_size()); };
        auto sync = [&]() {
            dev.flash_sync();
            run(dev, 60);
        };
        Capture quiet(fwlog + ".3");
        sync();
        std::string wrongP, wrongN;
        int persisted = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (P[i].flags & FM1P_META)
                continue;                                  // (its target's entry is tested)
            const std::vector<uint8_t> before = image();
            const int32_t cur = P[i].get(), v = cur == P[i].min ? P[i].max : P[i].min;
            P[i].set(v);
            run(dev, 30);
            sync();
            const bool changed = image() != before;
            const bool want = (P[i].flags & FM1P_PERSIST) != 0;
            persisted += changed;
            if (changed != want)
                (want ? wrongP : wrongN) += std::string(" ") + P[i].name;
            P[i].set(cur);
            if (!std::strcmp(P[i].name, "Key Tonic"))       // (the tonic's knob turned Key Mode on)
                P[find("Key Mode")].set(0);
            run(dev, 30);
            sync();
        }
        quiet.text();
        check(wrongP.empty(), "every FM1P_PERSIST entry changes the flash image after flash_sync (" +
                                  std::to_string(persisted) + " did)" + (wrongP.empty() ? "" : "; not:" + wrongP));
        check(wrongN.empty(), "no other entry does" + (wrongN.empty() ? std::string() : ":" + wrongN));
    }

    std::printf("param_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
