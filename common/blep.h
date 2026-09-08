/*
 *  blep.h — anti-aliased classic waveforms via polyBLEP.
 *
 *  One phasor per instance. saw() and pulse() read the same phasor, so a single
 *  oscillator can morph between shapes without phase jumps.
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class BlepOsc {
 public:
  void reset(float phase = 0.f) {
    phase_ = phase;
    dt_ = 0.f;
  }

  inline void setFreq(float hz) {
    /* Clamp below Nyquist; polyBLEP degrades badly past ~0.4 fs. */
    dt_ = clampf(0.f, hz * kSampleRateRecip, 0.45f);
  }

  inline float phase() const { return phase_; }
  inline float dt() const { return dt_; }

  inline void advance() {
    phase_ += dt_;
    if (phase_ >= 1.f) phase_ -= 1.f;
  }

  /* Band-limited saw at an arbitrary phase, using this oscillator's rate. */
  inline float sawAt(float p) const { return 2.f * p - 1.f - blep(p, dt_); }

  inline float saw() const { return sawAt(phase_); }

  /* Two-saw PWM: DC-corrected, band-limited pulse. width in (0,1). */
  inline float pulse(float width) const {
    float p2 = phase_ + width;
    if (p2 >= 1.f) p2 -= 1.f;
    return sawAt(phase_) - sawAt(p2) + (2.f * width - 1.f);
  }

  /* Naive triangle from an integrated-free formula; mild aliasing, cheap. */
  inline float tri() const {
    const float t = phase_ < 0.5f ? phase_ * 2.f : 2.f - phase_ * 2.f;
    return t * 2.f - 1.f;
  }

 private:
  static inline float blep(float t, float dt) {
    if (dt <= 0.f) return 0.f;
    if (t < dt) {
      const float x = t / dt;
      return x + x - x * x - 1.f;
    }
    if (t > 1.f - dt) {
      const float x = (t - 1.f) / dt;
      return x * x + x + x + 1.f;
    }
    return 0.f;
  }

  float phase_ = 0.f;
  float dt_ = 0.f;
};

}  // namespace dsp
