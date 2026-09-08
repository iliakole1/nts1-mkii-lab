/*
 *  dsp_util.h — small portable helpers shared by every unit in this repo.
 *
 *  Deliberately free of logue-sdk headers so the same code compiles for the
 *  Cortex-M7 target and for the native offline test harness (see tests/).
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace dsp {

constexpr float kSampleRate = 48000.f;
constexpr float kSampleRateRecip = 1.f / kSampleRate;
constexpr float kPi = 3.14159265358979323846f;

inline float clampf(float lo, float x, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* 0..1023 knob value -> 0..1 */
inline float knob(int32_t v) { return v * (1.f / 1023.f); }

/* Cheap 2^x, ~1e-4 relative error over the range we use for pitch. */
inline float pow2f_fast(float x) {
  const float xi = floorf(x);
  const float xf = x - xi;
  const float y =
      1.f + xf * (0.6931472f + xf * (0.2402265f + xf * (0.0555041f + xf * 0.0096181f)));
  union {
    float f;
    int32_t i;
  } u;
  u.i = (static_cast<int32_t>(xi) + 127) << 23;
  return y * u.f;
}

/* MIDI note (fractional allowed) -> Hz */
inline float noteToHz(float note) {
  return 440.f * pow2f_fast((note - 69.f) * (1.f / 12.f));
}

/* Smooth saturator, unity slope near zero, asymptotic to +/-1. */
inline float softclipf(float x) {
  const float ax = fabsf(x);
  if (ax < 0.66666667f) return x;
  if (ax > 1.33333333f) return x > 0.f ? 1.f : -1.f;
  const float s = x > 0.f ? 1.f : -1.f;
  const float t = ax - 0.66666667f;
  return s * (0.66666667f + t - (t * t) * 0.75f);
}

/* One-pole parameter smoother, time constant in ms, updated per sample. */
class Smoother {
 public:
  void init(float initial, float ms) {
    value_ = target_ = initial;
    setTime(ms);
  }
  void setTime(float ms) {
    coef_ = 1.f - expf(-1.f / (ms * 0.001f * kSampleRate));
  }
  inline void set(float target) { target_ = target; }
  inline void snap(float v) { value_ = target_ = v; }
  inline float next() {
    value_ += (target_ - value_) * coef_;
    return value_;
  }
  inline float value() const { return value_; }

 private:
  float value_ = 0.f, target_ = 0.f, coef_ = 1.f;
};

}  // namespace dsp
