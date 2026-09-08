/*
 *  adsr.h — per-voice ADSR. Linear attack, exponential decay/release.
 *
 *  Runs per sample; idle voices report isIdle() so the allocator can reuse them.
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class ADSR {
 public:
  enum Stage { kIdle = 0, kAttack, kDecay, kSustain, kRelease };

  void init() {
    value_ = 0.f;
    stage_ = kIdle;
    setAttack(5.f);
    setDecay(300.f);
    setSustain(0.7f);
    setRelease(200.f);
  }

  /* Times in milliseconds. */
  void setAttack(float ms) { attackRate_ = 1.f / (fmaxf(ms, 0.5f) * 0.001f * kSampleRate); }
  void setDecay(float ms) { decayCoef_ = coefFor(ms); }
  void setRelease(float ms) { releaseCoef_ = coefFor(ms); }
  void setSustain(float level) { sustain_ = clampf(0.f, level, 1.f); }

  void gateOn(bool retrigger) {
    if (retrigger) value_ = 0.f;
    stage_ = kAttack;
  }

  void gateOff() {
    if (stage_ != kIdle) stage_ = kRelease;
  }

  void kill() {
    value_ = 0.f;
    stage_ = kIdle;
  }

  inline bool isIdle() const { return stage_ == kIdle; }
  inline bool isReleasing() const { return stage_ == kRelease; }
  inline float value() const { return value_; }

  inline float next() {
    switch (stage_) {
      case kAttack:
        value_ += attackRate_;
        if (value_ >= 1.f) {
          value_ = 1.f;
          stage_ = kDecay;
        }
        break;
      case kDecay:
        value_ += (decayTarget() - value_) * decayCoef_;
        if (value_ - sustain_ < 1e-4f) stage_ = kSustain;
        if (value_ <= 1e-5f) {  // sustain 0: piano-style decay, voice is done
          value_ = 0.f;
          stage_ = kIdle;
        }
        break;
      case kSustain:
        value_ += (decayTarget() - value_) * decayCoef_;
        if (value_ <= 1e-5f) {
          value_ = 0.f;
          stage_ = kIdle;
        }
        break;
      case kRelease:
        value_ -= value_ * releaseCoef_;
        if (value_ < 1e-5f) {
          value_ = 0.f;
          stage_ = kIdle;
        }
        break;
      case kIdle:
      default:
        value_ = 0.f;
        break;
    }
    return value_;
  }

 private:
  /*
   * A decay to a sustain of zero aims slightly *below* zero, so it crosses in
   * finite time — at almost exactly the nominal decay time. Aiming at zero
   * makes it asymptotic: the note is inaudible after one decay time but the
   * voice stays allocated for two and a half, which on a 6-voice piano is a
   * third of the polyphony sitting on silence.
   */
  inline float decayTarget() const { return sustain_ > 1e-4f ? sustain_ : -0.01f; }

  /* Per-sample one-pole coefficient reaching ~99% of target in `ms`. */
  static float coefFor(float ms) {
    const float samples = fmaxf(ms, 1.f) * 0.001f * kSampleRate;
    return 1.f - expf(-4.6f / samples);
  }

  float value_ = 0.f;
  float attackRate_ = 0.f;
  float decayCoef_ = 0.f;
  float releaseCoef_ = 0.f;
  float sustain_ = 0.7f;
  Stage stage_ = kIdle;
};

}  // namespace dsp
