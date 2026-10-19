// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 Firmware Hub (https://thefirmwarehub.com, zvenson's) as a source of emulated firmwares: every Hub firmware
// with an FM-1 package runs on the fm1emu core (core-glue/fm1emu), as on RemoteFM1's site
// (https://quixotic7.github.io/RemoteFM1/: web/engines/emu/catalog.mjs, web/site/js/live.js). FM1VST bundles no
// firmware: a package is fetched from where the Hub points when the user picks it.
//
// THE CATALOG  https://thefirmwarehub.com/catalog.json (FM1VST_HUB_CATALOG overrides the URL, FM1VST_HUB the base the
//              packages' relative paths resolve against: tests point both at a file:// fixture). Fetched off the audio
//              and message threads (Service's worker thread, juce::URL with a timeout), at most every 10 minutes per
//              process, and cached as hub/catalog.json. Offline, the cache; on a first run offline, the bundled
//              fallback list (plugin/hub/fallback-catalog.json: a snapshot of the un-gated firmwares only, so no gated
//              firmware's name is in the binary's list).
// THE ENTRIES  each firmware with a `pkg` (the Hub's own id, name, author, version, pkg, sha256, size, product, source,
//              beta, kind, what), in the Hub's order; Bubba is listed with a note (it does not run on fm1-emu). M-VAVE's
//              stock firmware is never fetched (as on the site: the user opens their own FM-1.fwsc); the catalog's
//              `stock.sha256` only recognises the file.
// PACKAGES     hub/packages/<id>-<version>-<sha256[0..12]>.fwsc, written only after the bytes match the catalog's
//              SHA-256 and size (a mismatch is refused and nothing is kept). The Hub deletes old packages, so a pick
//              always uses the current entry; a project reopened later finds its package here by SHA-256, offline too.
// FOLDERS      <app support>/FM1VST/hub (FM1EMU_HOME relocates <app support>, as for every other FM1VST file).
#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace hub {

struct Entry {
    juce::String id, name, author, version;
    juce::String pkg;                       // the package's absolute URL
    juce::String sha256;                    // lower-case hex, as the catalog gives it
    juce::int64 size = 0;
    juce::String product, source, release, what;
    juce::String editor;                    // the catalog's `editor`: the firmware's web editor / manager, "" none
    juce::StringArray kinds;                // the Hub's kind(s): synth groove chords drums dev other
    bool beta = false;
    juce::String note;                      // known not to run on fm1-emu (Bubba), else ""

    juce::String category() const;          // the browser's: Synth Groovebox Chords Drums Other (the first kind's)
    juce::String tag() const;               // a short tag from `what` (live.js tagOf)
    juce::String shortAuthor() const;       // the author without a parenthesis
};

struct Catalog {
    juce::String updated;                   // the Hub's "updated"
    juce::String origin;                    // "live", "cache" or "bundled"
    std::vector<Entry> firmwares;
    juce::String stockSha256, stockSource, stockProduct;   // M-VAVE's V15 (recognised, never fetched)
    const Entry *find(const juce::String &id) const;
    const Entry *findSha(const juce::String &sha256) const;
};

// the Hub's id for an FM1VST core and back (they are the same but for two)
juce::String hubIdForCore(const juce::String &coreId);
juce::String coreIdForHub(const juce::String &hubId);
// the site's version order (live.js compareVersions, simplified): <0, 0, >0, or nullopt when unsure
std::optional<int> compareVersions(const juce::String &a, const juce::String &b);
bool hubNewer(const juce::String &hubVersion, const juce::String &nativeVersion);   // unsure counts as newer

juce::String baseUrl();                     // FM1VST_HUB, else https://thefirmwarehub.com/
juce::String catalogUrl();                  // FM1VST_HUB_CATALOG, else baseUrl() + catalog.json
juce::File dir();                           // <app support>/FM1VST/hub
juce::File packagesDir();                   // dir()/packages
juce::File cachedCatalogFile();             // dir()/catalog.json

// the Hub's catalog JSON (relative package paths against base); false (and why) when it is not one
bool parse(const juce::String &json, const juce::String &base, Catalog &out, juce::String *error = nullptr);
Catalog bundled();                          // the fallback list compiled in (origin "bundled")
Catalog cached();                           // hub/catalog.json (origin "cache"), empty when there is none

juce::String sha256Hex(const void *data, size_t n);
// "" when the bytes are the catalog's (SHA-256, and size when given), else why not
juce::String verify(const void *data, size_t n, const juce::String &sha256, juce::int64 size);
juce::File packageFile(const juce::String &id, const juce::String &version, const juce::String &sha256);
// a cached package with this SHA-256 (its bytes checked), or false
bool readCached(const juce::String &sha256, juce::MemoryBlock &out);
// put bytes the caller has checked into the cache (atomically); the file, or {} on failure
juce::File store(const juce::String &id, const juce::String &version, const juce::String &sha256, const void *data, size_t n);

// The catalog and the downloads, off the audio and message threads: one worker per process (hold it with a
// juce::SharedResourcePointer<hub::Service>). Results come back on the message thread (MessageManager::callAsync),
// and every change is broadcast (ChangeBroadcaster, message thread).
class Service : public juce::ChangeBroadcaster, private juce::Thread {
public:
    Service();
    ~Service() override;

    // the best catalog known: the live one, else the cache, else the bundled list (message thread or any)
    Catalog catalog() const;
    // fetch the catalog unless it was fetched less than 10 minutes ago (force: now); returns at once
    void refresh(bool force = false);
    bool refreshing() const { return catBusy_.load(); }
    juce::String lastError() const;          // the last catalog fetch's failure, "" after a success

    // a package: from the cache, else downloaded, checked and cached; done(file, error) on the message thread
    using Done = std::function<void(juce::File file, juce::String error)>;
    void fetch(const Entry &e, Done done);
    struct Job {
        juce::String sha256, name, version;
        bool running = false, failed = false;
        double progress = 0;                 // 0..1 (-1: the size is not known)
        juce::String error;
    };
    std::optional<Job> job(const juce::String &sha256) const;

    // blocking (the worker; tests): the catalog from catalogUrl() into out (and the cache), or false and why
    static bool fetchCatalogNow(Catalog &out, juce::String *error, int timeoutMs = 10000);
    // the package's file, from the cache or downloaded (checked, cached); {} and why on failure
    static juce::File downloadNow(const Entry &e, juce::String &error, const std::function<void(double)> &progress = {},
                                  int timeoutMs = 20000);

private:
    void run() override;
    struct Task {
        bool catalog = false;
        Entry entry;
        std::vector<Done> done;
    };
    mutable std::mutex m_;
    std::vector<Task> queue_;
    std::map<juce::String, Job> jobs_;
    std::optional<Catalog> live_;
    juce::uint32 liveAt_ = 0;               // Time::getMillisecondCounter when live_ was fetched
    juce::String error_;
    std::atomic<bool> catBusy_{false};
    juce::WaitableEvent wake_;
};

}   // namespace hub
