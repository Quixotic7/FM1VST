// SPDX-License-Identifier: GPL-3.0-only
// Core modules (FM1-VST-PLAN.md 4.3): finding *.fm1core files and loading one per instrument.
//
// WHY EACH LOAD IS A COPY. A core is the firmware compiled as one translation unit: all of its state is C globals
// (emu_hal, the engine, the UI, the flash image). dyld maps a given file once per process however often it is
// dlopen'ed (it recognises the path and the file's inode), so two instruments loading the same file would share
// one firmware. load() therefore copies the module to a unique path first,
//     ~/Library/Caches/fm1emu/instances/<uuid>/<name>.fm1core
// and dlopens the copy: a new file is a new image with its own globals. The copy (and its folder) is deleted when
// the LoadedCore goes away. RTLD_LOCAL keeps one copy's symbols from binding another's (the module exports only
// fm1core_get anyway).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "fm1core.h"

struct CoreInfo {
    std::string path;       // the module file as found
    std::string id, name, version, source_url;
    uint32_t flash_size = 0;
};

class LoadedCore {
public:
    ~LoadedCore();          // shutdown(), dlclose, the copy and its folder deleted
    LoadedCore(const LoadedCore &) = delete;
    LoadedCore &operator=(const LoadedCore &) = delete;

    const fm1core_t *core() const { return core_; }
    const std::string &source_path() const { return source_; }   // the module it was copied from
    const std::string &copy_path() const { return copy_; }       // the per-instance copy dlopen'ed

private:
    friend class CoreLoader;
    LoadedCore() = default;
    void *handle_ = nullptr;
    const fm1core_t *core_ = nullptr;
    std::string source_, copy_, dir_;
};

class CoreLoader {
public:
    // ~/Library/Application Support/fm1emu/cores, created if missing ("" if HOME is unknown)
    static std::string default_user_dir();
    // ~/Library/Caches/fm1emu/instances (the per-instance copies live in a fresh folder under it)
    static std::string instances_dir();

    // every *.fm1core in dirs (not recursive), in the order given, each probed once (loaded, its descriptor read,
    // unloaded); a file that fails to load or has another ABI is skipped and reported in errors (if given)
    static std::vector<CoreInfo> scan(const std::vector<std::string> &dirs, std::vector<std::string> *errors = nullptr);
    // read one module's descriptor without keeping it loaded
    static bool probe(const std::string &path, CoreInfo &out, std::string *error = nullptr);

    // a private copy of the module, loaded (RTLD_NOW | RTLD_LOCAL), its ABI checked; nullptr on failure
    static std::unique_ptr<LoadedCore> load(const std::string &path, std::string *error = nullptr);
};
