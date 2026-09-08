/*
 *  ensemble/dsp.h — stereo chorus/ensemble for the MOD FX slot.
 *
 *  Three LFO-modulated fractional-delay taps per channel, mixed with opposite
 *  polarity across the stereo field: the Juno/string-machine trick. Pairs well
 *  with the poly8 oscillator, which is mono by construction.
 *
 *  Portable — no logue-sdk includes. SDK glue lives in modfx.h / unit.cc.
 */
#pragma once

#include "dsp_util.h"
#include "lfo.h"

class EnsembleEngine {
 public:
  /* Floats of SDRAM we ask the runtime for: two 4096-sample delay lines. */
  static constexpr uint32_t kLineSize = 4096;  // 85 ms at 48 kHz
  static constexpr uint32_t kBufferSize = kLineSize * 2;

  enum ParamId {
    P_RATE = 0,  // A knob (labelled TIME on the hardware)
    P_DEPTH,     // B knob
    P_MIX,
    P_MODE,
    P_SPREAD,
    P_TONE,
    P_COUNT
  };

  enum Mode { kModeI = 0, kModeII, kModeDual, kNumModes };

  void init(float *buffer) {
    lineL_ = buffer;
    lineR_ = buffer + kLineSize;
    write_ = 0;
    for (uint32_t i = 0; i < kBufferSize; ++i) buffer[i] = 0.f;

    for (int i = 0; i < 3; ++i) lfo_[i].init(i * (1.f / 3.f));

    rate_ = 0.6f;
    depth_ = 0.5f;
    mix_ = 0.5f;
    mode_ = kModeII;
    spread_ = 0.7f;
    tone_ = 0.7f;
    lpL_ = lpR_ = 0.f;
    updateRates();
  }

  void reset() {
    if (!lineL_) return;
    for (uint32_t i = 0; i < kBufferSize; ++i) lineL_[i] = 0.f;
    lpL_ = lpR_ = 0.f;
  }

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_RATE:
        rate_ = dsp::knob(value);
        updateRates();
        break;
      case P_DEPTH:
        depth_ = dsp::knob(value);
        break;
      case P_MIX:
        mix_ = value * 0.01f;
        break;
      case P_MODE:
        mode_ = static_cast<Mode>(
            static_cast<int>(dsp::clampf(0.f, static_cast<float>(value), kNumModes - 1.f)));
        break;
      case P_SPREAD:
        spread_ = value * 0.01f;
        break;
      case P_TONE:
        tone_ = value * 0.01f;
        break;
      default:
        break;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) {
    if (!lineL_) return;

    const int taps = (mode_ == kModeI) ? 1 : (mode_ == kModeII ? 2 : 3);
    /* Base delay and sweep in samples. Short base = chorus, long = ensemble. */
    const float baseDelay = (mode_ == kModeDual ? 16.f : 9.f) * 0.001f * dsp::kSampleRate;
    const float sweep = depth_ * (mode_ == kModeDual ? 5.f : 3.5f) * 0.001f * dsp::kSampleRate;
    const float wet = mix_;
    const float dry = 1.f - mix_ * 0.5f;  // keep the dry signal mostly intact
    const float spread = spread_;
    /* TONE darkens the wet path: one-pole lowpass coefficient. */
    const float lpCoef = dsp::clampf(0.02f, tone_ * tone_, 1.f);

    for (uint32_t n = 0; n < frames; ++n) {
      const float inL = in[n * 2];
      const float inR = in[n * 2 + 1];
      const float mono = (inL + inR) * 0.5f;

      lineL_[write_] = mono;
      lineR_[write_] = mono;

      float wetL = 0.f, wetR = 0.f;
      for (int t = 0; t < taps; ++t) {
        const float m = lfo_[t].next();
        const float dL = baseDelay + sweep * m;
        /* spread 0 = both channels sweep together, 1 = fully counter-phase. */
        const float dR = baseDelay + sweep * m * (1.f - 2.f * spread);
        wetL += read(lineL_, dL);
        wetR += read(lineR_, dR);
      }

      const float norm = 1.f / static_cast<float>(taps);
      wetL *= norm;
      wetR *= norm;

      lpL_ += (wetL - lpL_) * lpCoef;
      lpR_ += (wetR - lpR_) * lpCoef;

      out[n * 2] = dsp::softclipf(inL * dry + lpL_ * wet);
      out[n * 2 + 1] = dsp::softclipf(inR * dry + lpR_ * wet);

      write_ = (write_ + 1) & (kLineSize - 1);
    }
  }

 private:
  void updateRates() {
    /* 0.05 Hz .. 6 Hz, with the three LFOs at mutually irrational-ish ratios. */
    const float hz = 0.05f * dsp::pow2f_fast(rate_ * 6.9f);
    lfo_[0].setRate(hz);
    lfo_[1].setRate(hz * 0.87f);
    lfo_[2].setRate(hz * 1.31f);
  }

  /* Linear-interpolated read `delay` samples behind the write head. */
  inline float read(const float *line, float delay) const {
    delay = dsp::clampf(1.f, delay, static_cast<float>(kLineSize - 2));
    float pos = static_cast<float>(write_) - delay;
    if (pos < 0.f) pos += static_cast<float>(kLineSize);
    const uint32_t i0 = static_cast<uint32_t>(pos);
    const float frac = pos - static_cast<float>(i0);
    const uint32_t i1 = (i0 + 1) & (kLineSize - 1);
    return dsp::lerpf(line[i0], line[i1], frac);
  }

  float *lineL_ = nullptr;
  float *lineR_ = nullptr;
  uint32_t write_ = 0;
  dsp::SineLfo lfo_[3];

  float rate_ = 0.6f;
  float depth_ = 0.5f;
  float mix_ = 0.5f;
  Mode mode_ = kModeII;
  float spread_ = 0.7f;
  float tone_ = 0.7f;
  float lpL_ = 0.f, lpR_ = 0.f;
};
