// SPDX-License-Identifier: GPL-3.0-only
// The video recorder's file writer (VideoRecorder.mm, AVFoundation; VideoCapture.h drives it): AVAssetWriter with
//   - one video track: H.264 (VideoToolbox, the hardware encoder on Apple silicon), High profile, from 32-bit BGRA
//     frames, tagged BT.709; the bitrate by size and rate (defaultBitrate: 8 Mbps at 1080p30, 12 at 1080p60, 5 / 7.5 at
//     720p, the square LCD sizes by their pixel count); a key frame every two seconds;
//   - one or two audio tracks: AAC-LC, stereo, 48 kHz (from the device's rate: AVAssetWriter converts), 256 kbps each,
//     fed float32 stereo; the second (the mic, "Separate") is a track of its own, enabled, so an editor shows both;
//   - .mp4 (the default) or .mov; movie fragments every two seconds (movieFragmentInterval), so a take cut short by a
//     crash, a kill or a full disk is readable up to its last fragment; finish() writes the complete movie.
// Every time is on one timeline: a video frame k at k / fps, an audio block at its first frame / the device's rate
// (VideoCapture.h "SYNC"); the session starts at 0 and ends at the take's last audio frame.
// Not thread-safe: one thread (the recorder's) calls everything after open().
// Elsewhere than macOS every call fails ("not supported").
#pragma once
#include <juce_core/juce_core.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace fm1video {

struct EncoderSpec {
    juce::File file;
    bool mov = false;                            // QuickTime (.mov); else MPEG-4 (.mp4)
    int width = 1280, height = 720, fps = 30;    // even sizes
    int videoBitrate = 0;                        // bits/s; 0: defaultBitrate
    double inputRate = 48000;                    // the timeline's (the audio device's) rate
    int audioRate = 48000;                       // the AAC tracks' rate
    int audioTracks = 1;                         // 1 or 2
    int audioBitrate = 256000;                   // per track
    double fragmentSeconds = 2.0;                // movie fragments (0: none)
};

int defaultBitrate(int w, int h, int fps);

class Encoder {
public:
    Encoder();
    ~Encoder();                                  // an open file is finished
    bool open(const EncoderSpec &s, juce::String &error);
    bool isOpen() const;
    // frame k (shown from k / fps), BGRA rows top to bottom; waits up to waitMs for the input; false: not written
    bool addVideo(int64_t k, const uint8_t *bgra, int stride, int waitMs = 250);
    // interleaved stereo float at timeline frame `at`; false: not written (failed, or the input stayed busy)
    bool addAudio(int track, int64_t at, const float *interleaved, int frames, int waitMs = 250);
    bool audioReady(int track) const;
    // ends the session at timeline frame `end`, writes the movie (blocks until done)
    bool finish(int64_t end, juce::String &error);
    // the writer stopped (a full disk, an I/O error): why
    bool failed() const;
    juce::String error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// ---- the microphone permission (macOS: AVCaptureDevice; asked for only when the user turns the mic on)
enum class MicAccess { granted, denied, undetermined, restricted, unsupported };
MicAccess micAccess();
void requestMicAccess(std::function<void(bool granted)> done);   // done on the message thread

// ---- reading a file back (tests: AVAssetReader)
struct Probe {
    bool ok = false;
    juce::String error;
    double duration = 0;                         // the movie's, seconds
    int videoTracks = 0, audioTracks = 0;
    int width = 0, height = 0;
    double videoDuration = 0, nominalFps = 0;
    int64_t videoFrames = 0;                     // samples in the video track
    juce::String videoCodec;                     // "avc1"
    std::vector<double> audioDurations;
    std::vector<int> audioRates, audioChannels;
    std::vector<juce::String> audioCodecs;       // "aac "
};
Probe probe(const juce::File &f);
// every video frame decoded to BGRA, in order: cb(pts seconds, pixels, w, h, stride); cb false stops
bool decodeVideo(const juce::File &f, const std::function<bool(double, const uint8_t *, int, int, int)> &cb, juce::String &error);
// audio track i (0-based) decoded to float stereo interleaved at its own rate; its first sample's time (s)
bool decodeAudio(const juce::File &f, int track, std::vector<float> &out, int &rate, double &start, juce::String &error);

}   // namespace fm1video
