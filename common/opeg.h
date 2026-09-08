/*
 *  opeg.h — four-stage rate/level envelope, one per FM operator.
 *
 *  An ADSR cannot make these sounds. A DX-style envelope is four (level, time)
 *  pairs: on key-down it walks to L1, then L2, then L3 and holds there; on key
 *  release it walks to L4. Nothing says a later level has to be lower, which is
 *  what a brass swell is — and the whole envelope runs on a *modulator*, where
 *  it shapes brightness rather than loudness. That is the instrument.
 *
 *  Rises are linear and falls are exponential, matching common/adsr.h: a linear
 *  attack is what gives a struck operator its punch, and an exponential decay
 *  is what a real resonator does.
 *
 *  L3 = 0 is the percussive case. The envelope reaches zero and reports idle
 *  while the key is still down, so the voice frees itself — bells, marimba and
 *  plucked patches all depend on it.
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class OpEG {
 public:
  struct Spec {
    float level[4];   // L1..L4, 0..1
    float timeMs[4];  // T1..T4, time to traverse that segment
  };

  void init() {
    value_ = 0.f;
    stage_ = kIdle;
    const Spec flat = {{1.f, 1.f, 1.f, 0.f}, {2.f, 10.f, 10.f, 100.f}};
    setSpec(flat, 0.f, 0.f);
  }

  /* The two adds come from the ATK and REL knobs and only stretch, never
   * shorten: zero on both is exactly the patch. */
  void setSpec(const Spec &s, float attackAddMs, float releaseAddMs) {
    for (int i = 0; i < 4; ++i) {
      level_[i] = clampf(0.f, s.level[i], 1.f);
      float ms = s.timeMs[i];
      if (i == 0) ms += attackAddMs;
      if (i == 3) ms += releaseAddMs;
      const float samples = fmaxf(ms, 0.5f) * 0.001f * kSampleRate;
      riseRate_[i] = 1.f / samples;
      fallCoef_[i] = 1.f - expf(-4.6f / samples);
    }
  }

  void gateOn(bool retrigger) {
    if (retrigger) value_ = 0.f;
    stage_ = 0;
  }

  void gateOff() {
    if (stage_ != kIdle) stage_ = 3;
  }

  void kill() {
    value_ = 0.f;
    stage_ = kIdle;
  }

  inline bool isIdle() const { return stage_ == kIdle; }
  inline float value() const { return value_; }

  inline float next() {
    if (stage_ == kIdle) return 0.f;
    if (stage_ == kSustain) return value_;

    const float target = level_[stage_];
    const bool toZero = target <= 1e-4f;

    if (value_ < target - 1e-5f) {
      value_ += riseRate_[stage_];
      if (value_ >= target) {
        value_ = target;
        advance();
      }
    } else if (value_ > target + 1e-6f) {
      /*
       * Falling segments aim a little *past* their target rather than at it.
       *
       * An exponential approach to its own asymptote stalls in single
       * precision long before it arrives: on a long segment the last
       * increments drop under the mantissa step and the value parks a few
       * ten-thousandths above the level it was heading for. Testing "close
       * enough to advance" then never fires, and the envelope is stuck one
       * segment short — which on a patch whose next segment is the one that
       * fades it to silence means a note that sounds forever and a voice the
       * pool can never reuse.
       *
       * Aiming 0.01 below the target guarantees the crossing happens, and the
       * value is snapped back on arrival so the level is still exact.
       */
      value_ += ((target - 0.01f) - value_) * fallCoef_[stage_];
      if (value_ <= target) {
        value_ = target;
        if (toZero) {
          /* A segment that lands on zero ends the note rather than holding. */
          value_ = 0.f;
          stage_ = kIdle;
        } else {
          advance();
        }
      }
    } else {
      /* Already at the target. Zero still means the note is over. */
      if (toZero) {
        value_ = 0.f;
        stage_ = kIdle;
      } else {
        advance();
      }
    }
    return value_;
  }

 private:
  static constexpr int kIdle = 6;
  static constexpr int kSustain = 7;

  inline void advance() {
    if (stage_ == 3) {  // release finished
      value_ = 0.f;
      stage_ = kIdle;
    } else if (stage_ == 2) {
      /* L3 is the sustain level: hold here until the key comes up. */
      stage_ = kSustain;
    } else {
      ++stage_;
    }
  }

  float level_[4] = {0.f, 0.f, 0.f, 0.f};
  float riseRate_[4] = {0.f, 0.f, 0.f, 0.f};
  float fallCoef_[4] = {0.f, 0.f, 0.f, 0.f};
  float value_ = 0.f;
  int stage_ = kIdle;
};

}  // namespace dsp
