// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 Firmware Hub's catalog and packages (see Hub.h).
#include "Hub.h"

#include <juce_cryptography/juce_cryptography.h>

#include <BinaryData.h>

#include "cores.h"

namespace hub {

using juce::String;

// ---------------------------------------------------------------- entries ---
String Entry::category() const
{
    static const std::map<String, String> cat{{"synth", "Synth"}, {"groove", "Groovebox"}, {"chords", "Chords"},
                                              {"drums", "Drums"},  {"other", "Other"},      {"dev", "Other"}};
    for (const String &k : kinds) {
        auto it = cat.find(k.toLowerCase());
        if (it != cat.end())
            return it->second;
    }
    return "Other";
}

String Entry::tag() const
{
    // live.js tagOf: the first clause, without the article, at most ~30 characters
    String s = what;
    for (const char *sep : {":", ";", "(", ". ", " - ", " \xe2\x80\x94 "}) {
        const int i = s.indexOf(String::fromUTF8(sep));
        if (i >= 0)
            s = s.substring(0, i);
    }
    if (s.endsWithChar('.'))
        s = s.dropLastCharacters(1);
    s = s.trim();
    for (const char *a : {"a ", "an ", "the "})
        if (s.startsWithIgnoreCase(a))
            s = s.substring((int)std::strlen(a)).trim();
    if (s.isEmpty())
        return "Firmware";
    const String comma = s.upToFirstOccurrenceOf(",", false, false).trim();
    if (s.length() > 30 && comma.length() >= 12)
        s = comma;
    s = s.substring(0, 1).toUpperCase() + s.substring(1);
    if (s.length() <= 30)
        return s;
    String cut = s.substring(0, 29);
    const int sp = cut.lastIndexOfChar(' ');
    if (sp > 12)
        cut = cut.substring(0, sp);
    return cut.trimCharactersAtEnd(" ,-") + String::fromUTF8("\xe2\x80\xa6");
}

String Entry::shortAuthor() const
{
    String a = author;
    for (int i; (i = a.indexOfChar('(')) >= 0;) {
        const int j = a.indexOfChar(i, ')');
        a = a.substring(0, i) + (j > i ? a.substring(j + 1) : String());
    }
    a = a.trim();
    return a.isNotEmpty() ? a : String("unknown");
}

const Entry *Catalog::find(const String &id) const
{
    for (const Entry &e : firmwares)
        if (e.id == id)
            return &e;
    return nullptr;
}

const Entry *Catalog::findSha(const String &sha) const
{
    for (const Entry &e : firmwares)
        if (e.sha256.equalsIgnoreCase(sha))
            return &e;
    return nullptr;
}

String hubIdForCore(const String &coreId)
{
    if (coreId == "fartmachine")
        return "fart";
    if (coreId == "nomansky")
        return "nmsfm";
    return coreId;
}

String coreIdForHub(const String &hubId)
{
    if (hubId == "fart")
        return "fartmachine";
    if (hubId == "nmsfm")
        return "nomansky";
    return hubId;
}

// ---------------------------------------------------------------- versions ---
namespace {
String normVersion(const String &v)
{
    String s = v.trim().toLowerCase();
    if (s.startsWithChar('v') && s.length() > 1 && juce::CharacterFunctions::isDigit(s[1]))
        s = s.substring(1);
    return s.upToFirstOccurrenceOf("+", false, false);
}
// a suffix's rank: a release 100, a pre-release below it, -1 unknown
int suffixRank(String s)
{
    s = s.trimCharactersAtStart("-._ ");
    if (s.isEmpty())
        return 100;
    static const std::pair<const char *, int> pre[] = {{"dev", 0}, {"snapshot", 0}, {"alpha", 10}, {"beta", 20}, {"preview", 30},
                                                       {"pre", 30}, {"test", 30},    {"rc", 40},    {"a", 10},     {"b", 20}};
    for (const auto &p : pre)
        if (s.startsWith(p.first)) {
            const String rest = s.substring((int)std::strlen(p.first)).trimCharactersAtStart("-._");
            return p.second + juce::jlimit(0, 9, rest.getIntValue());
        }
    return -1;
}
}   // namespace

std::optional<int> compareVersions(const String &a, const String &b)
{
    const String x = normVersion(a), y = normVersion(b);
    if (x == y)
        return 0;
    if (x.isEmpty() || y.isEmpty())
        return std::nullopt;
    auto head = [](const String &s, String &rest) {
        int i = 0;
        while (i < s.length() && (juce::CharacterFunctions::isDigit(s[i]) || (s[i] == '.' && i + 1 < s.length() &&
                                                                               juce::CharacterFunctions::isDigit(s[i + 1]))))
            i++;
        rest = s.substring(i);
        return s.substring(0, i);
    };
    String rx, ry;
    const String hx = head(x, rx), hy = head(y, ry);
    if (hx.isEmpty() || hy.isEmpty())
        return std::nullopt;
    juce::StringArray cx, cy;
    cx.addTokens(hx, ".", "");
    cy.addTokens(hy, ".", "");
    for (int i = 0; i < juce::jmax(cx.size(), cy.size()); i++) {
        const int d = cx[i].getIntValue() - cy[i].getIntValue();
        if (d)
            return d > 0 ? 1 : -1;
    }
    const int sx = suffixRank(rx), sy = suffixRank(ry);
    if (sx < 0 || sy < 0)
        return rx == ry ? std::optional<int>(0) : std::nullopt;
    return sx == sy ? (rx == ry ? std::optional<int>(0) : std::nullopt) : std::optional<int>(sx > sy ? 1 : -1);
}

bool hubNewer(const String &hubVersion, const String &nativeVersion)
{
    const auto c = compareVersions(hubVersion, nativeVersion);
    return !c || *c > 0;
}

// ---------------------------------------------------------------- places ---
static String envOr(const char *name, const String &dflt)
{
    const char *v = std::getenv(name);
    return v && *v ? String::fromUTF8(v) : dflt;
}

String baseUrl()
{
    String b = envOr("FM1VST_HUB", "https://thefirmwarehub.com/");
    return b.endsWithChar('/') ? b : b + "/";
}

String catalogUrl() { return envOr("FM1VST_HUB_CATALOG", baseUrl() + "catalog.json"); }

juce::File dir() { return juce::File(String::fromUTF8(CoreLoader::app_support_dir().c_str())).getChildFile("FM1VST").getChildFile("hub"); }
juce::File packagesDir() { return dir().getChildFile("packages"); }
juce::File cachedCatalogFile() { return dir().getChildFile("catalog.json"); }

// ---------------------------------------------------------------- parsing ---
static String resolve(const String &base, const String &path)
{
    if (path.isEmpty())
        return {};
    if (path.matchesWildcard("*://*", true))
        return path;
    return (base.endsWithChar('/') ? base : base + "/") + path.trimCharactersAtStart("/");
}

bool parse(const String &json, const String &base, Catalog &out, String *error)
{
    juce::var v;
    const juce::Result r = juce::JSON::parse(json, v);
    if (r.failed() || !v.isObject()) {
        if (error)
            *error = "not JSON: " + r.getErrorMessage();
        return false;
    }
    const juce::var fws = v.getProperty("firmwares", {});
    if (!fws.isArray() || fws.size() == 0) {
        if (error)
            *error = "no firmwares in it";
        return false;
    }
    Catalog c;
    c.updated = v.getProperty("updated", "").toString();
    for (const juce::var &f : *fws.getArray()) {
        if (!f.isObject())
            continue;
        Entry e;
        e.id = f.getProperty("id", "").toString();
        e.pkg = f.getProperty("pkg", "").toString();
        if (e.id.isEmpty() || e.pkg.isEmpty() || c.find(e.id))
            continue;                            // (no package: nothing to run; a repeated id: the first)
        e.pkg = resolve(base, e.pkg);
        e.name = f.getProperty("name", e.id).toString();
        e.author = f.getProperty("author", "").toString();
        e.version = f.getProperty("version", "").toString();
        e.sha256 = f.getProperty("sha256", "").toString().toLowerCase();
        e.size = (juce::int64)f.getProperty("size", 0);
        e.product = f.getProperty("product", "").toString();
        const String repo = f.getProperty("repo", "").toString();
        e.source = f.getProperty("source", "").toString();
        if (e.source.isEmpty())
            e.source = repo.isNotEmpty() ? "https://github.com/" + repo : f.getProperty("site", "").toString();
        e.release = f.getProperty("release", "").toString();
        e.what = f.getProperty("what", "").toString();
        e.beta = (bool)f.getProperty("beta", false);
        const juce::var k = f.getProperty("kind", {});
        if (k.isArray())
            for (const juce::var &x : *k.getArray())
                e.kinds.add(x.toString());
        else if (k.toString().isNotEmpty())
            e.kinds.add(k.toString());
        if (e.id == "bubba")                     // (catalog.mjs EMU_SKIP)
            e.note = "boots to a black screen, no sound (needs M-VAVE's own loader)";
        c.firmwares.push_back(std::move(e));
    }
    const juce::var stock = v.getProperty("stock", {});
    if (stock.isObject()) {
        c.stockSha256 = stock.getProperty("sha256", "").toString().toLowerCase();
        c.stockSource = stock.getProperty("source", "").toString();
        c.stockProduct = stock.getProperty("product", "").toString();
    }
    if (c.firmwares.empty()) {
        if (error)
            *error = "no firmware with a package in it";
        return false;
    }
    out = std::move(c);
    return true;
}

Catalog bundled()
{
    Catalog c;
    parse(String::fromUTF8(BinaryData::fallbackcatalog_json, BinaryData::fallbackcatalog_jsonSize), "https://thefirmwarehub.com/", c);
    c.origin = "bundled";
    return c;
}

Catalog cached()
{
    Catalog c;
    const juce::File f = cachedCatalogFile();
    if (f.existsAsFile() && parse(f.loadFileAsString(), baseUrl(), c))
        c.origin = "cache";
    else
        c = {};
    return c;
}

// ---------------------------------------------------------------- packages ---
String sha256Hex(const void *data, size_t n) { return juce::SHA256(data, n).toHexString().toLowerCase(); }

String verify(const void *data, size_t n, const String &sha256, juce::int64 size)
{
    if (size > 0 && (juce::int64)n != size)
        return String((juce::int64)n) + " bytes, the catalog says " + String(size);
    if (sha256.isEmpty())
        return "the catalog gives no SHA-256";
    const String got = sha256Hex(data, n);
    if (!got.equalsIgnoreCase(sha256))
        return "SHA-256 " + got.substring(0, 12) + String::fromUTF8("\xe2\x80\xa6, the catalog says ") + sha256.substring(0, 12) +
               String::fromUTF8("\xe2\x80\xa6");
    return {};
}

juce::File packageFile(const String &id, const String &version, const String &sha256)
{
    const String name = juce::File::createLegalFileName(id + "-" + version).replaceCharacter(' ', '_') + "-" +
                        sha256.toLowerCase().substring(0, 12) + ".fwsc";
    return packagesDir().getChildFile(name);
}

bool readCached(const String &sha256, juce::MemoryBlock &out)
{
    if (sha256.length() < 12)
        return false;
    const String tail = "-" + sha256.toLowerCase().substring(0, 12) + ".fwsc";
    for (const juce::File &f : packagesDir().findChildFiles(juce::File::findFiles, false, "*.fwsc")) {
        if (!f.getFileName().endsWith(tail))
            continue;
        juce::MemoryBlock mb;
        if (f.loadFileAsData(mb) && verify(mb.getData(), mb.getSize(), sha256, 0).isEmpty()) {
            out = std::move(mb);
            return true;
        }
    }
    return false;
}

juce::File store(const String &id, const String &version, const String &sha256, const void *data, size_t n)
{
    const juce::File f = packageFile(id, version, sha256);
    if (!f.getParentDirectory().createDirectory())
        return {};
    juce::TemporaryFile tmp(f);
    {
        juce::FileOutputStream out(tmp.getFile());
        if (!out.openedOk() || !out.write(data, n))
            return {};
        out.flush();
        if (out.getStatus().failed())
            return {};
    }
    return tmp.overwriteTargetFileWithTemporary() ? f : juce::File();
}

// ---------------------------------------------------------------- fetching ---
// a URL's bytes (file:// too), at most maxBytes; false and why on failure
static bool fetchBytes(const String &url, juce::MemoryBlock &out, String &error, int timeoutMs, juce::int64 maxBytes,
                       const std::function<void(juce::int64 got, juce::int64 total)> &progress = {})
{
    const juce::URL u(url);
    int status = 0;
    std::unique_ptr<juce::InputStream> in;
    if (u.isLocalFile())
        in = u.getLocalFile().createInputStream();
    else
        in = u.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                     .withConnectionTimeoutMs(timeoutMs)
                                     .withNumRedirectsToFollow(5)
                                     .withStatusCode(&status)
                                     .withExtraHeaders("User-Agent: FM1VST/" JucePlugin_VersionString));
    if (!in) {
        error = u.isLocalFile() ? "cannot read " + u.getLocalFile().getFullPathName() : "cannot reach " + u.getDomain();
        return false;
    }
    if (status >= 400) {
        error = url + ": HTTP " + String(status);
        return false;
    }
    const juce::int64 total = in->getTotalLength();
    juce::MemoryOutputStream mo(out, false);
    char buf[32768];
    const juce::uint32 t0 = juce::Time::getMillisecondCounter();
    for (;;) {
        const auto got = in->read(buf, sizeof buf);
        if (got < 0) {
            error = url + ": read failed";
            return false;
        }
        if (got == 0) {
            if (in->isExhausted())
                break;
            if (juce::Time::getMillisecondCounter() - t0 > (juce::uint32)timeoutMs * 6) {
                error = url + ": timed out";
                return false;
            }
            juce::Thread::sleep(5);
            continue;
        }
        mo.write(buf, (size_t)got);
        if ((juce::int64)mo.getDataSize() > maxBytes) {
            error = url + ": larger than " + String(maxBytes) + " bytes";
            return false;
        }
        if (progress)
            progress((juce::int64)mo.getDataSize(), total);
    }
    mo.flush();
    out.setSize(mo.getDataSize());
    return true;
}

