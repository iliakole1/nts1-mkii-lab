/*
 *  micro/dsp.h — micro pitch shifting for the MOD FX slot.
 *
 *  Two pitch shifters a few cents apart, one per side, with a short delay on
 *  each. That is all it is, and it is the widest-sounding effect there is that
 *  does not move: no LFO, nothing sweeping, so nothing to hear cycling. A
 *  chorus makes width by modulating a delay, which is audible as movement. A
 *  micro pitch shifter makes width by putting genuinely different pitches in
 *  the two ears, and the brain reads that as size rather than as motion.
 *
 *  Detunes of five to fifteen cents are the useful range. Past about twenty
 *  five it stops reading as one instrument and starts reading as two, which is
 *  what the wider modes are for.
 *
 *  Because the interval is tiny the shifter barely splices: grain rate is
 *  |ratio-1|/window, so at 7 cents and a 43 ms window a splice happens every
 *  ten seconds or so. That is why this can use a short window and keep
 *  transients intact where units/revfx/shimmer needs a long one.
 *
 *  Pairs with the mono oscillators here the way units/modfx/ensemble does, but
 *  without the movement — useful on the ones that already have their own
 *  vibrato and do not want more.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in modfx.h / unit.cc.
 */
#pragma once

#include "dsp_util.h"
#include "shifter.h"

class MicroEngine {
 public:
  /* Two lines, power of two. Must hold twice the window plus the longest
   * delay: 2*2048 + 3000 fits inside 8192 with room to spare. */
  static constexpr int kLineSize = 8192;
  static constexpr int kWindow = 2048;  // 43 ms
  static constexpr uint32_t kBufferSize = kLineSize * 2;

  enum ParamId {
    P_DETUNE = 0,  // A knob, cents
    P_MIX,         // B knob
    P_DELAY,
    P_MODE,
    P_SPREAD,
    P_FEEDBACK,
    P_TONE,
    P_COUNT
  };

  enum Mode { kFine = 0, kWide, kSlap, kNumModes };

  void init(float *buffer) {
    left_.init(buffer, kLineSize, kWindow);
    right_.init(buffer + kLineSize, kLineSize, kWindow);

    detune_ = 0.14f;
    mix_ = 0.5f;
    delay_ = 0.3f;
    mode_ = kFine;
    spread_ = 0.85f;
    feedback_ = 0.f;
    tone_ = 0.8f;

    lpL_ = lpR_ = 0.f;
    fbL_ = fbR_ = 0.f;
  }

  void reset() {
    left_.reset();
    right_.reset();
    lpL_ = lpR_ = 0.f;
    fbL_ = fbR_ = 0.f;
  }

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_DETUNE:
        detune_ = dsp::knob(value);
        break;
      case P_MIX:
        mix_ = value * 0.01f;
        break;
      case P_DELAY:
        delay_ = dsp::knob(value);
        break;
      case P_MODE:
        mode_ = static_cast<Mode>(
            static_cast<int>(dsp::clampf(0.f, static_cast<float>(value), kNumModes - 1.f)));
        break;
      case P_SPREAD:
        spread_ = value * 0.01f;
        break;
      case P_FEEDBACK:
        feedback_ = value * 0.01f;
        break;
      case P_TONE:
        tone_ = value * 0.01f;
        break;
      default:
        break;
    }
  }

  static const char *modeName(int m) {
    static const char *kNames[kNumModes] = {"FINE", "WIDE", "SLAP"};
    return kNames[m < 0 ? 0 : (m >= kNumModes ? kNumModes - 1 : m)];
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) {
    /*
     * Mode is the pair of (how far apart, how far behind). The detune knob
     * scales the interval; the mode decides the character — a few cents and
     * almost no delay reads as one wide instrument, more of both reads as two
     * takes of the same part.
     */
    float centsScale = 1.f, baseMs = 0.f, skewMs = 0.f;
    switch (mode_) {
      case kFine:
        centsScale = 1.f;
        baseMs = 6.f;
        skewMs = 3.f;
        break;
      case kWide:
        centsScale = 2.4f;
        baseMs = 18.f;
        skewMs = 9.f;
        break;
      default:
        centsScale = 1.6f;
        baseMs = 42.f;
        skewMs = 20.f;
        break;
    }

    /* Up to 25 cents on the knob, scaled by the mode. */
    const float cents = detune_ * 25.f * centsScale;
    const float ratioDown = dsp::centsToRatio(-cents);
    const float ratioUp = dsp::centsToRatio(cents);

    const float ms = baseMs + delay_ * 40.f;
    const float dL = ms * 0.001f * dsp::kSampleRate;
    const float dR = (ms + skewMs) * 0.001f * dsp::kSampleRate;

    const float wet = mix_;
    const float dry = 1.f - mix_ * 0.5f;
    const float fb = feedback_ * 0.72f;
    const float lp = dsp::clampf(0.02f, tone_ * tone_, 1.f);

    for (uint32_t n = 0; n < frames; ++n) {
      const float inL = in[n * 2];
      const float inR = in[n * 2 + 1];
      const float mono = (inL + inR) * 0.5f;

      /*
       * Feedback cascades the detune: each pass through shifts the already
       * shifted signal again, so a little of it turns a two-voice widener into
       * a smeared cluster. Soft-clipped and damped, because a pitch shifter in
       * its own feedback path will otherwise walk away.
       */
      left_.write(dsp::softclipf(mono + fbL_ * fb));
      right_.write(dsp::softclipf(mono + fbR_ * fb));

      float wetL = left_.read(ratioDown, dL);
      float wetR = right_.read(ratioUp, dR);

      lpL_ += (wetL - lpL_) * lp;
      lpR_ += (wetR - lpR_) * lp;
      wetL = lpL_;
      wetR = lpR_;

      fbL_ = wetL;
      fbR_ = wetR;

      /*
       * SPREAD collapses the two shifted voices toward the middle. At 0 both
       * ears get the same sum, which is a thickener rather than a widener —
       * and stays mono-compatible, which the fully spread setting does not.
       */
      const float midW = (wetL + wetR) * 0.5f;
      const float outL = dsp::lerpf(midW, wetL, spread_);
      const float outR = dsp::lerpf(midW, wetR, spread_);

      out[n * 2] = dsp::softclipf(inL * dry + outL * wet);
      out[n * 2 + 1] = dsp::softclipf(inR * dry + outR * wet);
    }
  }

 private:
  dsp::Shifter left_, right_;

  float detune_ = 0.14f;
  float mix_ = 0.5f;
  float delay_ = 0.3f;
  Mode mode_ = kFine;
  float spread_ = 0.85f;
  float feedback_ = 0.f;
  float tone_ = 0.8f;

  float lpL_ = 0.f, lpR_ = 0.f;
  float fbL_ = 0.f, fbR_ = 0.f;
};
