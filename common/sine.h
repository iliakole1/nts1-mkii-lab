/*
 *  sine.h — interpolated sine table.
 *
 *  Held as a plain member of the owning engine rather than a static: the target
 *  links with nano/nosys specs and no C++ static-init guards, so header-only
 *  static tables are a good way to get a unit that will not load.
 *
 *  256 points with linear interpolation puts the error around -70 dB, which is
 *  well under the noise floor of anything this repo does with it.
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class SineTable {
 public:
  static constexpr int kSize = 256;

  void init() {
    for (int i = 0; i <= kSize; ++i)
      table_[i] = sinf(2.f * kPi * static_cast<float>(i) / static_cast<float>(kSize));
  }

  /*
   * Phase in turns, wrapped by masking — so it must be >= 0. FM arguments can
   * swing negative, so add kPhaseBias (a whole number of turns, therefore
   * free) before calling.
   *
   * Truncation rather than floorf() is deliberate: this FPU is fpv4-sp-d16,
   * which has no VRINT, so floorf() in the sample loop becomes a libm call.
   */
  static constexpr float kPhaseBias = 16.f;

  inline float at(float phase) const {
    const float x = phase * static_cast<float>(kSize);
    const int i = static_cast<int>(x);
    const int idx = i & (kSize - 1);
    return lerpf(table_[idx], table_[idx + 1], x - static_cast<float>(i));
  }

 private:
  float table_[kSize + 1] = {0.f};
};

}  // namespace dsp
