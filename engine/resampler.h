// SPDX-License-Identifier: GPL-3.0-only
// A 4-point, 3rd-order Lagrange interpolator (stereo): the device's fixed 44.1 kHz stream to the host's rate
// (FM1-VST-PLAN.md 4.2; a first pass, r8brain later if quality asks for it).
//
// Time mapping: host frame k is the input stream at position k * in_rate / out_rate, exactly (frame 0 = input
// sample 0). The window is x[-1], x0, x1, x2 around the position x0 + frac, so producing an output needs the input
// two samples ahead of it: LOOKAHEAD. No anti-alias filtering on the way down (the device's output is already
// band-limited to ~20 kHz and the host rate is normally >= 44.1 kHz).
#pragma once
#include <cstdint>

class Lagrange4 {
public:
    static constexpr int LOOKAHEAD = 2;      // input samples needed beyond the output's position

    void reset(double in_rate, double out_rate)
    {
        step_ = in_rate / out_rate;
        frac_ = 0.0;
        primed_ = 0;
        for (int c = 0; c < 2; c++)
            for (int i = 0; i < 4; i++)
                h_[c][i] = 0.0f;
    }
    double step() const { return step_; }

    // how many input frames must be pushed before the next output can be taken
    int need() const
    {
        if (primed_ < 3)
            return 3 - primed_;              // x0 x1 x2 (x[-1] = 0 before the stream starts)
        return frac_ >= 1.0 ? 1 : 0;
    }
    void push(float l, float r)
    {
        shift(h_[0], l);
        shift(h_[1], r);
        if (primed_ < 3)
            primed_++;
        else
            frac_ -= 1.0;
    }
    // one output frame (call only when need() == 0)
    void take(float &l, float &r)
    {
        const float t = (float)frac_;
        l = interp(h_[0], t);
        r = interp(h_[1], t);
        frac_ += step_;
    }

private:
    static void shift(float *h, float v)
    {
        h[0] = h[1];
        h[1] = h[2];
        h[2] = h[3];
        h[3] = v;
    }
    // Lagrange through (-1, h0) (0, h1) (1, h2) (2, h3) at t in [0, 1)
    static float interp(const float *h, float t)
    {
        const float xm1 = h[0], x0 = h[1], x1 = h[2], x2 = h[3];
        const float c0 = -t * (t - 1.0f) * (t - 2.0f) * (1.0f / 6.0f);
        const float c1 = (t + 1.0f) * (t - 1.0f) * (t - 2.0f) * 0.5f;
        const float c2 = -(t + 1.0f) * t * (t - 2.0f) * 0.5f;
        const float c3 = (t + 1.0f) * t * (t - 1.0f) * (1.0f / 6.0f);
        return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
    }

    float h_[2][4] = {};
    double step_ = 1.0, frac_ = 0.0;
    int primed_ = 0;
};
