// SPDX-License-Identifier: GPL-3.0-only
// The emulated chip's clock choice and Auto's governor (see EmuClock.h; web/site/js/cpu.js).
#include "EmuClock.h"

#include <algorithm>

namespace emuclock {

bool validChoice(int c) { return std::find(kChoices.begin(), kChoices.end(), c) != kChoices.end(); }

int nextStep(int mhz)
{
    for (size_t i = 0; i + 1 < kAutoSteps.size(); i++)
        if (kAutoSteps[i] == mhz)
            return kAutoSteps[i + 1];
    return 0;
}

int mhzFor(int choice, int autoMhz)
{
    if (choice != kAuto && validChoice(choice))
        return choice;
    return std::find(kAutoSteps.begin(), kAutoSteps.end(), autoMhz) != kAutoSteps.end() ? autoMhz : kDeviceMhz;
}

std::string choiceName(int choice) { return choice == kAuto ? "Auto" : std::to_string(choice) + " MHz"; }

Governor::Verdict Governor::feed(double load, double lateMs, double now)
{
    if (done_ || now - t0_ < kGraceMs)
        return {};
    if (load > kHighLoad) {
        if (highSince_ < 0)
            highSince_ = now;
    } else {
        highSince_ = -1;
    }
    if (lateMs > 0)
        late_.emplace_back(now, lateMs);
    while (!late_.empty() && now - late_.front().first > kLateWindowMs)
        late_.pop_front();
    double lateSum = 0;
    for (const auto &l : late_)
        lateSum += l.second;
    const bool slow = (highSince_ >= 0 && now - highSince_ >= kHighForMs) || lateSum >= kLateMs;
    if (!slow)
        return {};
    done_ = true;
    Verdict v;
    const int next = choice_ == kAuto ? nextStep(mhz_) : 0;
    if (next)
        v.step = next;
    else
        v.warn = true;
    return v;
}

}   // namespace emuclock