bool Service::fetchCatalogNow(Catalog &out, String *error, int timeoutMs)
{
    juce::MemoryBlock mb;
    String err;
    if (!fetchBytes(catalogUrl(), mb, err, timeoutMs, 16 << 20)) {
        if (error)
            *error = err;
        return false;
    }
    const String json = String::fromUTF8((const char *)mb.getData(), (int)mb.getSize());
    Catalog c;
    if (!parse(json, baseUrl(), c, &err)) {
        if (error)
            *error = catalogUrl() + ": " + err;
        return false;
    }
    c.origin = "live";
    const juce::File f = cachedCatalogFile();
    if (f.getParentDirectory().createDirectory()) {
        juce::TemporaryFile tmp(f);
        if (tmp.getFile().replaceWithText(json))
            tmp.overwriteTargetFileWithTemporary();
    }
    out = std::move(c);
    return true;
}

juce::File Service::downloadNow(const Entry &e, String &error, const std::function<void(double)> &progress, int timeoutMs)
{
    juce::MemoryBlock mb;
    if (readCached(e.sha256, mb))
        return packageFile(e.id, e.version, e.sha256).existsAsFile() ? packageFile(e.id, e.version, e.sha256)
                                                                      : store(e.id, e.version, e.sha256, mb.getData(), mb.getSize());
    if (e.pkg.isEmpty()) {
        error = e.name + ": no package to download (open your own .fwsc)";
        return {};
    }
    if (e.sha256.isEmpty()) {
        error = e.name + ": the catalog gives no SHA-256, so the package cannot be checked";
        return {};
    }
    String err;
    if (!fetchBytes(e.pkg, mb, err, timeoutMs, 4 << 20, [&](juce::int64 got, juce::int64 total) {
            if (progress)
                progress(total > 0 ? (double)got / (double)total : (e.size > 0 ? (double)got / (double)e.size : -1.0));
        })) {
        error = e.name + " " + e.version + ": " + err;
        return {};
    }
    const String bad = verify(mb.getData(), mb.getSize(), e.sha256, e.size);
    if (bad.isNotEmpty()) {
        error = e.name + " " + e.version + ": refused, " + bad;
        return {};
    }
    const juce::File f = store(e.id, e.version, e.sha256, mb.getData(), mb.getSize());
    if (f == juce::File())
        error = e.name + ": cannot write " + packageFile(e.id, e.version, e.sha256).getFullPathName();
    return f;
}

