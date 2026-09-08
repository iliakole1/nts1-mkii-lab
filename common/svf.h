/*
 *  svf.h — topology-preserving state variable filter (Zavalishin / Cytomic form).
 *
 *  Stable at audio-rate cutoff modulation, which is what a per-voice filter
 *  envelope needs. Coefficients are updated at control rate (once per block).
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class SVF {
 public:
  void reset() {
    ic1_ = 0.f;
    ic2_ = 0.f;
  }

  /* cutoff in Hz, res 0..1 (1 = self-oscillating-ish) */
  void setCutoff(float hz, float res) {
    const float f = clampf(20.f, hz, 0.47f * kSampleRate);
    const float g = tanf(kPi * f * kSampleRateRecip);
    const float k = 2.f - 1.98f * clampf(0.f, res, 1.f);
    a1_ = 1.f / (1.f + g * (g + k));
    a2_ = g * a1_;
    a3_ = g * a2_;
    k_ = k;
  }

  inline float lowpass(float in) {
    const float v3 = in - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = 2.f * v1 - ic1_;
    ic2_ = 2.f * v2 - ic2_;
    return v2;
  }

  inline float highpass(float in) {
    const float v3 = in - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = 2.f * v1 - ic1_;
    ic2_ = 2.f * v2 - ic2_;
    return in - k_ * v1 - v2;
  }

  inline float bandpass(float in) {
    const float v3 = in - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = 2.f * v1 - ic1_;
    ic2_ = 2.f * v2 - ic2_;
    return v1;
  }

 private:
  float ic1_ = 0.f, ic2_ = 0.f;
  float a1_ = 0.f, a2_ = 0.f, a3_ = 0.f, k_ = 2.f;
};

}  // namespace dsp
