/*
 *  drive/dsp.h — four-flavour distortion for the MOD FX slot.
 *
 *    SOFT  smooth asymmetric saturation. Amp-like: compresses before it tears.
 *    FUZZ  hard clipping with a DC bias, so the two halves clip unevenly and
 *          the tone gets that gated, spitty edge.
 *    FOLD  wavefolder. Past the threshold the signal turns back on itself and
 *          keeps generating new harmonics instead of flattening.
 *    CRSH  sample-and-hold plus bit quantisation. No oversampling here — the
 *          aliasing *is* the effect.
 *
 *  The three analogue-ish modes run at 2x oversampling. Clipping generates
 *  harmonics well past Nyquist, and without oversampling those fold back as
 *  inharmonic junk that turns chords to mush. Upsampling is linear
 *  interpolation; decimation is a 7-tap halfband FIR, whose zero-valued odd
 *  taps mean only five multiplies per output sample.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in modfx.h / unit.cc.
 */
#pragma once

#include "dsp_util.h"

class DriveEngine {
 public:
  /* No SDRAM needed; the runtime allocates nothing when this is 0. */
  static constexpr uint32_t kBufferSize = 0;

  enum ParamId {
    P_DRIVE = 0,  // A knob (labelled TIME on the hardware)
    P_TONE,       // B knob
    P_MODE,
    P_MIX,
    P_LEVEL,
    P_BIAS,
    P_COUNT
  };

  enum Mode { kSoft = 0, kFuzz, kFold, kCrush, kNumModes };

  void init(float *) {
    drive_ = 0.4f;
    tone_ = 0.6f;
    mode_ = kSoft;
    mix_ = 1.f;
    level_ = 0.7f;
    bias_ = 0.2f;
    reset();
  }

  void reset() {
    upL_ = upR_ = 0.f;
    for (int i = 0; i < 7; ++i) hbL_[i] = hbR_[i] = 0.f;
    toneL_ = toneR_ = 0.f;
    holdL_ = holdR_ = 0.f;
    holdCount_ = 0.f;
  }

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_DRIVE:
        drive_ = dsp::knob(value);
        break;
      case P_TONE:
        tone_ = dsp::knob(value);
        break;
      case P_MODE:
        mode_ = static_cast<Mode>(static_cast<int>(
            dsp::clampf(0.f, static_cast<float>(value), kNumModes - 1.f)));
        break;
      case P_MIX:
        mix_ = value * 0.01f;
        break;
      case P_LEVEL:
        level_ = value * 0.01f;
        break;
      case P_BIAS:
        bias_ = value * 0.01f;
        break;
      default:
        break;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) {
    const Mode mode = mode_;
    /* 1x to ~40x gain into the shaper. */
    const float gain = 1.f + drive_ * drive_ * 40.f;
    /* Loud settings would otherwise be deafening next to the dry signal. */
    const float makeup = level_ / (1.f + drive_ * 2.2f);
    const float toneCoef = dsp::clampf(0.02f, tone_ * tone_ * 0.9f + 0.02f, 0.92f);
    const float bias = bias_ * 0.4f;
    const float wet = mix_;
    const float dry = 1.f - mix_;

    if (mode == kCrush) {
      const float rateDiv = 1.f + drive_ * drive_ * 30.f;
      const float levels = dsp::pow2f_fast(dsp::lerpf(12.f, 2.f, drive_));
      const float invLevels = 1.f / levels;

      for (uint32_t n = 0; n < frames; ++n) {
        const float l = in[n * 2], r = in[n * 2 + 1];
        holdCount_ -= 1.f;
        if (holdCount_ <= 0.f) {
          holdCount_ += rateDiv;
          holdL_ = static_cast<float>(static_cast<int32_t>(l * levels)) * invLevels;
          holdR_ = static_cast<float>(static_cast<int32_t>(r * levels)) * invLevels;
        }
        out[n * 2] = shapeOut(l, holdL_ * level_, dry, wet, toneCoef, toneL_);
        out[n * 2 + 1] = shapeOut(r, holdR_ * level_, dry, wet, toneCoef, toneR_);
      }
      return;
    }

    for (uint32_t n = 0; n < frames; ++n) {
      const float l = in[n * 2];
      const float r = in[n * 2 + 1];

      /* 2x upsample: the midpoint is a linear interpolation of the previous
       * input and this one, which is plenty at this ratio. */
      pushHalfband(hbL_, shape(mode, (upL_ + l) * 0.5f * gain, bias));
      pushHalfband(hbL_, shape(mode, l * gain, bias));
      pushHalfband(hbR_, shape(mode, (upR_ + r) * 0.5f * gain, bias));
      pushHalfband(hbR_, shape(mode, r * gain, bias));
      upL_ = l;
      upR_ = r;

      out[n * 2] = shapeOut(l, halfband(hbL_) * makeup, dry, wet, toneCoef, toneL_);
      out[n * 2 + 1] = shapeOut(r, halfband(hbR_) * makeup, dry, wet, toneCoef, toneR_);
    }
  }

 private:
  /* 7-tap halfband decimator: h = [-1, 0, 5, 8, 5, 0, -1] / 16. The odd taps
   * are zero, so five of the seven multiplies vanish. */
  static inline void pushHalfband(float *h, float x) {
    h[6] = h[5];
    h[5] = h[4];
    h[4] = h[3];
    h[3] = h[2];
    h[2] = h[1];
    h[1] = h[0];
    h[0] = x;
  }

  static inline float halfband(const float *h) {
    return -0.0625f * h[0] + 0.3125f * h[2] + 0.5f * h[3] + 0.3125f * h[4] -
           0.0625f * h[6];
  }

  /* Waveshapers, all unity-ish in and roughly unity out. */
  static inline float shape(Mode mode, float x, float bias) {
    switch (mode) {
      case kFuzz: {
        const float y = x + bias;
        return dsp::clampf(-1.f, y * 0.8f, 1.f);
      }
      case kFold: {
        /* Reflect repeatedly around +/-1 instead of clamping. */
        float y = x * 0.5f + bias;
        for (int i = 0; i < 4; ++i) {
          if (y > 1.f) y = 2.f - y;
          else if (y < -1.f) y = -2.f - y;
          else break;
        }
        return y;
      }
      case kSoft:
      default:
        return dsp::softclipf(x * 0.6f + bias) - bias * 0.5f;
    }
  }

  /* Post tone (one-pole lowpass), DC block and dry/wet, per channel. */
  static inline float shapeOut(float dryIn, float wetIn, float dry, float wet,
                               float toneCoef, float &toneState) {
    toneState += (wetIn - toneState) * toneCoef;
    return dsp::softclipf(dryIn * dry + toneState * wet);
  }

  float drive_ = 0.4f;
  float tone_ = 0.6f;
  Mode mode_ = kSoft;
  float mix_ = 1.f;
  float level_ = 0.7f;
  float bias_ = 0.2f;

  float upL_ = 0.f, upR_ = 0.f;
  float hbL_[7] = {0.f}, hbR_[7] = {0.f};
  float toneL_ = 0.f, toneR_ = 0.f;
  float holdL_ = 0.f, holdR_ = 0.f;
  float holdCount_ = 0.f;
};
