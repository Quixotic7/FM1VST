// SPDX-License-Identifier: GPL-3.0-only
// The macOS clipboard for images and data (see Clipboard.h). Written to build with or without ARC: no alloc / retain,
// the Core Foundation objects released by hand.
#include "Clipboard.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include <mutex>

namespace fm1clip {

static std::mutex nameLock;
static std::string current;

void setPasteboardName(const std::string &name)
{
    std::lock_guard<std::mutex> l(nameLock);
    current = name;
}

std::string pasteboardName()
{
    std::lock_guard<std::mutex> l(nameLock);
    return current;
}

static NSPasteboard *board(const std::string &name)
{
    if (name.empty())
        return [NSPasteboard generalPasteboard];
    NSString *n = [NSString stringWithUTF8String:name.c_str()];
    return n ? [NSPasteboard pasteboardWithName:n] : nil;
}

// 8-bit RGB as a CGImage in sRGB (the bytes copied); null on failure
static CGImageRef makeImage(const uint8_t *rgb, int w, int h)
{
    CFDataRef data = CFDataCreate(nullptr, rgb, (CFIndex)w * h * 3);
    if (!data)
        return nullptr;
    CGDataProviderRef prov = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef img = nullptr;
    if (prov && cs)
        img = CGImageCreate((size_t)w, (size_t)h, 8, 24, (size_t)w * 3, cs, (CGBitmapInfo)kCGImageAlphaNone | kCGBitmapByteOrderDefault,
                            prov, nullptr, false, kCGRenderingIntentDefault);
    if (cs)
        CGColorSpaceRelease(cs);
    if (prov)
        CGDataProviderRelease(prov);
    return img;
}

// the image as an uncompressed TIFF (its sRGB profile embedded by ImageIO)
static NSData *tiffOf(CGImageRef img)
{
    CFMutableDataRef out = CFDataCreateMutable(nullptr, 0);
    CGImageDestinationRef dst = out ? CGImageDestinationCreateWithData(out, CFSTR("public.tiff"), 1, nullptr) : nullptr;
    NSData *result = nil;
    if (dst) {
        NSDictionary *props = @{
            (__bridge NSString *)kCGImagePropertyTIFFDictionary : @{(__bridge NSString *)kCGImagePropertyTIFFCompression : @1},
        };
        CGImageDestinationAddImage(dst, img, (__bridge CFDictionaryRef)props);
        if (CGImageDestinationFinalize(dst))
            result = [NSData dataWithBytes:CFDataGetBytePtr(out) length:(NSUInteger)CFDataGetLength(out)];
        CFRelease(dst);
    }
    if (out)
        CFRelease(out);
    return result;
}

bool writeImage(const uint8_t *rgb, int w, int h, const std::vector<uint8_t> &png)
{
    if (!rgb || w <= 0 || h <= 0 || png.empty())
        return false;
    @autoreleasepool {
        CGImageRef img = makeImage(rgb, w, h);
        if (!img)
            return false;
        NSData *tiff = tiffOf(img);
        CGImageRelease(img);
        if (!tiff)
            return false;
        NSData *pngData = [NSData dataWithBytes:png.data() length:png.size()];
        NSPasteboard *pb = board(pasteboardName());
        if (!pb)
            return false;
        [pb clearContents];
        [pb declareTypes:@[ NSPasteboardTypePNG, NSPasteboardTypeTIFF ] owner:nil];
        const BOOL a = [pb setData:pngData forType:NSPasteboardTypePNG];
        const BOOL b = [pb setData:tiff forType:NSPasteboardTypeTIFF];
        return a && b;
    }
}

bool writeData(const std::vector<uint8_t> &bytes, const char *uti)
{
    if (bytes.empty() || !uti)
        return false;
    @autoreleasepool {
        NSPasteboard *pb = board(pasteboardName());
        if (!pb)
            return false;
        NSData *d = [NSData dataWithBytes:bytes.data() length:bytes.size()];
        NSString *t = [NSString stringWithUTF8String:uti];
        [pb clearContents];
        [pb declareTypes:@[ t, @"public.data" ] owner:nil];
        const BOOL a = [pb setData:d forType:t];
        const BOOL b = [pb setData:d forType:@"public.data"];
        return a && b;
    }
}

std::string makeUniquePasteboard()
{
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard pasteboardWithUniqueName];
        const char *n = pb ? [[pb name] UTF8String] : nullptr;
        return n ? std::string(n) : std::string();
    }
}