// ---------------------------------------------------------------- the service ---
Service::Service() : juce::Thread("FM1VST Hub") { startThread(juce::Thread::Priority::low); }

Service::~Service()
{
    signalThreadShouldExit();
    wake_.signal();
    stopThread(30000);
}

Catalog Service::catalog() const
{
    {
        std::lock_guard<std::mutex> l(m_);
        if (live_)
            return *live_;
    }
    Catalog c = cached();
    return c.firmwares.empty() ? bundled() : c;
}

String Service::lastError() const
{
    std::lock_guard<std::mutex> l(m_);
    return error_;
}

void Service::refresh(bool force)
{
    {
        std::lock_guard<std::mutex> l(m_);
        if (catBusy_.load())
            return;
        if (!force && live_ && juce::Time::getMillisecondCounter() - liveAt_ < 10u * 60u * 1000u)
            return;
        for (const Task &t : queue_)
            if (t.catalog)
                return;
        Task t;
        t.catalog = true;
        queue_.push_back(std::move(t));
        catBusy_.store(true);
    }
    wake_.signal();
}

void Service::fetch(const Entry &e, Done done)
{
    {
        std::lock_guard<std::mutex> l(m_);
        for (Task &t : queue_)
            if (!t.catalog && t.entry.sha256 == e.sha256) {
                t.done.push_back(std::move(done));
                return;
            }
        Job j;
        j.sha256 = e.sha256;
        j.name = e.name;
        j.version = e.version;
        j.running = true;
        jobs_[e.sha256] = j;
        Task t;
        t.entry = e;
        t.done.push_back(std::move(done));
        queue_.push_back(std::move(t));
    }
    wake_.signal();
    sendChangeMessage();
}

