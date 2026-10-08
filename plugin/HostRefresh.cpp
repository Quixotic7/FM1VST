// SPDX-License-Identifier: GPL-3.0-only
// See HostRefresh.h.
#include "HostRefresh.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include <pluginterfaces/vst/ivsteditcontroller.h>
#pragma clang diagnostic pop

// JUCE's VST3 wrapper passes its EditController::componentHandler (a Vst::IComponentHandler, which derives from
// FUnknown alone: the same pointer). No queryInterface: the interface ids are defined only in the VST3 target.
void Vst3HostRefresh::setIComponentHandler(Steinberg::FUnknown *h)
{
    if (h)
        h->addRef();
    if (handler_)
        handler_->release();
    handler_ = h;
}

Vst3HostRefresh::~Vst3HostRefresh()
{
    if (handler_)
        handler_->release();
}

bool Vst3HostRefresh::refresh()
{
    count_++;
    if (!handler_)
        return false;
    static_cast<Steinberg::Vst::IComponentHandler *>(handler_)->restartComponent(Steinberg::Vst::kParamValuesChanged);
    return true;
}
