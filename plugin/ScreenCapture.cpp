// SPDX-License-Identifier: GPL-3.0-only
// Copy the screen (see ScreenCapture.h).
#include "ScreenCapture.h"

#include "Clipboard.h"

#include <array>

namespace fm1screen {

std::vector<uint8_t> toRgb(const uint16_t *lcdBe, int scale)
{
    const int n = scale == 2 || scale == 4 ? scale : 1;
    const int w = kW * n, h = kH * n;
    std::vector<uint8_t> out((size_t)w * (size_t)h * 3);
    for (int y = 0; y < h; y++) {
        const uint16_t *src = lcdBe + (size_t)(y / n) * kW;
        uint8_t *o = out.data() + (size_t)y * (size_t)w * 3;
        for (int x = 0; x < w; x++, o += 3) {
            const uint32_t c = argbOf(src[x / n]);
            o[0] = (uint8_t)(c >> 16), o[1] = (uint8_t)(c >> 8), o[2] = (uint8_t)c;
        }
    }
    return out;
}

std::vector<uint8_t> rawLE(const uint16_t *lcdBe)
{
    std::vector<uint8_t> out(kRawBytes);
    for (int i = 0; i < kPixels; i++) {
        const uint16_t be = lcdBe[i];
        // (the array holds the bytes as sent: high byte first in memory)
        const uint8_t *b = (const uint8_t *)&be;
        out[(size_t)i * 2] = b[1];
        out[(size_t)i * 2 + 1] = b[0];
    }
    return out;
}

// ------------------------------------------------------------------ PNG ---
static uint32_t crc32(const uint8_t *p, size_t n, uint32_t c = 0)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++)
                v = v & 1u ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
        return t;
    }();
    c = ~c;
    for (size_t i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

static void put32(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

static void chunk(std::vector<uint8_t> &out, const char type[4], const uint8_t *data, size_t n)
{
    put32(out, (uint32_t)n);
    const size_t at = out.size();
    out.insert(out.end(), type, type + 4);
    if (n)
        out.insert(out.end(), data, data + n);
    put32(out, crc32(out.data() + at, n + 4));
}

std::vector<uint8_t> encodePng(const uint8_t *rgb, int w, int h)
{
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, (uint32_t)w);
    put32(ihdr, (uint32_t)h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});    // 8 bits, RGB, deflate, filter 0, no interlace
    chunk(out, "IHDR", ihdr.data(), ihdr.size());
    const uint8_t intent = 0;                    // sRGB, perceptual
    chunk(out, "sRGB", &intent, 1);
    // the rows, each with filter type 0, zlib-compressed (JUCE's zlib: windowBits 0 = the zlib format)
    juce::MemoryOutputStream z;
    {
        juce::GZIPCompressorOutputStream gz(z, 9);
        const uint8_t none = 0;
        for (int y = 0; y < h; y++) {
            gz.write(&none, 1);
            gz.write(rgb + (size_t)y * (size_t)w * 3, (size_t)w * 3);
        }
        gz.flush();
    }
    chunk(out, "IDAT", (const uint8_t *)z.getData(), z.getDataSize());
    chunk(out, "IEND", nullptr, 0);
    return out;
}

juce::String defaultFileName(const juce::String &firmwareId, const juce::String &version, juce::Time t, const juce::String &ext)
{
    auto safe = [](juce::String s, const char *fallback) {
        s = s.trim().replaceCharacters(" /\\:", "____").retainCharacters(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-+");
        return s.isEmpty() ? juce::String(fallback) : s;
    };
    return safe(firmwareId, "fm1") + "-" + safe(version, "0") + "-" + t.formatted("%Y%m%d-%H%M%S") + ext;
}

// ------------------------------------------------------------ clipboard ---
bool copyImage(const uint16_t *lcdBe, int scale)
{
    const int n = scale == 2 || scale == 4 ? scale : 1;
    const std::vector<uint8_t> rgb = toRgb(lcdBe, n);
    const std::vector<uint8_t> png = encodePng(rgb.data(), kW * n, kH * n);
    return fm1clip::writeImage(rgb.data(), kW * n, kH * n, png);
}

bool copyRaw(const uint16_t *lcdBe) { return fm1clip::writeData(rawLE(lcdBe), "com.quixotic7.fm1vst.rgb565le"); }

bool savePng(const juce::File &f, const uint16_t *lcdBe, int scale)
{
    const int n = scale == 2 || scale == 4 ? scale : 1;
    const std::vector<uint8_t> rgb = toRgb(lcdBe, n);
    const std::vector<uint8_t> png = encodePng(rgb.data(), kW * n, kH * n);
    return f.replaceWithData(png.data(), png.size());
}

bool saveRaw(const juce::File &f, const uint16_t *lcdBe)
{
    const std::vector<uint8_t> raw = rawLE(lcdBe);
    return f.replaceWithData(raw.data(), raw.size());
}

}   // namespace fm1screen

#if !JUCE_MAC
// (no clipboard for images elsewhere)
namespace fm1clip {
void setPasteboardName(const std::string &) {}
std::string pasteboardName() { return {}; }
bool writeImage(const uint8_t *, int, int, const std::vector<uint8_t> &) { return false; }
bool writeData(const std::vector<uint8_t> &, const char *) { return false; }
std::string makeUniquePasteboard() { return {}; }
void releasePasteboard(const std::string &) {}
long changeCount(const std::string &) { return 0; }
std::vector<std::string> types(const std::string &) { return {}; }
std::vector<uint8_t> read(const std::string &, const char *) { return {}; }
bool decode(const std::vector<uint8_t> &, int &, int &, std::vector<uint8_t> &, std::string &) { return false; }
}   // namespace fm1clip
#endif