void releasePasteboard(const std::string &name)
{
    if (name.empty())
        return;                                 // (never the general one)
    @autoreleasepool {
        [board(name) releaseGlobally];
    }
}

long changeCount(const std::string &name)
{
    @autoreleasepool {
        return (long)[board(name) changeCount];
    }
}

std::vector<std::string> types(const std::string &name)
{
    std::vector<std::string> out;
    @autoreleasepool {
        for (NSString *t in [board(name) types])
            out.emplace_back([t UTF8String]);
    }
    return out;
}

std::vector<uint8_t> read(const std::string &name, const char *uti)
{
    std::vector<uint8_t> out;
    @autoreleasepool {
        NSString *t = [NSString stringWithUTF8String:uti];
        NSData *d = t ? [board(name) dataForType:t] : nil;
        if (d)
            out.assign((const uint8_t *)[d bytes], (const uint8_t *)[d bytes] + [d length]);
    }
    return out;
}

bool decode(const std::vector<uint8_t> &file, int &w, int &h, std::vector<uint8_t> &rgb, std::string &colourSpace)
{
    w = h = 0;
    rgb.clear();
    colourSpace.clear();
    CFDataRef data = CFDataCreate(nullptr, file.data(), (CFIndex)file.size());
    if (!data)
        return false;
    CGImageSourceRef src = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (!src)
        return false;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!img)
        return false;
    bool ok = false;
    const size_t bpc = CGImageGetBitsPerComponent(img), bpp = CGImageGetBitsPerPixel(img), stride = CGImageGetBytesPerRow(img);
    const CGImageAlphaInfo alpha = CGImageGetAlphaInfo(img);
    const bool little = (CGImageGetBitmapInfo(img) & kCGBitmapByteOrderMask) == kCGBitmapByteOrder32Little;
    if (CGColorSpaceRef cs = CGImageGetColorSpace(img)) {
        if (CFStringRef n = CGColorSpaceCopyName(cs)) {
            char buf[256] = {};
            if (CFStringGetCString(n, buf, sizeof buf, kCFStringEncodingUTF8))
                colourSpace = buf;
            CFRelease(n);
        }
    }
    // where R, G, B sit in a pixel's bytes
    int ri = -1, gi = -1, bi = -1;
    if (bpc == 8 && bpp == 24 && alpha == kCGImageAlphaNone) {
        ri = 0, gi = 1, bi = 2;
    } else if (bpc == 8 && bpp == 32) {
        const bool alphaFirst = alpha == kCGImageAlphaFirst || alpha == kCGImageAlphaPremultipliedFirst || alpha == kCGImageAlphaNoneSkipFirst;
        // big-endian order: alpha first -> A R G B, else R G B A; little-endian 32: reversed
        const int o = alphaFirst ? 1 : 0;
        if (!little)
            ri = o, gi = o + 1, bi = o + 2;
        else
            ri = 3 - o, gi = 2 - o, bi = 1 - o;
    }
    if (ri >= 0) {
        if (CFDataRef px = CGDataProviderCopyData(CGImageGetDataProvider(img))) {
            w = (int)CGImageGetWidth(img);
            h = (int)CGImageGetHeight(img);
            const uint8_t *p = CFDataGetBytePtr(px);
            const size_t step = bpp / 8;
            if ((size_t)CFDataGetLength(px) >= stride * (size_t)h) {
                rgb.resize((size_t)w * (size_t)h * 3);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        const uint8_t *q = p + (size_t)y * stride + (size_t)x * step;
                        uint8_t *o = rgb.data() + ((size_t)y * (size_t)w + (size_t)x) * 3;
                        o[0] = q[ri], o[1] = q[gi], o[2] = q[bi];
                    }
                ok = true;
            }
            CFRelease(px);
        }
    }
    CGImageRelease(img);
    return ok;
}

}   // namespace fm1clip
