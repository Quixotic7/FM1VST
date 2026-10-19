// SPDX-License-Identifier: GPL-3.0-only
// Copy the screen: the firmware's 240 x 240 LCD out of the plugin, pixel for pixel, for mock-ups in an image editor.
//
// THE FRAME is the one the editor shows: PanelComponent::shownLcd(), the RGB565 big-endian copy (as sent) it last
// converted for drawing (on the message thread; the audio thread is never locked for it). Freeze (the screen's context
// menu) holds that copy while the firmware and the audio run on.
//
// THE COLOURS are the panel's own conversion, argbOf (the 5 / 6 bits replicated, as emu_img.c and the panel draw
// them; core-glue/common/ppm.h's fm1g_rgb_of, c * 255 / 31, is the host sims' reference and differs by one in a few
// levels), so a copy at 1:1 is exactly what the plugin shows at a whole multiple. Scaled copies (x2, x4) are nearest-
// neighbour: each LCD pixel an n x n block.
//
// PNG: 8-bit RGB (no alpha), an sRGB chunk and nothing else that would move a colour (no gAMA, cHRM or iCCP), zlib
// compressed. The clipboard (macOS, Clipboard.h) gets this PNG as public.png and the same pixels as public.tiff
// (sRGB). Raw: the 115,200 bytes of the frame as RGB565 little-endian, row-major (the panel's copy is big-endian, as
// the firmware sends it to the LCD; the raw copy is byte-swapped to the usual order).
#pragma once
#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace fm1screen {

constexpr int kW = 240, kH = 240;
constexpr int kPixels = kW * kH;
constexpr size_t kRawBytes = (size_t)kPixels * 2;   // 115,200

// one big-endian RGB565 pixel (as sent) -> 0xFFRRGGBB, the bits replicated: what the panel draws
inline uint32_t argbOf(uint16_t be)
{
    const uint32_t p = (uint32_t)((be >> 8) | ((be & 0xFFu) << 8));
    const uint32_t r = (p >> 11) & 31u, g = (p >> 5) & 63u, b = p & 31u;
    return 0xFF000000u | ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
}

// the frame as 8-bit RGB, (240 * scale)^2 * 3 bytes, rows top to bottom; scale 1, 2 or 4 (nearest-neighbour)
std::vector<uint8_t> toRgb(const uint16_t *lcdBe, int scale);
// the frame as RGB565 little-endian, row-major (kRawBytes)
std::vector<uint8_t> rawLE(const uint16_t *lcdBe);
// 8-bit RGB (w * h * 3) as a PNG file's bytes (sRGB chunk)
std::vector<uint8_t> encodePng(const uint8_t *rgb, int w, int h);

// "<id>-<version>-<YYYYMMDD-HHMMSS><ext>" (each part reduced to file-name-safe characters)
juce::String defaultFileName(const juce::String &firmwareId, const juce::String &version, juce::Time t, const juce::String &ext);

// the clipboard (macOS: public.png + public.tiff; Clipboard.h); false where there is none or it failed
bool copyImage(const uint16_t *lcdBe, int scale);
bool copyRaw(const uint16_t *lcdBe);
// files
bool savePng(const juce::File &f, const uint16_t *lcdBe, int scale);
bool saveRaw(const juce::File &f, const uint16_t *lcdBe);

}   // namespace fm1screen
