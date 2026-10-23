// SPDX-License-Identifier: GPL-3.0-only
// The video recorder's audio capture (VideoCapture.h): a lock-free stereo ring, one producer (the audio thread), one
// consumer (the recorder's thread). Nothing on the producer's side allocates, locks or waits.
//
// THE TIMELINE. arm() asks for a take; the producer's first push after it starts the take's timeline at frame 0 (the
// block's first sample), and every later frame is one more on it: produced() is the take's length in device frames, the
// single clock the recorder hangs the video and every audio track on (VideoCapture.h "SYNC"). push() appends a block;
// pushAt(at, ..) puts a block at timeline frame `at` (a gap before it filled with silence, an overlap with what is
// already there dropped): the mic input places its blocks on the main tap's timeline that way.
//
// THE END. disarm() (the consumer) ends the take: pushes are ignored from then on, and produced() read after it is the
// take's end. freeze(reason) (a power-on, a device restart: the processor) does the same from the other side, before any
// sample of the new state lands, and remembers why. A full ring (the consumer stalled for capacity() frames) also stops
// the take (overflowed()): the recorder ends the take there rather than leave a hole in it.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

class AudioTap {
public:
    static constexpr int kDefaultCapacity = 1 << 19;     // frames: 10.9 s at 48 kHz (4 MiB)
    enum Reason { kNone = 0, kPowerOn = 1, kDevice = 2 };

    // ---- the consumer (allocate and arm only while disarmed)
    void allocate(int capacityFrames = kDefaultCapacity)
    {
        int c = 1024;
        while (c < capacityFrames)
            c <<= 1;
        if ((int)buf_.size() != c * 2) {
            buf_.assign((size_t)c * 2, 0.0f);
            cap_ = c;
        }
    }
    int capacity() const { return cap_; }
    void arm()
    {
        if (cap_ <= 0)
            allocate();
        overflow_.store(false);
        reason_.store(kNone);
        started_.store(false);
        init_ = false;
        state_.store(kArmReq, std::memory_order_release);
    }
    void disarm() { state_.store(kOff, std::memory_order_release); }
    // forget the last take (produced() -1 until the next one starts); not while armed
    void clear()
    {
        if (!armed())
            started_.store(false);
    }
    bool armed() const
    {
        const int s = state_.load(std::memory_order_acquire);
        return s == kArmReq || s == kRunning;
    }
    bool running() const { return state_.load(std::memory_order_acquire) == kRunning; }
    // (any thread) the take ends here; a no-op unless armed
    void freeze(Reason why)
    {
        int s = state_.load();
        while ((s == kArmReq || s == kRunning) && !state_.compare_exchange_weak(s, kFrozen))
            ;
        if (s == kArmReq || s == kRunning)
            reason_.store(why);
    }
    bool frozen() const { return state_.load(std::memory_order_acquire) == kFrozen; }
    Reason reason() const { return (Reason)reason_.load(); }
    bool overflowed() const { return overflow_.load(std::memory_order_acquire); }
    // the take's frames so far (-1: its first block has not come yet)
    int64_t produced() const
    {
        if (!started_.load(std::memory_order_acquire))
            return -1;
        return w_.load(std::memory_order_acquire) - start_.load(std::memory_order_relaxed);
    }
    // the take's frames the consumer has taken
    int64_t consumed() { return sync() ? r_ - start_.load(std::memory_order_relaxed) : 0; }
    // up to n frames from consumed() on, interleaved stereo; the frames copied
    int read(float *interleaved, int n)
    {
        if (!sync())
            return 0;
        const int64_t w = w_.load(std::memory_order_acquire);
        const int k = (int)std::max<int64_t>(0, std::min<int64_t>(n, w - r_));
        for (int i = 0; i < k; i++) {
            const size_t at = (size_t)((r_ + i) & (cap_ - 1)) * 2;
            interleaved[2 * i] = buf_[at];
            interleaved[2 * i + 1] = buf_[at + 1];
        }
        r_ += k;
        rp_.store(r_, std::memory_order_release);
        return k;
    }