std::optional<Service::Job> Service::job(const String &sha) const
{
    std::lock_guard<std::mutex> l(m_);
    auto it = jobs_.find(sha);
    if (it == jobs_.end())
        return std::nullopt;
    return it->second;
}

void Service::run()
{
    while (!threadShouldExit()) {
        std::optional<Task> t;
        {
            std::lock_guard<std::mutex> l(m_);
            if (!queue_.empty()) {
                t = std::move(queue_.front());
                queue_.erase(queue_.begin());
            }
        }
        if (!t) {
            wake_.wait(1000);
            continue;
        }
        if (t->catalog) {
            Catalog c;
            String err;
            const bool ok = fetchCatalogNow(c, &err);
            {
                std::lock_guard<std::mutex> l(m_);
                if (ok) {
                    live_ = std::move(c);
                    liveAt_ = juce::Time::getMillisecondCounter();
                    error_.clear();
                } else {
                    error_ = err;
                }
                catBusy_.store(false);
            }
            sendChangeMessage();
            continue;
        }
        const String sha = t->entry.sha256;
        String err;
        const juce::File f = downloadNow(t->entry, err, [this, sha](double p) {
            std::lock_guard<std::mutex> l(m_);
            jobs_[sha].progress = p;
        });
        {
            std::lock_guard<std::mutex> l(m_);
            Job &j = jobs_[sha];
            j.running = false;
            j.failed = f == juce::File();
            j.error = err;
            j.progress = j.failed ? j.progress : 1.0;
        }
        sendChangeMessage();
        auto done = std::make_shared<std::vector<Done>>(std::move(t->done));
        juce::MessageManager::callAsync([done, f, err] {
            for (const Done &d : *done)
                if (d)
                    d(f, err);
        });
    }
}

}   // namespace hub
