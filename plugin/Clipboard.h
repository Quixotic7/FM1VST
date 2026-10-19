// SPDX-License-Identifier: GPL-3.0-only
// The macOS clipboard for images and data (Clipboard.mm, NSPasteboard; JUCE's SystemClipboard is text only). Used by
// ScreenCapture.h. Elsewhere every call returns false / empty.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fm1clip {

// where the writes go: "" (the default) the general pasteboard; a test sets a private one (makeUniquePasteboard)
void setPasteboardName(const std::string &name);
std::string pasteboardName();

// 8-bit RGB (w * h * 3, rows top to bottom), sRGB: the given PNG bytes as public.png and the same pixels as an
// uncompressed sRGB TIFF (public.tiff), in one write (the pasteboard cleared first)
bool writeImage(const uint8_t *rgb, int w, int h, const std::vector<uint8_t> &png);
// bytes under a type (a UTI), plus public.data
bool writeData(const std::vector<uint8_t> &bytes, const char *uti);

// ---- tests
std::string makeUniquePasteboard();                 // a private named pasteboard (pasteboardWithUniqueName)
void releasePasteboard(const std::string &name);    // releaseGlobally
long changeCount(const std::string &name);         // ("" the general one: read only, never written by a test)
std::vector<std::string> types(const std::string &name);
std::vector<uint8_t> read(const std::string &name, const char *uti);
// an image file's bytes (PNG, TIFF) decoded by ImageIO without colour matching: its size, its pixels as 8-bit RGB, and
// its colour space's name ("kCGColorSpaceSRGB"); false if it is not 8 bits per channel RGB / RGBA
bool decode(const std::vector<uint8_t> &file, int &w, int &h, std::vector<uint8_t> &rgb, std::string &colourSpace);

}   // namespace fm1clip
