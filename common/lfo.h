/*
 *  lfo.h — cheap sine LFO (parabolic approximation, ~0.6% error).
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class SineLfo {
 public:
  void init(float phase01 = 0.f) {
    phase_ = phase01;
    inc_ = 0.f;
  }

  void setRate(float hz) { inc_ = hz * kSampleRateRecip; }

  inline float next() {
    phase_ += inc_;
    if (phase_ >= 1.f) phase_ -= 1.f;
    return value();
  }

  /* -1..1 */
  inline float value() const {
    const float t = phase_ * 2.f - 1.f;          // -1..1 ramp
    const float y = 4.f * t * (1.f - fabsf(t));  // parabolic "sine"
    return 0.225f * (y * fabsf(y) - y) + y;      // error-correction term
  }

 private:
  float phase_ = 0.f;
  float inc_ = 0.f;
};

}  // namespace dsp
