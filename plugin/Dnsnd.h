// SPDX-License-Identifier: GPL-3.0-only
// Digitone sound files (.dnsnd) read for FIZZ's FM TONE (FIZZ P1.5s): a straight C++ port of ChoralRoot's reader
// (ChoralRootFM1 web/fm1dnsnd.js parseDnsnd + dnsndToQuad, and fm1sounds.js soundFromDnsnd's name), so a sound dropped
// on the Standalone or the plugin becomes the same 96-byte FM TONE blob, byte for byte, as the FIZZ web preview makes
// (tests/dnsnd_test.cpp and tools/dnsnd_compare.mjs check it against the JS on a folder of presets).
//
// A .dnsnd is a ZIP of manifest.json ({FileType "Sound", Payload, ...}) and the payload: a 0x23-byte header (magic
// ac 11 d3 03, the decompressed size u16le at 0x1c = 302), an LZ4 block, a trailer. The 302-byte record: be ef ba ce,
// u32be version (1 or 5), the name (16 bytes) at 12, the parameters as byte pairs from 30. The JS reader's notes (what
// is lost or approximated) are kept as text.
//
// FIZZ P1.5x: Digitone II presets (.dn2pst) too: parseDn2, a port of FMToneSeq tools/dn2pst.mjs (parseDn2pst +
// dn2pstToQuad; the format: FMToneSeq research/DN2-FORMAT.md): a ZIP of manifest.json and Elektron's stored container
// (a 31-byte header, the 364-byte body as an LZ4 block chain or raw, a 12-byte trailer with a zero-seed CRC-32); the
// 359-byte record's FM TONE parameters at 28 + 2 x id with the DN1's laws. Other machines (WAVETONE, FM DRUM,
// SWARMER, MIDI) are refused with the reader's message. Its Digitone tags give FIZZ's tags (BASS, CHORD -> KEYS, PAD,
// LEAD, the drums -> DRUMS, SOUND FX -> FX).
//
// storeFrame() makes FIZZ's STORE SOUND SysEx for it (FMToneSeq docs/SYSEX.md, protocol 1): into the LIBRARY's first
// free slot, its name, its DRIVE, P1.5x its pan and sends (mixmask 0F: FIZZ keeps them with the preset) and its tags
// byte (when it has tags), the blob 7-bit packed; BATCH after the first of a batch (one undo on the unit).
#pragma once
#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <vector>

namespace fm1dnsnd {

constexpr int kNp = 73, kBlob = 96;
// where an imported sound goes in FIZZ: its LIBRARY (false) or the project's POOL (true), the first free slot either
// way. One place to change (FIZZ's presets and sounds are being redesigned; FMToneSeq docs/SYSEX.md has both)
constexpr bool kImportIntoPool = false;

struct Sound {
    juce::String name;                          // soundFromDnsnd's: printable, trimmed, 12 letters ("DIGITONE": none)
    int version = 0;                            // the record's: 1 or 5
    std::array<int, kNp> values{};              // FM TONE's values (version 5's units)
    std::array<uint8_t, kBlob> blob{};          // 'Q' 5, the values (eng_quad.c quad_pack)
    int drive = 0, pan = 0;                     // 0..127, -64..63 (not in the blob)
    int reverb = 0, delay = 0, chorus = 0;      // the sends, 0..127 (P1.5x: FIZZ keeps them with the preset)
    int tags = 0;                               // P1.5x: FIZZ's tags (bit k: BASS KEYS PLUCK PAD LEAD DRUMS FX); 0 none
    juce::StringArray notes;                    // what was lost or approximated
};

// the file's bytes -> the sound; false and err (one line) if it is not a Digitone sound this reader knows
bool parse(const void *data, size_t n, Sound &out, juce::String &err);
bool read(const juce::File &f, Sound &out, juce::String &err);   // (.dn2pst: parseDn2)
// P1.5x: a Digitone II preset (.dn2pst), its stored container, its body or a bare 359-byte record -> the sound
bool parseDn2(const void *data, size_t n, Sound &out, juce::String &err);

// FIZZ's STORE SOUND frame (F0 .. F7): kImportIntoPool's place, slot 0 (the first free); batch: joins the previous
// one's undo
std::vector<uint8_t> storeFrame(const Sound &s, bool batch);

}   // namespace fm1dnsnd
