// SPDX-License-Identifier: GPL-3.0-only
// Core modules: scan, probe, load a per-instance copy (see cores.h for why the copy exists).
#include "cores.h"

#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

static std::string home_dir()
{
    const char *h = std::getenv("HOME");
    if (h && *h)
        return h;
    const struct passwd *pw = getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : "";
}

std::string CoreLoader::default_user_dir()
{
    const std::string home = home_dir();
    if (home.empty())
        return "";
    const fs::path p = fs::path(home) / "Library" / "Application Support" / "fm1emu" / "cores";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

std::string CoreLoader::instances_dir()
{
    const std::string home = home_dir();
    const fs::path base = home.empty() ? fs::temp_directory_path() : fs::path(home) / "Library" / "Caches";
    return (base / "fm1emu" / "instances").string();
}

static const fm1core_t *open_core(const std::string &path, void **handle, std::string *error)
{
    void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        if (error) {
            const char *e = dlerror();
            *error = path + ": " + (e ? e : "dlopen failed");
        }
        return nullptr;
    }
    auto get = reinterpret_cast<fm1core_get_fn>(dlsym(h, FM1CORE_SYMBOL));
    if (!get) {
        if (error)
            *error = path + ": no " FM1CORE_SYMBOL " (not a core module)";
        dlclose(h);
        return nullptr;
    }
    const fm1core_t *c = get(FM1CORE_ABI);
    if (!c || c->abi_version != FM1CORE_ABI) {
        if (error)
            *error = path + ": built for another core ABI (this host: " + std::to_string(FM1CORE_ABI) + ")";
        dlclose(h);
        return nullptr;
    }
    *handle = h;
    return c;
}

static std::string str(const char *s) { return s ? s : ""; }

bool CoreLoader::probe(const std::string &path, CoreInfo &out, std::string *error)
{
    void *h = nullptr;
    const fm1core_t *c = open_core(path, &h, error);
    if (!c)
        return false;
    out.path = path;
    out.id = str(c->id);
    out.name = str(c->name);
    out.version = str(c->version);
    out.source_url = str(c->source_url);
    out.flash_size = c->flash_size;
    dlclose(h);                  // nothing was initialised: no shutdown needed
    return true;
}

std::vector<CoreInfo> CoreLoader::scan(const std::vector<std::string> &dirs, std::vector<std::string> *errors)
{
    std::vector<CoreInfo> found;
    for (const std::string &d : dirs) {
        std::error_code ec;
        if (d.empty() || !fs::is_directory(d, ec))
            continue;
        std::vector<fs::path> files;
        for (const auto &e : fs::directory_iterator(d, ec))
            if (e.path().extension() == FM1CORE_SUFFIX && (e.is_regular_file(ec) || e.is_symlink(ec)))
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path &f : files) {
            CoreInfo info;
            std::string err;
            if (probe(f.string(), info, &err))
                found.push_back(info);
            else if (errors)
                errors->push_back(err);
        }
    }
    return found;
}

std::unique_ptr<LoadedCore> CoreLoader::load(const std::string &path, std::string *error)
{
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        if (error)
            *error = path + ": no such module";
        return nullptr;
    }
    uuid_t u;
    uuid_string_t us;
    uuid_generate_random(u);
    uuid_unparse_lower(u, us);
    const fs::path dir = fs::path(instances_dir()) / us;
    const fs::path copy = dir / fs::path(path).filename();
    fs::create_directories(dir, ec);
    if (ec || !fs::copy_file(path, copy, fs::copy_options::overwrite_existing, ec) || ec) {
        if (error)
            *error = path + ": cannot copy to " + copy.string() + ": " + ec.message();
        fs::remove_all(dir, ec);
        return nullptr;
    }
    void *h = nullptr;
    const fm1core_t *c = open_core(copy.string(), &h, error);
    if (!c) {
        fs::remove_all(dir, ec);
        return nullptr;
    }
    std::unique_ptr<LoadedCore> lc(new LoadedCore());
    lc->handle_ = h;
    lc->core_ = c;
    lc->source_ = path;
    lc->copy_ = copy.string();
    lc->dir_ = dir.string();
    return lc;
}

LoadedCore::~LoadedCore()
{
    if (core_ && core_->shutdown)
        core_->shutdown();
    core_ = nullptr;
    if (handle_)
        dlclose(handle_);
    std::error_code ec;
    if (!copy_.empty())
        fs::remove(copy_, ec);
    if (!dir_.empty())
        fs::remove(dir_, ec);
}
