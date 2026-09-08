/*
 *  noise.h — xorshift white noise. No libc rand(), no state shared between
 *  voices, ~5 cycles per sample.
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class Noise {
 public:
  void seed(uint32_t s) { state_ = s ? s : 0x9E3779B9U; }

  /* -1..1 */
  inline float next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    /* Top 24 bits into a float, scaled to signed unity. */
    return static_cast<float>(static_cast<int32_t>(state_)) * 4.656613e-10f;
  }

 private:
  uint32_t state_ = 0x9E3779B9U;
};

}  // namespace dsp
