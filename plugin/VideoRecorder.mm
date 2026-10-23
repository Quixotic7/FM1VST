// SPDX-License-Identifier: GPL-3.0-only
// The video recorder's file writer (see VideoRecorder.h): AVAssetWriter, and AVAssetReader for the tests. Built with
// ARC (CMakeLists.txt: -fobjc-arc for this file only); every AVFoundation call that can raise is inside @try.
#include "VideoRecorder.h"

#include <juce_events/juce_events.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <chrono>
#include <cstring>
#include <thread>

#if !__has_feature(objc_arc)
#error "VideoRecorder.mm is built with -fobjc-arc"
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   // (tracksWithMediaType: the synchronous form, fine here)

namespace fm1video {

static juce::String str(NSString *s) { return s ? juce::String::fromUTF8(s.UTF8String) : juce::String(); }
static juce::String why(NSError *e)
{
    if (!e)
        return "unknown error";
    juce::String t = str(e.localizedDescription);
    if (NSString *r = e.localizedFailureReason)
        t << " (" << str(r) << ")";
    else if (NSError *u = e.userInfo[NSUnderlyingErrorKey])
        t << " (" << str(u.localizedDescription) << ")";
    return t;
}
static NSString *_Nonnull nsPath(const juce::File &f)
{
    NSString *s = [NSString stringWithUTF8String:f.getFullPathName().toRawUTF8()];
    return s != nil ? s : @"";
}
static juce::String fourcc(FourCharCode c)
{
    char s[5] = {(char)(c >> 24), (char)(c >> 16), (char)(c >> 8), (char)c, 0};
    return s;
}

int defaultBitrate(int w, int h, int fps)
{
    // 8 Mbps for 1920 x 1080 at 30 fps, scaled by pixels; 60 fps x 1.5
    const double px = (double)w * h / (1920.0 * 1080.0);
    const double b = 8.0e6 * px * (fps > 30 ? 1.5 : 1.0);
    return juce::jlimit(1500000, 40000000, (int)(b + 0.5));
}

// ================================================================ writer ===
struct Encoder::Impl {
    EncoderSpec spec;
    AVAssetWriter *writer = nil;
    AVAssetWriterInput *video = nil;
    AVAssetWriterInputPixelBufferAdaptor *adaptor = nil;
    NSMutableArray<AVAssetWriterInput *> *audio = nil;
    CMAudioFormatDescriptionRef pcm = nullptr;
    bool open = false, finished = false;
    int64_t lastEnd = 0;
    juce::String err;

