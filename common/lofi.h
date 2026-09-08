/*
 *  lofi.h — the converter stage from a cheap PCM keyboard: sample-and-hold
 *  rate reduction plus bit-depth quantisation, applied to the summed output.
 *
 *  One DAC for the whole instrument, which is why this belongs after the voice
 *  mix rather than inside a voice.
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class LoFi {
 public:
  void reset() {
    held_ = 0.f;
    count_ = 0.f;
  }

  /* amount 0..1: from ~15 bit at full rate to ~5 bit at a 1/8 rate. */
  void setAmount(float amount) {
    amount_ = clampf(0.f, amount, 1.f);
    rateDiv_ = 1.f + amount_ * amount_ * 7.f;
    levels_ = pow2f_fast(lerpf(15.f, 5.f, amount_));
    invLevels_ = 1.f / levels_;
  }

  inline float process(float x) {
    count_ -= 1.f;
    if (count_ <= 0.f) {
      count_ += rateDiv_;
      /* Quantise on capture, exactly as a converter would. */
      held_ = static_cast<float>(static_cast<int32_t>(x * levels_)) * invLevels_;
    }
    return held_;
  }

 private:
  float amount_ = 0.f;
  float rateDiv_ = 1.f;
  float levels_ = 32768.f;
  float invLevels_ = 1.f / 32768.f;
  float held_ = 0.f;
  float count_ = 0.f;
};

}  // namespace dsp
