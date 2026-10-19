// SPDX-License-Identifier: GPL-3.0-only
// The emulated chip's clock (the fm1emu core's boot option "mhz": fm1-emu's instructions per emulated second, fixed at
// power-on, so a change reboots the firmware with its flash kept), as RemoteFM1's site chooses it (web/site/js/cpu.js).
// The FM-1 runs at about 320-360 MHz. The choice (per instance, in the plugin state): Auto, 240, 320, 360, 480 or 640
// MHz (320-360: the device's range; 480 and 640: an overclock, more headroom for heavy firmwares). Auto (the default)
// is 360 and steps down (360 -> 320 -> 240 -> 180 -> 120, the floor) when this computer can't keep up; it never steps up
// by itself (picking Auto again starts at 360). A manual pick never changes; it only warns.
//
// "Can't keep up" (Governor): the processor reports every half second the share of the audio time it spent rendering
// (load) and how much rendering ran past the audio it made (lateMs: blocks that took longer than they last, heard as
// dropouts). Too slow: load above 75 % for 2 s, or 250 ms late within 5 s. The first 4 s after a boot don't count (the
// boot and the JIT compiling the hot code). Offline rendering (a bounce) is never measured.
// Pure code, no JUCE: tests/hub_test.cpp runs it on fake timing.
#pragma once
#include <array>
#include <deque>
#include <string>
#include <utility>

namespace emuclock {

constexpr int kAuto = 0;                                   // a choice: 0 = Auto, else the MHz
constexpr std::array<int, 6> kChoices{kAuto, 240, 320, 360, 480, 640};
constexpr int kDeviceMhz = 360;                            // the default, and where Auto starts
constexpr int kDeviceLo = 320, kDeviceHi = 360;            // the device's range
constexpr std::array<int, 5> kAutoSteps{360, 320, 240, 180, 120};
constexpr double kGraceMs = 4000, kHighLoad = 0.75, kHighForMs = 2000, kLateMs = 250, kLateWindowMs = 5000;

bool validChoice(int c);
inline bool isOverclock(int mhz) { return mhz > kDeviceHi; }
inline bool inDeviceRange(int mhz) { return mhz >= kDeviceLo && mhz <= kDeviceHi; }
int nextStep(int mhz);                                     // Auto's next step down, 0 at the floor (or off the list)
// the clock a choice gives: a manual pick itself; Auto its stepped value when it is one of the steps, else 360
int mhzFor(int choice, int autoMhz);
std::string choiceName(int choice);                        // "Auto", "360 MHz"

// one per boot (Auto's or a manual pick's); feed() a report at time nowMs
class Governor {
public:
    Governor() = default;
    Governor(int choice, int mhz, double nowMs) : choice_(choice), mhz_(mhz), t0_(nowMs) {}
    struct Verdict {
        int step = 0;                                      // Auto: reboot at this clock
        bool warn = false;                                 // a manual pick, or Auto at its floor: tell the user, once
    };
    Verdict feed(double load, double lateMs, double nowMs);
    bool done() const { return done_; }
    int mhz() const { return mhz_; }
    int choice() const { return choice_; }

private:
    int choice_ = kAuto, mhz_ = kDeviceMhz;
    double t0_ = 0, highSince_ = -1;
    std::deque<std::pair<double, double>> late_;
    bool done_ = false;
};

}   // namespace emuclock