    ~Impl()
    {
        if (pcm)
            CFRelease(pcm);
    }
    bool writing() const { return writer && writer.status == AVAssetWriterStatusWriting; }
    bool waitReady(AVAssetWriterInput *in, int waitMs)
    {
        const auto t0 = std::chrono::steady_clock::now();
        while (!in.readyForMoreMediaData) {
            if (!writing())
                return false;
            if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(waitMs))
                return false;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        return writing();
    }
    void noteFailure()
    {
        if (writer && writer.status == AVAssetWriterStatusFailed && err.isEmpty())
            err = why(writer.error);
    }
};

Encoder::Encoder() : d_(std::make_unique<Impl>()) {}

Encoder::~Encoder()
{
    if (d_->open && !d_->finished) {
        juce::String e;
        finish(d_->lastEnd, e);
    }
}

bool Encoder::isOpen() const { return d_->open && !d_->finished; }
bool Encoder::failed() const
{
    d_->noteFailure();
    return d_->err.isNotEmpty();
}
juce::String Encoder::error() const
{
    d_->noteFailure();
    return d_->err;
}

bool Encoder::open(const EncoderSpec &s, juce::String &error)
{
    Impl &d = *d_;
    d.spec = s;
    if (s.width <= 0 || s.height <= 0 || (s.width & 1) || (s.height & 1) || s.fps <= 0 || s.inputRate <= 0 ||
        s.audioTracks < 1 || s.audioTracks > 2) {
        error = "bad video settings";
        return false;
    }
    @autoreleasepool {
        @try {
            s.file.deleteFile();
            NSURL *url = [NSURL fileURLWithPath:nsPath(s.file)];
            NSError *e = nil;
            d.writer = [AVAssetWriter assetWriterWithURL:url fileType:(s.mov ? AVFileTypeQuickTimeMovie : AVFileTypeMPEG4) error:&e];
            if (!d.writer) {
                error = why(e);
                return false;
            }
            d.writer.shouldOptimizeForNetworkUse = NO;
            if (s.fragmentSeconds > 0)
                d.writer.movieFragmentInterval = CMTimeMakeWithSeconds(s.fragmentSeconds, 600);

            // the video: H.264 from BGRA, BT.709
            const int vbr = s.videoBitrate > 0 ? s.videoBitrate : defaultBitrate(s.width, s.height, s.fps);
            NSDictionary *vs = @{
                AVVideoCodecKey : AVVideoCodecTypeH264,
                AVVideoWidthKey : @(s.width),
                AVVideoHeightKey : @(s.height),
                AVVideoCompressionPropertiesKey : @{
                    AVVideoAverageBitRateKey : @(vbr),
                    AVVideoExpectedSourceFrameRateKey : @(s.fps),
                    AVVideoMaxKeyFrameIntervalKey : @(s.fps * 2),
                    AVVideoProfileLevelKey : AVVideoProfileLevelH264HighAutoLevel,
                },
                AVVideoColorPropertiesKey : @{
                    AVVideoColorPrimariesKey : AVVideoColorPrimaries_ITU_R_709_2,
                    AVVideoTransferFunctionKey : AVVideoTransferFunction_ITU_R_709_2,
                    AVVideoYCbCrMatrixKey : AVVideoYCbCrMatrix_ITU_R_709_2,
                },
            };
            d.video = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:vs];
            d.video.expectsMediaDataInRealTime = YES;
            d.video.mediaTimeScale = (CMTimeScale)(s.fps * 100);
            d.adaptor = [AVAssetWriterInputPixelBufferAdaptor
                assetWriterInputPixelBufferAdaptorWithAssetWriterInput:d.video
                                           sourcePixelBufferAttributes:@{
                                               (NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                                               (NSString *)kCVPixelBufferWidthKey : @(s.width),
                                               (NSString *)kCVPixelBufferHeightKey : @(s.height),
                                               (NSString *)kCVPixelBufferIOSurfacePropertiesKey : @{},
                                           }];
            if (![d.writer canAddInput:d.video]) {
                error = "the H.264 encoder refused " + juce::String(s.width) + " x " + juce::String(s.height);
                return false;
            }
            [d.writer addInput:d.video];

            // the audio: float32 stereo in at the device's rate, AAC out
            AudioChannelLayout acl{};
            acl.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
            AudioStreamBasicDescription asbd{};
            asbd.mSampleRate = s.inputRate;
            asbd.mFormatID = kAudioFormatLinearPCM;
            asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
            asbd.mBytesPerPacket = 8;
            asbd.mFramesPerPacket = 1;
            asbd.mBytesPerFrame = 8;
            asbd.mChannelsPerFrame = 2;
            asbd.mBitsPerChannel = 32;
            if (CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, sizeof(acl), &acl, 0, nullptr, nullptr, &d.pcm) != noErr) {
                error = "no audio format";
                return false;
            }
            NSMutableDictionary *as = [@{
                AVFormatIDKey : @(kAudioFormatMPEG4AAC),
                AVSampleRateKey : @(s.audioRate),
                AVNumberOfChannelsKey : @2,
                AVEncoderBitRateKey : @(s.audioBitrate),
                AVChannelLayoutKey : [NSData dataWithBytes:&acl length:sizeof(acl)],
            } mutableCopy];
            if ((int)(s.inputRate + 0.5) != s.audioRate)
                as[AVSampleRateConverterAudioQualityKey] = @(AVAudioQualityMax);
            d.audio = [NSMutableArray array];
            for (int t = 0; t < s.audioTracks; t++) {
                AVAssetWriterInput *a = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:as
                                                                         sourceFormatHint:d.pcm];
                a.expectsMediaDataInRealTime = YES;
                if (![d.writer canAddInput:a]) {
                    error = "the AAC encoder refused the audio settings";
                    return false;
                }
                [d.writer addInput:a];
                [d.audio addObject:a];
            }
            if (![d.writer startWriting]) {
                error = why(d.writer.error);
                return false;
            }
            [d.writer startSessionAtSourceTime:kCMTimeZero];
        } @catch (NSException *x) {
            error = "AVFoundation: " + str(x.reason);
            return false;
        }
    }
    d.open = true;
    return true;
}