    // ---- the producer (the audio thread). R null: L on both sides
    void push(const float *L, const float *R, int n, float gain = 1.0f) noexcept
    {
        if (n <= 0 || !begin())
            return;
        write(L, R, n, gain);
    }
    // a block that belongs at timeline frame `at` (L null: silence)
    void pushAt(int64_t at, const float *L, const float *R, int n, float gain = 1.0f) noexcept
    {
        if (n <= 0 || at + n <= 0 || !begin())
            return;
        int64_t have = w_.load(std::memory_order_relaxed) - start_.load(std::memory_order_relaxed);
        if (at < 0) {                            // (partly before the take began: the rest only)
            const int drop = (int)-at;
            L = L ? L + drop : nullptr, R = R ? R + drop : nullptr, n -= drop, at = 0;
        }
        while (at > have && state_.load(std::memory_order_relaxed) == kRunning) {   // a gap: silence up to `at`
            const int k = (int)std::min<int64_t>(at - have, 4096);
            write(nullptr, nullptr, k, 0.0f);
            have += k;
        }
        if (at < have) {                         // an overlap: what is there already stays
            const int64_t drop = have - at;
            if (drop >= n)
                return;
            L = L ? L + drop : nullptr, R = R ? R + drop : nullptr, n -= (int)drop;
        }
        if (state_.load(std::memory_order_relaxed) == kRunning)
            write(L, R, n, gain);
    }

private:
    enum { kOff = 0, kArmReq = 1, kRunning = 2, kFrozen = 3 };
    // (consumer) the take has begun; the read position taken from the producer's start the first time
    bool sync()
    {
        if (!started_.load(std::memory_order_acquire))
            return false;
        if (!init_) {
            r_ = start_.load(std::memory_order_relaxed);
            init_ = true;
        }
        return true;
    }
    // (producer) false: not taking samples. The first push after arm() starts the take at the current write position
    bool begin() noexcept
    {
        int s = state_.load(std::memory_order_acquire);
        if (s == kArmReq && cap_ > 0) {
            const int64_t w = w_.load(std::memory_order_relaxed);
            start_.store(w, std::memory_order_relaxed);
            rp_.store(w, std::memory_order_relaxed);
            started_.store(true, std::memory_order_release);
            if (state_.compare_exchange_strong(s, kRunning))
                s = kRunning;
        }
        return s == kRunning && cap_ > 0;
    }
    void write(const float *L, const float *R, int n, float gain) noexcept
    {
        const int64_t w = w_.load(std::memory_order_relaxed);
        if (w + n - rp_.load(std::memory_order_acquire) > cap_) {   // full: the take ends here
            overflow_.store(true, std::memory_order_release);
            int s = kRunning;
            state_.compare_exchange_strong(s, kFrozen);
            return;
        }
        for (int i = 0; i < n; i++) {
            const size_t at = (size_t)((w + i) & (cap_ - 1)) * 2;
            const float l = L ? L[i] * gain : 0.0f;
            buf_[at] = l;
            buf_[at + 1] = R ? R[i] * gain : l;
        }
        w_.store(w + n, std::memory_order_release);
    }

    std::vector<float> buf_;
    int cap_ = 0;
    std::atomic<int> state_{kOff}, reason_{kNone};
    std::atomic<bool> overflow_{false}, started_{false};
    std::atomic<int64_t> w_{0}, start_{0}, rp_{0};
    int64_t r_ = 0;                              // (the consumer's own)
    bool init_ = false;
};

// The video recorder's pictures (VideoCapture.h "SYNC"): what the frames show, captured on the audio thread at the
// frames' instants, so a frame's content is tied to the timeline however late it is drawn. After each block the
// producer (the audio thread) is given the timeline's length and the state now (the panel's snapshot); every frame whose
// instant k * rate / fps the block passed gets that state: one slot for frames [k0, k1) (a block longer than a frame
// covers several). Lock-free, one producer, one consumer (the recorder's thread); the slots are made by allocate() on
// the consumer's side, never on the audio thread. A full ring drops the newest frames (dropped()): the consumer
// repeats the last picture it drew for them, never the state "now".
template <class T> class SnapshotRing {
public:
    struct Slot {
        int64_t k0 = 0, k1 = 0;                  // the frames [k0, k1) show `state`
        T state{};
    };
    // ---- the consumer (allocate and arm only while the tap is not running)
    void allocate(int slots)
    {
        if ((int)slots_.size() != std::max(2, slots))
            slots_.assign((size_t)std::max(2, slots), Slot{});
    }
    int capacity() const { return (int)slots_.size(); }
    void arm(int64_t rate, int fps)
    {
        if (slots_.empty())
            allocate(64);
        rate_.store(rate);
        fps_.store(fps);
        r_ = w_.load(std::memory_order_acquire);   // (nothing of the last take)
        rp_.store(r_, std::memory_order_release);
        dropped_.store(0);
        gen_.fetch_add(1, std::memory_order_release);
    }
    // the oldest slot not taken yet (nullptr none); pop() when done with it
    const Slot *front() const { return r_ < w_.load(std::memory_order_acquire) ? &slots_[(size_t)(r_ % (int64_t)slots_.size())] : nullptr; }
    void pop()
    {
        if (r_ < w_.load(std::memory_order_acquire))
            rp_.store(++r_, std::memory_order_release);
    }
    int backlog() const { return (int)(w_.load(std::memory_order_acquire) - r_); }
    int64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }   // frames the producer had no room for

    // ---- the producer (the audio thread, after a block): the timeline's length now, the state now
    void capture(const T &state, int64_t produced) noexcept
    {
        if (slots_.empty() || produced <= 0)
            return;
        const int64_t g = gen_.load(std::memory_order_acquire);
        if (g != seenGen_) {                     // (a new take)
            seenGen_ = g;
            nextK_ = 0;
        }
        const int64_t rate = rate_.load(std::memory_order_relaxed), fps = fps_.load(std::memory_order_relaxed);
        if (rate <= 0 || fps <= 0)
            return;
        const int64_t kEnd = (produced * fps + rate - 1) / rate;   // frames whose instant is before `produced`
        if (kEnd <= nextK_)
            return;
        const int64_t w = w_.load(std::memory_order_relaxed);
        if (w - rp_.load(std::memory_order_acquire) >= (int64_t)slots_.size()) {
            dropped_.fetch_add(kEnd - nextK_, std::memory_order_relaxed);
        } else {
            Slot &s = slots_[(size_t)(w % (int64_t)slots_.size())];
            s.k0 = nextK_;
            s.k1 = kEnd;
            s.state = state;
            w_.store(w + 1, std::memory_order_release);
        }
        nextK_ = kEnd;
    }

private:
    std::vector<Slot> slots_;
    std::atomic<int64_t> w_{0}, rp_{0}, rate_{0}, gen_{0}, dropped_{0};
    std::atomic<int> fps_{0};
    int64_t r_ = 0;                              // (the consumer's own)
    int64_t nextK_ = 0, seenGen_ = 0;            // (the producer's own)
};
