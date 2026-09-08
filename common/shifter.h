/*
 *  shifter.h — delay-line pitch shifter, one head plus a splice.
 *
 *  Resampling a delay line at a different rate than it is written shifts
 *  everything in it. The head has to jump back periodically or it runs off the
 *  end, and hiding that jump is the entire problem.
 *
 *  One head does the resampling; the second exists only to hide the splice.
 *
 *  The obvious two-head design — heads locked half a window apart, both
 *  audible the whole time, crossfaded triangularly — sounds detuned on
 *  anything sustained, and the reason is worth writing down. Both heads
 *  resample the same source at the same ratio, so they emit the *same*
 *  frequency; their delays differ by a constant W/2, so their phase difference
 *  is a constant 2*pi*f*(W/2)/SR. That is a fixed comb across the spectrum,
 *  with a notch every 2*SR/W Hz. Sustained notes get shredded into a detuned
 *  scatter, which is exactly what it sounds like.
 *
 *  So: head A sweeps the full window alone, and only in the last 12% does head
 *  B — a whole window behind, sitting where A is about to jump back to — fade
 *  in to cover the wrap. Interference exists only during the splice, and an
 *  equal-power crossfade is right there because the two heads are reading
 *  genuinely different content.
 *
 *  Shifting down sweeps the delay outward from W to 2W instead, so the splice
 *  head never has to read samples that have not been written yet.
 *
 *  The window length sets the grain rate — |ratio-1|/window — and every grain
 *  boundary puts sidebands around each partial. Short windows keep transients
 *  crisp but buzz on big intervals; long ones smear transients but stay smooth.
 *  A shift of an octave wants a long window; a shift of a few cents barely
 *  splices at all and can have a short one.
 *
 *  The caller owns the line, which must be a power of two and at least twice
 *  the window plus whatever extra delay is asked for.
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

class Shifter {
 public:
  /* Fraction of a grain spent crossfading across the splice. */
  static constexpr float kFade = 0.12f;

  void init(float *line, int size, int window) {
    line_ = line;
    size_ = size;
    mask_ = size - 1;
    window_ = static_cast<float>(window);
    reset();
  }

  /* Changing the window mid-flight is fine — it only moves where the heads
   * read from — but it must not exceed half the line, or the splice head
   * reads samples that have not been written yet. */
  void setWindow(int window) {
    const float maxW = static_cast<float>(size_) * 0.4f;
    window_ = clampf(16.f, static_cast<float>(window), maxW);
  }

  void reset() {
    if (line_)
      for (int i = 0; i < size_; ++i) line_[i] = 0.f;
    write_ = 0;
    phase_ = 0.f;
  }

  inline void write(float x) {
    if (!line_) return;
    line_[write_] = x;
    write_ = (write_ + 1) & mask_;
  }

  /* ratio 1.0 passes through. `extra` adds plain delay in samples. */
  inline float read(float ratio, float extra = 0.f) {
    if (!line_) return 0.f;
    const float span = fabsf(ratio - 1.f);
    if (span < 1e-6f) return readAt(window_ + extra);  // unity: nothing to do

    /* |increment| < 1, so a compare beats floorf() — a libm call on this FPU. */
    phase_ += span / window_;
    if (phase_ >= 1.f) phase_ -= 1.f;

    const float q = phase_;  // 0 -> 1 across one grain
    const bool up = (ratio > 1.f);
    const float dA = (up ? (1.f - q) : (1.f + q)) * window_ + extra;
    const float a = readAt(dA);

    if (q < 1.f - kFade) return a;

    const float x = (q - (1.f - kFade)) / kFade;  // 0..1 over the splice
    const float dB = up ? dA + window_ : dA - window_;
    return sqrtf(1.f - x) * a + sqrtf(x) * readAt(dB);
  }

  inline float readAt(float delay) const {
    float pos = static_cast<float>(write_) - delay;
    while (pos < 0.f) pos += static_cast<float>(size_);
    const int i0 = static_cast<int>(pos);
    const float frac = pos - static_cast<float>(i0);
    return lerpf(line_[i0 & mask_], line_[(i0 + 1) & mask_], frac);
  }

 private:
  float *line_ = nullptr;
  int size_ = 0;
  int mask_ = 0;
  int write_ = 0;
  float window_ = 1.f;
  float phase_ = 0.f;
};

/* Cents to a frequency ratio. */
inline float centsToRatio(float cents) { return pow2f_fast(cents * (1.f / 1200.f)); }

}  // namespace dsp