bool Encoder::addVideo(int64_t k, const uint8_t *bgra, int stride, int waitMs)
{
    Impl &d = *d_;
    if (!d.open || d.finished)
        return false;
    bool ok = false;
    @autoreleasepool {
        @try {
            if (!d.waitReady(d.video, waitMs)) {
                d.noteFailure();
                return false;
            }
            CVPixelBufferPoolRef pool = d.adaptor.pixelBufferPool;
            CVPixelBufferRef pb = nullptr;
            if (!pool || CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &pb) != kCVReturnSuccess || !pb) {
                d.noteFailure();
                return false;
            }
            CVPixelBufferLockBaseAddress(pb, 0);
            uint8_t *dst = (uint8_t *)CVPixelBufferGetBaseAddress(pb);
            const size_t dstStride = CVPixelBufferGetBytesPerRow(pb);
            const size_t row = (size_t)d.spec.width * 4;
            for (int y = 0; y < d.spec.height; y++)
                std::memcpy(dst + (size_t)y * dstStride, bgra + (size_t)y * (size_t)stride, row);
            CVPixelBufferUnlockBaseAddress(pb, 0);
            ok = [d.adaptor appendPixelBuffer:pb withPresentationTime:CMTimeMake(k, d.spec.fps)];
            CVPixelBufferRelease(pb);
        } @catch (NSException *x) {
            if (d.err.isEmpty())
                d.err = "AVFoundation: " + str(x.reason);
            ok = false;
        }
    }
    if (!ok)
        d.noteFailure();
    return ok;
}

bool Encoder::audioReady(int track) const
{
    const Impl &d = *d_;
    return d.open && !d.finished && track >= 0 && track < (int)d.audio.count && d.audio[(NSUInteger)track].readyForMoreMediaData;
}

bool Encoder::addAudio(int track, int64_t at, const float *interleaved, int frames, int waitMs)
{
    Impl &d = *d_;
    if (!d.open || d.finished || track < 0 || track >= (int)d.audio.count || frames <= 0)
        return false;
    bool ok = false;
    @autoreleasepool {
        @try {
            AVAssetWriterInput *in = d.audio[(NSUInteger)track];
            if (!d.waitReady(in, waitMs)) {
                d.noteFailure();
                return false;
            }
            const size_t bytes = (size_t)frames * 8;
            CMBlockBufferRef bb = nullptr;
            if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault, nullptr, 0, bytes,
                                                   kCMBlockBufferAssureMemoryNowFlag, &bb) != kCMBlockBufferNoErr)
                return false;
            CMBlockBufferReplaceDataBytes(interleaved, bb, 0, bytes);
            CMSampleBufferRef sb = nullptr;
            const OSStatus st = CMAudioSampleBufferCreateReadyWithPacketDescriptions(
                kCFAllocatorDefault, bb, d.pcm, frames, CMTimeMake(at, (int32_t)(d.spec.inputRate + 0.5)), nullptr, &sb);
            CFRelease(bb);
            if (st != noErr || !sb)
                return false;
            ok = [in appendSampleBuffer:sb];
            CFRelease(sb);
        } @catch (NSException *x) {
            if (d.err.isEmpty())
                d.err = "AVFoundation: " + str(x.reason);
            ok = false;
        }
    }
    if (!ok)
        d.noteFailure();
    else
        d.lastEnd = std::max(d.lastEnd, at + frames);
    return ok;
}

bool Encoder::finish(int64_t end, juce::String &error)
{
    Impl &d = *d_;
    if (!d.open || d.finished) {
        error = d.err;
        return d.err.isEmpty();
    }
    d.finished = true;
    bool ok = false;
    @autoreleasepool {
        @try {
            if (d.writing()) {
                [d.video markAsFinished];
                for (AVAssetWriterInput *a in d.audio)
                    [a markAsFinished];
                [d.writer endSessionAtSourceTime:CMTimeMake(end, (int32_t)(d.spec.inputRate + 0.5))];
                dispatch_semaphore_t done = dispatch_semaphore_create(0);
                [d.writer finishWritingWithCompletionHandler:^{
                  dispatch_semaphore_signal(done);
                }];
                dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)60 * NSEC_PER_SEC));
                ok = d.writer.status == AVAssetWriterStatusCompleted;
            }
            // (a failed writer is left as it is: cancelWriting would delete what it wrote; the fragments stay readable)
        } @catch (NSException *x) {
            if (d.err.isEmpty())
                d.err = "AVFoundation: " + str(x.reason);
        }
        d.noteFailure();
        d.writer = nil;
        d.video = nil;
        d.adaptor = nil;
        d.audio = nil;
    }
    if (!ok && d.err.isEmpty())
        d.err = "the movie could not be finished";
    error = d.err;
    return ok;
}

