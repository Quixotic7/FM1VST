// SPDX-License-Identifier: GPL-3.0-only
// The Tier 2 parameter map of one core (FM1-VST-PLAN.md 4.5): param_test [MODULE.fm1core] (default: the ChoralRoot
// module), headless, on a Device and the loaded module (no JUCE). For every core: every entry driven to min, max and
// its default through set() and read back with get(); the value texts; the flash image changes for the entries the
// settings record holds (FM1P_PERSIST) and only for those. ChoralRoot: also the CR_TRACE lines a panel turn prints
// (one parameter per group, stdout captured) and the meta entries following the perform mode. Felucca / Melodee:
// the meta entries (HOME knobs, engine entries) following the selected part and its engine, and the texts of the
// firmware's param_format. Prints the map as a table. FM1EMU_HOME points under the build (the per-instance copy of
// the module goes there).
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
    s += f & FM1P_HIDDEN ? 'H' : '-';
    return s;
}

int main(int argc, char **argv)
{
    const std::string module = argc > 1 ? argv[1] : core_module();
    std::string home = FM1_SCRATCH_DIR;
    mkdir(home.c_str(), 0755);
    {
        const std::string base = module.substr(module.rfind('/') + 1);
        home += "/" + base.substr(0, base.find('.'));        // (one folder per core: ctest may run them in turn)
    }
    mkdir(home.c_str(), 0755);
    setenv("FM1EMU_HOME", home.c_str(), 1);
    std::string err;
    std::unique_ptr<LoadedCore> lc = CoreLoader::load(module, &err);
    if (!lc) {
        std::printf("param_test: %s\n", err.c_str());
        return 1;
    }
    const fm1core_t *c = lc->core();
    const bool cr = !std::strcmp(c->id, "choralroot");
    const bool fam = !std::strcmp(c->id, "felucca") || !std::strcmp(c->id, "melodee");   // (the same map layout)
    std::printf("param_test: core %s (%s)\n", c->id, module.c_str());
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
        if (c->param_epoch)
            c->param_epoch();                            // (a meta entry's target and descriptor are the epoch's)
        if ((e->flags & FM1P_META) && e->target && e->target() >= 0)
            e = &P[e->target()];
        lo = e->min, hi = e->max, def = e->def;
    };

    // ---- the table, the names
    std::printf("\n  %-3s %-16s %6s %6s %6s %-6s %s\n", "#", "name", "min", "max", "def", "flag", "path (set)");
    std::set<std::string> names;
    size_t longest = 0;
    uint32_t visible = 0;
    bool fnOk = true;
    for (uint32_t i = 0; i < n; i++) {
        std::printf("  %-3u %-16s %6d %6d %6d %-6s %s\n", i, P[i].name, P[i].min, P[i].max, P[i].def,
                    flags(P[i].flags).c_str(), P[i].path ? P[i].path : "");
        names.insert(P[i].name);
        visible += !(P[i].flags & FM1P_HIDDEN);
        longest = std::max(longest, std::strlen(P[i].name));
        fnOk = fnOk && P[i].get && P[i].set && P[i].text && P[i].min <= P[i].def && P[i].def <= P[i].max &&
               (!(P[i].flags & FM1P_META) == !P[i].target);
    }
    std::printf("  %u parameters, %u with a host slot (the rest FM1P_HIDDEN: knob targets only)\n\n", n, visible);
    // (plugin/Tier2Parameter.h: the opt-in -DFM1_TIER2_SLOTS=79: 8 knobs + 79 + the 41 panel booleans = Live's 128)
    check(visible <= 79, "the visible entries fit the plugin's opt-in 79 Tier 2 slots (" + std::to_string(visible) + ")");
    check(names.size() == n, "the names are unique");
    check(longest <= 16, "no name longer than 16 characters (longest " + std::to_string(longest) + ")");
    check(fnOk, "every entry has get / set / text, min <= def <= max, target exactly on the meta entries");

    // ---- boot
    Device dev(c);
    dev.boot_from(nullptr, 0);
    auto knob = [&](int role) -> int {               // what that knob turns now (after the epoch), -2: no hook
        if (!c->knob_target)
            return -2;
        c->param_epoch();
        return c->knob_target(role);
    };
    auto tname = [&](int t) -> std::string { return t >= 0 && (uint32_t)t < n ? P[t].name : t == -1 ? "(turn)" : "?"; };
    run(dev, 600);
    check(dev.booted() && !dev.halted(), "booted on a fresh flash, 600 ms");
    {   // ---- the physical knobs on the boot screen: every role names an entry or -1 (relative); MASTER -1
        static const char *const ROLE[EMU_NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB1", "KNOB2", "KNOB3", "KNOB4",
                                                 "MASTER"};
        std::string line;
        bool ok = c->knob_target != nullptr;
        for (int r = 0; r < EMU_NE; r++) {
            const int t = knob(r);
            ok = ok && t >= -1 && t < (int)n && (r != EMU_E_MASTER || t == -1);
            line += std::string(r ? ", " : "") + ROLE[r] + " " + tname(t);
        }
        std::printf("        (boot screen: %s)\n", line.c_str());
        check(ok, "knob_target: every role on the boot screen is a map entry or -1 (MASTER -1)");
        uint32_t hidden = 0;
        for (uint32_t i = 0; i < n; i++)
            hidden += (P[i].flags & FM1P_HIDDEN) != 0;
        check(hidden > 0, std::to_string(hidden) + " FM1P_HIDDEN entries (knob targets only; set / get below as every entry)");
    }

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
        for (const char *nm : !cr ? std::initializer_list<const char *>{} : std::initializer_list<const char *>{"Strum Rate", "Arp Gate", "Slop Amount", "Chord Reverb", "Bass Level", "Reverb Size",
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
        if (fam) {
            std::printf("        (texts: Attack 10 \"%s\", Level 104 \"%s\", Tempo 120 \"%s\", Arp Rate 2 \"%s\", LFO Rate 60 "
                        "\"%s\", Env>Filter 32 \"%s\", Part 1 \"%s\", Delay Time 1 \"%s\")\n",
                        show("Attack", 10).c_str(), show("Level", 104).c_str(), show("Tempo", 120).c_str(),
                        show("Arp Rate", 2).c_str(), show("LFO Rate", 60).c_str(), show("Env>Filter", 32).c_str(),
                        show("Part", 1).c_str(), show("Delay Time", 1).c_str());
            check(show("Tempo", 120) == "120 BPM" && show("Arp Rate", 2) == "1/16" && show("Env>Filter", 32) == "+50 %",
                  "param_format's texts: \"120 BPM\", \"1/16\", \"+50 %\"");
        }
        if (cr) {
        std::printf("        (texts: Strum Rate 40 \"%s\", Arp Division 6 \"%s\", Arp Dir 2 \"%s\", Pattern Type 2 \"%s\", "
                    "Chord Level 92 \"%s\", Delay Time 1 \"%s\", Chorus Rate 40 \"%s\", Tempo 120 \"%s\", Voicing -3 \"%s\")\n",
                    show("Strum Rate", 40).c_str(), show("Arp Division", 6).c_str(), show("Arp Dir", 2).c_str(),
                    show("Pattern Type", 2).c_str(), show("Chord Level", 92).c_str(), show("Delay Time", 1).c_str(),
                    show("Chorus Rate", 40).c_str(), show("Tempo", 120).c_str(), show("Voicing", -3).c_str());
        check(show("Arp Division", 6) == "1/8" && show("Strum Rate", 120) == "120 ms" && show("Strum Dir", 0) == "Up",
              "the knob row's texts: \"1/8\", \"120 ms\", \"Up\"");
        }
    }

    // ---- the trace: one parameter per group, the line a panel turn prints
    if (cr) {
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

    // ---- the knobs follow the screen: the view's KNOB 3 is the perform mode's first knob
    if (cr) {
        const int pm = find("Perform Mode"), ad = find("Arp Division"), sr = find("Strum Rate");
        const int v1 = knob(EMU_E_K1), sel = knob(EMU_E_SELECT), pre = knob(EMU_E_PRESETS);
        const uint32_t e0 = c->param_epoch();
        const int t0 = knob(EMU_E_K3);
        P[pm].set(3);                                     // Arpeggiate
        run(dev, 30);
        const uint32_t e1 = c->param_epoch();
        const int t1 = knob(EMU_E_K3);
        check(v1 == find("Voicing") && sel == find("Tempo") && pre == -1 && t0 == sr && t1 == ad && e1 != e0,
              "the view: KNOB1 Voicing, SELECT Tempo, PRESETS (turn); KNOB3 " + tname(t0) + ", then (Perform Mode "
              "Arpeggiate, the epoch moved " + std::to_string(e0) + " -> " + std::to_string(e1) + ") " + tname(t1));
        P[pm].set(4);                                     // Arp 2 Octaves: the same mode
        run(dev, 30);
        check(c->param_epoch() == e1, "Arp 2 Octaves (the same mode): the epoch stays");
        Capture quiet(fwlog + ".4");
        P[pm].set(0);
        run(dev, 30);
        P[find("Perform")].set(0);
        P[find("Arp Range")].set(P[find("Arp Range")].def);   // (Arp 2 Octaves set the range)
        run(dev, 30);
    }
    // ---- Felucca / Melodee: the meta entries follow the selected part and its engine
    if (fam) {
        const int part = find("Part"), e1 = find("E1");   // ("E1": the name before boot)
        int ed1 = -1;
        for (uint32_t i = 0; i < n; i++)
            if (!std::strncmp(P[i].name, "E1 ", 3))
                ed1 = (int)i;
        const uint32_t ep0 = c->param_epoch();
        const std::string name0 = ed1 >= 0 ? P[ed1].name : "?";
        const int t0 = knob(EMU_E_K1);                 // HOME: the engine's first knob
        const std::string tn0 = tname(t0);
        check(part >= 0 && t0 >= 0 && ed1 >= 0 && e1 < 0 && knob(EMU_E_ALGO) == part,
              "Part and the engine entries (\"" + name0 + "\") are in the map; HOME's KNOB1 is " + tn0 +
                  ", ALGORITHM is Part");
        if (part >= 0 && t0 >= 0 && ed1 >= 0) {
            const int32_t lvl0 = P[find("Level")].get();
            P[part].set(1);                                // part 2 (FM6 on a fresh unit)
            run(dev, 30);
            const uint32_t ep1 = c->param_epoch();
            const int t1 = knob(EMU_E_K1);
            const std::string name1 = P[ed1].name, tn1 = t1 >= 0 ? P[t1].name : "?";
            std::printf("        (part 1: E1 \"%s\", K1 -> \"%s\"; part 2: E1 \"%s\" %d..%d, K1 -> \"%s\"; epoch %u -> %u)\n",
                        name0.c_str(), tn0.c_str(), name1.c_str(), P[ed1].min, P[ed1].max, tn1.c_str(), ep0, ep1);
            check(P[part].get() == 1 && ep1 != ep0 && name1 != name0 && t1 >= 0,
                  "Part 2: the epoch moved, the engine entries renamed (\"" + name1 + "\"), HOME's KNOB1 now \"" +
                      tn1 + "\"");
            // the panel's KNOB 1 turns what knob_target names
            const int32_t before = P[t1].get();
            dev.enc(EMU_E_K1, before < P[t1].max ? 1 : -1);
            run(dev, 40);
            check(P[t1].get() != before, "the panel's KNOB1 +-1 on HOME moves " + tn1 + " (" + std::to_string(before) +
                                             " -> " + std::to_string(P[t1].get()) + ")");
            P[t1].set(P[t1].def);
            P[find("Level")].set(lvl0 + 1 <= P[find("Level")].max ? lvl0 + 1 : lvl0 - 1);
            run(dev, 20);
            const int32_t lvl2 = P[find("Level")].get();
            P[part].set(0);
            run(dev, 30);
            check(P[find("Level")].get() == lvl0 && c->param_epoch() != ep1 && std::string(P[ed1].name) == name0,
                  "back on part 1: its own level (part 2's was " + std::to_string(lvl2) + "), the names back");
            P[part].set(1);
            run(dev, 10);
            P[find("Level")].set(lvl0);                    // (part 2's level as it was: the flash test below starts clean)
            P[part].set(0);
            run(dev, 30);
        }
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
            if (cr && !std::strcmp(P[i].name, "Key Tonic"))  // (the tonic's knob turned Key Mode on)
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
