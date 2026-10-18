// SPDX-License-Identifier: GPL-3.0-only
// A core's conformance file (core-glue/<family>/conformance_<id>.txt or conformance.txt; the directives are listed in
// tests/core_conformance.cpp), read by core_conformance and core_smoke (its `sound` prelude, settle_ms, key, hold).
#pragma once
#include <strings.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fm1core.h"

// A comment starts at a '#' at the line's start or after whitespace, or at "//": so a sharp in a note name
// (key F#3, key note:A#3) is not one.
inline std::string strip_comment(const std::string &line)
{
    for (size_t i = 0; i < line.size(); i++) {
        const bool lead = i == 0 || std::isspace((unsigned char)line[i - 1]);
        if ((line[i] == '#' && lead) || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/'))
            return line.substr(0, i);
    }
    return line;
}

struct Conf {
    std::string path;
    bool found = false;
    uint32_t boot_ms = 3000, settle_ms = 1000, hold_ms = 500;
    int key = 9;                                   // D4
    std::string sound, save, saved, reboot;        // script texts
    std::string saved_param;
    int32_t saved_value = 0;
    bool has_saved_param = false, flash_none = false, reboot_none = false, saved_lcd_differs = false;
    std::string flash_none_why, reboot_none_why;
    int guard_fail = -1;
    std::string guard_reason;
    std::vector<std::string> errors;
};

inline std::string note_name(int key)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int m = FM1P_NOTE0 + key;
    return std::string(N[m % 12]) + std::to_string(m / 12 - 1);
}

inline Conf read_conf(const std::string &path)
{
    Conf c;
    c.path = path;
    std::ifstream in(path);
    if (!in)
        return c;
    c.found = true;
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ln++;
        line = strip_comment(line);
        std::istringstream ss(line);
        std::string d;
        if (!(ss >> d))
            continue;
        std::string rest;
        std::getline(ss, rest);
        const size_t b = rest.find_first_not_of(" \t");
        rest = b == std::string::npos ? "" : rest.substr(b);
        auto num = [&](uint32_t &v) {
            char *e = nullptr;
            const unsigned long x = std::strtoul(rest.c_str(), &e, 10);
            if (e == rest.c_str())
                c.errors.push_back("line " + std::to_string(ln) + ": " + d + " needs a number");
            else
                v = (uint32_t)x;
        };
        if (d == "boot_ms")
            num(c.boot_ms);
        else if (d == "settle_ms")
            num(c.settle_ms);
        else if (d == "hold")
            num(c.hold_ms);
        else if (d == "key") {
            int k = -1;
            for (int i = 0; i < FM1P_NKEY; i++)
                if (!strcasecmp(rest.c_str(), note_name(i).c_str()))
                    k = i;
            if (k < 0)
                c.errors.push_back("line " + std::to_string(ln) + ": key " + rest + ": not a note F3..G5");
            else
                c.key = k;
        } else if (d == "sound")
            c.sound += rest + "\n";
        else if (d == "save")
            c.save += rest + "\n";
        else if (d == "saved")
            c.saved += rest + "\n";
        else if (d == "saved-param") {
            const size_t sp = rest.rfind(' ');
            if (sp == std::string::npos) {
                c.errors.push_back("line " + std::to_string(ln) + ": saved-param NAME VALUE");
            } else {
                c.saved_param = rest.substr(0, sp);
                c.saved_value = (int32_t)std::strtol(rest.c_str() + sp + 1, nullptr, 0);
                c.has_saved_param = true;
            }
        } else if (d == "saved-lcd-differs") {
            c.saved_lcd_differs = true;
        } else if (d == "flash" && rest.compare(0, 4, "none") == 0) {
            c.flash_none = true;
            c.flash_none_why = rest.size() > 5 ? rest.substr(5) : "(no reason given)";
        } else if (d == "reboot" && rest.compare(0, 4, "none") == 0) {
            c.reboot_none = true;
            c.reboot_none_why = rest.size() > 5 ? rest.substr(5) : "(no reason given)";
        } else if (d == "reboot")
            c.reboot += rest + "\n";
        else if (d == "reboot-guard") {
            std::istringstream r(rest);
            r >> c.guard_fail >> c.guard_reason;
        } else
            c.errors.push_back("line " + std::to_string(ln) + ": unknown directive \"" + d + "\"");
    }
    return c;
}
