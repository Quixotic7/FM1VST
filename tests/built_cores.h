// SPDX-License-Identifier: GPL-3.0-only
// The cores this build made (CMake's FM1_CORE_IDS as FM1_BUILT_CORES, comma-separated): a public build
// (FM1_PUBLIC_BUILD) leaves the gated ones out, so a test that drives one of those skips (exit 77, ctest
// SKIP_RETURN_CODE) and says which core it needs.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <set>
#include <string>

#ifndef FM1_BUILT_CORES
#error "FM1_BUILT_CORES: the core ids CMake built (CMakeLists.txt)"
#endif

static inline std::set<std::string> built_cores()
{
    std::set<std::string> out;
    const std::string all = FM1_BUILT_CORES;
    for (size_t a = 0; a < all.size();) {
        size_t b = all.find(',', a);
        if (b == std::string::npos)
            b = all.size();
        if (b > a)
            out.insert(all.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

static inline bool core_built(const std::string &id) { return built_cores().count(id) != 0; }

// every core in ids built, or the test ends here: SKIP (77) with the reason
static inline void require_cores(const char *test, std::initializer_list<const char *> ids)
{
    std::string missing;
    for (const char *id : ids)
        if (!core_built(id))
            missing += std::string(missing.empty() ? "" : ", ") + id;
    if (missing.empty())
        return;
    std::printf("%s: SKIP: it drives the %s core%s, which this build leaves out (FM1_PUBLIC_BUILD: gated)\n", test,
                missing.c_str(), missing.find(',') == std::string::npos ? "" : "s");
    std::exit(77);
}