// ============================================================ permission ===
MicAccess micAccess()
{
    if (@available(macOS 10.14, *)) {
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusAuthorized: return MicAccess::granted;
        case AVAuthorizationStatusDenied: return MicAccess::denied;
        case AVAuthorizationStatusRestricted: return MicAccess::restricted;
        case AVAuthorizationStatusNotDetermined: return MicAccess::undetermined;
        }
    }
    return MicAccess::granted;
}

void requestMicAccess(std::function<void(bool)> done)
{
    if (@available(macOS 10.14, *)) {
        // (the system's prompt: the Info.plist's NSMicrophoneUsageDescription; without it macOS would end the app)
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL granted) {
                                   auto cb = done;
                                   juce::MessageManager::callAsync([cb, granted] {
                                       if (cb)
                                           cb(granted);
                                   });
                                 }];
        return;
    }
    juce::MessageManager::callAsync([done] {
        if (done)
            done(true);
    });
}

// ============================================================ read back ===
static AVURLAsset *assetOf(const juce::File &f)
{
    NSURL *url = [NSURL fileURLWithPath:nsPath(f)];
    return [AVURLAsset URLAssetWithURL:url options:@{AVURLAssetPreferPreciseDurationAndTimingKey : @YES}];
}

Probe probe(const juce::File &f)
{
    Probe p;
    if (!f.existsAsFile()) {
        p.error = "no file";
        return p;
    }
    @autoreleasepool {
        @try {
            AVURLAsset *a = assetOf(f);
            p.duration = CMTimeGetSeconds(a.duration);
            NSArray<AVAssetTrack *> *vt = [a tracksWithMediaType:AVMediaTypeVideo];
            NSArray<AVAssetTrack *> *at = [a tracksWithMediaType:AVMediaTypeAudio];
            p.videoTracks = (int)vt.count;
            p.audioTracks = (int)at.count;
            if (vt.count > 0) {
                AVAssetTrack *v = vt[0];
                p.width = (int)v.naturalSize.width;
                p.height = (int)v.naturalSize.height;
                p.nominalFps = v.nominalFrameRate;
                p.videoDuration = CMTimeGetSeconds(v.timeRange.duration);
                if (v.formatDescriptions.count > 0)
                    p.videoCodec = fourcc(CMFormatDescriptionGetMediaSubType((__bridge CMFormatDescriptionRef)v.formatDescriptions[0]));
                NSError *e = nil;
                AVAssetReader *r = [AVAssetReader assetReaderWithAsset:a error:&e];
                AVAssetReaderTrackOutput *o = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:v outputSettings:nil];
                o.alwaysCopiesSampleData = NO;
                if (r && [r canAddOutput:o]) {
                    [r addOutput:o];
                    [r startReading];
                    while (true) {
                        CMSampleBufferRef sb = [o copyNextSampleBuffer];
                        if (!sb)
                            break;
                        p.videoFrames += CMSampleBufferGetNumSamples(sb);
                        CFRelease(sb);
                    }
                }
            }
            for (AVAssetTrack *t in at) {
                p.audioDurations.push_back(CMTimeGetSeconds(t.timeRange.duration));
                int rate = 0, ch = 0;
                juce::String codec;
                if (t.formatDescriptions.count > 0) {
                    CMAudioFormatDescriptionRef fd = (__bridge CMAudioFormatDescriptionRef)t.formatDescriptions[0];
                    if (const AudioStreamBasicDescription *b = CMAudioFormatDescriptionGetStreamBasicDescription(fd)) {
                        rate = (int)b->mSampleRate;
                        ch = (int)b->mChannelsPerFrame;
                        codec = fourcc(b->mFormatID);
                    }
                }
                p.audioRates.push_back(rate);
                p.audioChannels.push_back(ch);
                p.audioCodecs.push_back(codec);
            }
            p.ok = true;
        } @catch (NSException *x) {
            p.error = "AVFoundation: " + str(x.reason);
        }
    }
    return p;
}

bool decodeVideo(const juce::File &f, const std::function<bool(double, const uint8_t *, int, int, int)> &cb, juce::String &error)
{
    @autoreleasepool {
        @try {
            AVURLAsset *a = assetOf(f);
            NSArray<AVAssetTrack *> *vt = [a tracksWithMediaType:AVMediaTypeVideo];
            if (vt.count == 0) {
                error = "no video track";
                return false;
            }
            NSError *e = nil;
            AVAssetReader *r = [AVAssetReader assetReaderWithAsset:a error:&e];
            if (!r) {
                error = why(e);
                return false;
            }
            AVAssetReaderTrackOutput *o = [AVAssetReaderTrackOutput
                assetReaderTrackOutputWithTrack:vt[0]
                                 outputSettings:@{(NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)}];
            o.alwaysCopiesSampleData = NO;
            [r addOutput:o];
            if (![r startReading]) {
                error = why(r.error);
                return false;
            }
            bool go = true;
            while (go) {
                @autoreleasepool {
                    CMSampleBufferRef sb = [o copyNextSampleBuffer];
                    if (!sb)
                        break;
                    CVImageBufferRef ib = CMSampleBufferGetImageBuffer(sb);
                    const double t = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
                    if (ib) {
                        CVPixelBufferLockBaseAddress(ib, kCVPixelBufferLock_ReadOnly);
                        go = cb(t, (const uint8_t *)CVPixelBufferGetBaseAddress(ib), (int)CVPixelBufferGetWidth(ib),
                                (int)CVPixelBufferGetHeight(ib), (int)CVPixelBufferGetBytesPerRow(ib));
                        CVPixelBufferUnlockBaseAddress(ib, kCVPixelBufferLock_ReadOnly);
                    }
                    CFRelease(sb);
                }
            }
            if (r.status == AVAssetReaderStatusFailed) {
                error = why(r.error);
                return false;
            }
            return true;
        } @catch (NSException *x) {
            error = "AVFoundation: " + str(x.reason);
            return false;
        }
    }
}

bool decodeAudio(const juce::File &f, int track, std::vector<float> &out, int &rate, double &start, juce::String &error)
{
    out.clear();
    rate = 0;
    start = 0;
    @autoreleasepool {
        @try {
            AVURLAsset *a = assetOf(f);
            NSArray<AVAssetTrack *> *at = [a tracksWithMediaType:AVMediaTypeAudio];
            if (track < 0 || track >= (int)at.count) {
                error = "no audio track " + juce::String(track);
                return false;
            }
            NSError *e = nil;
            AVAssetReader *r = [AVAssetReader assetReaderWithAsset:a error:&e];
            if (!r) {
                error = why(e);
                return false;
            }
            AVAssetReaderTrackOutput *o = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:at[(NSUInteger)track]
                                                                                     outputSettings:@{
                                                                                         AVFormatIDKey : @(kAudioFormatLinearPCM),
                                                                                         AVLinearPCMIsFloatKey : @YES,
                                                                                         AVLinearPCMBitDepthKey : @32,
                                                                                         AVLinearPCMIsNonInterleaved : @NO,
                                                                                         AVLinearPCMIsBigEndianKey : @NO,
                                                                                         AVNumberOfChannelsKey : @2,
                                                                                     }];
            [r addOutput:o];
            if (![r startReading]) {
                error = why(r.error);
                return false;
            }
            bool first = true;
            while (true) {
                @autoreleasepool {
                    CMSampleBufferRef sb = [o copyNextSampleBuffer];
                    if (!sb)
                        break;
                    if (first) {
                        start = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
                        if (CMFormatDescriptionRef fd = CMSampleBufferGetFormatDescription(sb))
                            if (const AudioStreamBasicDescription *b = CMAudioFormatDescriptionGetStreamBasicDescription(fd))
                                rate = (int)b->mSampleRate;
                        first = false;
                    }
                    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
                    const size_t len = bb ? CMBlockBufferGetDataLength(bb) : 0;
                    const size_t at0 = out.size();
                    out.resize(at0 + len / sizeof(float));
                    if (len)
                        CMBlockBufferCopyDataBytes(bb, 0, len, out.data() + at0);
                    CFRelease(sb);
                }
            }
            if (r.status == AVAssetReaderStatusFailed) {
                error = why(r.error);
                return false;
            }
            return true;
        } @catch (NSException *x) {
            error = "AVFoundation: " + str(x.reason);
            return false;
        }
    }
}

}   // namespace fm1video

#pragma clang diagnostic pop
