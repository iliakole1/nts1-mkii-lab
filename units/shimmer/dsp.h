/*
 *  shimmer/dsp.h — pitch-shifted reverb for the REVERB slot.
 *
 *  A Schroeder/Moorer tank — four damped combs into two allpasses per channel —
 *  with a pitch shifter wrapped around its own output and fed back in. Notes
 *  entering the tank come back an octave up, then that comes back an octave up
 *  again, and the tail climbs away from the source instead of just fading.
 *
 *  Two details keep it musical rather than a screaming feedback loop:
 *    - the shifted signal is damped and soft-clipped before re-entry, so the
 *      loop gain falls with each pass through
 *    - the shifter is a two-head crossfade with *triangular* windows, which sum
 *      to exactly 1 at 50% overlap, so no amplitude ripple
 *
 *  Portable: no logue-sdk includes. SDK glue lives in revfx.h / unit.cc.
 */
#pragma once

#include "dsp_util.h"

class ShimmerEngine {
 public:
  enum ParamId {
    /* The first three are the hardware's fixed A/B/MIX controls for rev fx. */
    P_TIME = 0,
    P_DEPTH,
    P_MIX,
    P_DAMP,
    P_PREDELAY,
    P_PITCH,
    P_LOWCUT,
    P_COUNT
  };

  enum Pitch { kOctaveUp = 0, kFifthUp, kTwoOctavesUp, kOctaveDown, kNumPitches };

  /* Comb and allpass lengths, Freeverb's tunings scaled from 44.1k to 48k,
   * live as locals in init() — static constexpr arrays would need an
   * out-of-line definition under -std=c++11, which the SDK build uses. */
  static constexpr int kNumCombs = 4;
  static constexpr int kNumAllpass = 2;

  static constexpr int kPreDelayMax = 9600;  // 200 ms
  static constexpr int kShiftSize = 32768;
  static constexpr int kShiftMask = kShiftSize - 1;
  /*
   * 250 ms grains. The window length sets the grain rate — (ratio-1)/window —
   * and every grain boundary puts sidebands around each partial. At 50 ms and
   * an octave up that rate is 20 Hz, which reads as a rough, detuned buzz
   * smeared around every note. At 250 ms it is 4 Hz: a slow shimmer instead.
   * Long grains smear transients, which for a reverb tail costs nothing.
   */
  static constexpr int kWindow = 12000;

  /* Floats of SDRAM to ask the runtime for. */
  static constexpr uint32_t kBufferSize = (1214 + 1293 + 1390 + 1476) +
                                          (1548 + 1623 + 1695 + 1760) + (605 + 480) +
                                          (371 + 245) + kPreDelayMax + kShiftSize + 64;

  void init(float *buffer) {
    const int kCombL[kNumCombs] = {1214, 1293, 1390, 1476};
    const int kCombR[kNumCombs] = {1548, 1623, 1695, 1760};
    const int kApL[kNumAllpass] = {605, 480};
    const int kApR[kNumAllpass] = {371, 245};

    float *p = buffer;
    for (uint32_t i = 0; i < kBufferSize; ++i) buffer[i] = 0.f;

    for (int c = 0; c < kNumCombs; ++c) {
      combL_[c].line = p;
      combL_[c].size = kCombL[c];
      p += kCombL[c];
      combR_[c].line = p;
      combR_[c].size = kCombR[c];
      p += kCombR[c];
    }
    for (int a = 0; a < kNumAllpass; ++a) {
      apL_[a].line = p;
      apL_[a].size = kApL[a];
      p += kApL[a];
      apR_[a].line = p;
      apR_[a].size = kApR[a];
      p += kApR[a];
    }
    preLine_ = p;
    p += kPreDelayMax;
    shiftLine_ = p;

    reset();

    time_ = 0.7f;
    depth_ = 0.45f;
    mix_ = 0.4f;
    damp_ = 0.5f;
    preDelaySamples_ = 0.f;
    pitch_ = kOctaveUp;
    lowCut_ = 0.2f;
  }

  void reset() {
    for (int c = 0; c < kNumCombs; ++c) {
      combL_[c].clear();
      combR_[c].clear();
    }
    for (int a = 0; a < kNumAllpass; ++a) {
      apL_[a].clear();
      apR_[a].clear();
    }
    if (preLine_)
      for (int i = 0; i < kPreDelayMax; ++i) preLine_[i] = 0.f;
    if (shiftLine_)
      for (int i = 0; i < kShiftSize; ++i) shiftLine_[i] = 0.f;
    preWrite_ = 0;
    shiftWrite_ = 0;
    shiftPhase_ = 0.f;
    hpL_ = hpR_ = 0.f;
    shiftLp_ = 0.f;
    shiftIn_ = 0.f;
  }

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_TIME:
        time_ = dsp::knob(value);
        break;
      case P_DEPTH:
        depth_ = dsp::knob(value);
        break;
      case P_MIX:
        mix_ = dsp::knob(value);
        break;
      case P_DAMP:
        damp_ = value * 0.01f;
        break;
      case P_PREDELAY:
        preDelaySamples_ = dsp::clampf(
            0.f, value * 0.001f * dsp::kSampleRate, static_cast<float>(kPreDelayMax - 2));
        break;
      case P_PITCH:
        pitch_ = static_cast<Pitch>(static_cast<int>(
            dsp::clampf(0.f, static_cast<float>(value), kNumPitches - 1.f)));
        break;
      case P_LOWCUT:
        lowCut_ = value * 0.01f;
        break;
      default:
        break;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) {
    if (!preLine_) return;

    /* Comb feedback: 0.7 is a short room, 0.96 a long hall. */
    const float feedback = dsp::lerpf(0.70f, 0.96f, time_);
    const float damping = dsp::clampf(0.f, damp_ * 0.6f, 0.9f);
    /*
     * The wet path is trimmed because the rewritten shifter passes far more
     * signal than the old one did; without this the tank sits on the output
     * soft clip at ordinary mix settings.
     */
    const float wet = mix_ * 0.75f;
    const float dry = 1.f - mix_ * 0.7f;
    /*
     * Regeneration falls as the tank lengthens — comb feedback and shimmer
     * feedback multiply — and is hard-capped on top of that. Past the cap the
     * loop sustains instead of decaying: a drone, not a reverb.
     *
     * These numbers are roughly half what they were before the shifter was
     * rewritten, and that is the point: the old two-head design cancelled a
     * good part of its own output, so it needed a hot loop to shimmer at all.
     * A shifter that actually passes its signal needs far less.
     */
    const float shimmer = fminf(depth_ * (0.75f - 0.30f * time_), 0.45f);
    const float ratio = pitchRatio();
    /* One-pole high-pass coefficient for the tail; keeps the low end clear. */
    const float hpCoef = dsp::clampf(0.0005f, lowCut_ * lowCut_ * 0.06f, 0.06f);

    for (uint32_t n = 0; n < frames; ++n) {
      const float inL = in[n * 2];
      const float inR = in[n * 2 + 1];

      /* Predelay is shared: the tank is fed a mono sum, as most plates are. */
      preLine_[preWrite_] = (inL + inR) * 0.5f;
      const float src = readLine(preLine_, kPreDelayMax, preWrite_, preDelaySamples_);
      if (++preWrite_ >= kPreDelayMax) preWrite_ = 0;

      /* Previous tail, pitch-shifted, is what makes it shimmer. */
      const float shifted = shiftRead(ratio);
      shiftLp_ += (shifted - shiftLp_) * 0.5f;  // tame the top before re-entry
      const float regen = dsp::softclipf(shiftLp_ * shimmer);

      const float feedL = src + regen;
      const float feedR = src + regen;

      float accL = 0.f, accR = 0.f;
      for (int c = 0; c < kNumCombs; ++c) {
        accL += combL_[c].process(feedL, feedback, damping);
        accR += combR_[c].process(feedR, feedback, damping);
      }
      accL *= 1.f / kNumCombs;
      accR *= 1.f / kNumCombs;

      for (int a = 0; a < kNumAllpass; ++a) {
        accL = apL_[a].process(accL);
        accR = apR_[a].process(accR);
      }

      /* High-pass the tail so the shimmer does not build up mud. */
      hpL_ += (accL - hpL_) * hpCoef;
      hpR_ += (accR - hpR_) * hpCoef;
      const float tailL = accL - hpL_;
      const float tailR = accR - hpR_;

      /*
       * Feed the shifter from the tail, mono and band-limited. Reading the line
       * at 2x speed doubles every frequency in it, so anything above a quarter
       * of the sample rate comes back as aliasing — inharmonic, and exactly the
       * kind of scatter a shimmer must not have.
       */
      const float tailMono = (tailL + tailR) * 0.5f;
      shiftIn_ += (tailMono - shiftIn_) * 0.45f;
      shiftLine_[shiftWrite_] = shiftIn_;
      shiftWrite_ = (shiftWrite_ + 1) & kShiftMask;

      out[n * 2] = dsp::softclipf(inL * dry + tailL * wet);
      out[n * 2 + 1] = dsp::softclipf(inR * dry + tailR * wet);
    }
  }

 private:
  struct Comb {
    float *line = nullptr;
    int size = 1;
    int index = 0;
    float store = 0.f;

    void clear() {
      index = 0;
      store = 0.f;
      for (int i = 0; i < size; ++i) line[i] = 0.f;
    }

    inline float process(float in, float feedback, float damping) {
      const float out = line[index];
      store = out * (1.f - damping) + store * damping;  // lowpass in the loop
      line[index] = in + store * feedback;
      if (++index >= size) index = 0;
      return out;
    }
  };

  struct Allpass {
    float *line = nullptr;
    int size = 1;
    int index = 0;

    void clear() {
      index = 0;
      for (int i = 0; i < size; ++i) line[i] = 0.f;
    }

    inline float process(float in) {
      const float buf = line[index];
      const float out = -in + buf;
      line[index] = in + buf * 0.5f;
      if (++index >= size) index = 0;
      return out;
    }
  };

  /*
   * Exact ratios matter more here than anywhere else in the repo: the shifted
   * signal is fed back and shifted again, so any error compounds generation
   * after generation. +24 was 3.9886 — five cents flat on the first pass, ten
   * on the second, and by the third the tail is audibly sour.
   */
  float pitchRatio() const {
    switch (pitch_) {
      case kFifthUp:
        return 1.4983071f;  // +7 semitones, 2^(7/12)
      case kTwoOctavesUp:
        return 4.f;  // +24
      case kOctaveDown:
        return 0.5f;
      case kOctaveUp:
      default:
        return 2.f;
    }
  }

  static inline float readLine(const float *line, int size, int write, float delay) {
    float pos = static_cast<float>(write) - delay;
    if (pos < 0.f) pos += static_cast<float>(size);
    const int i0 = static_cast<int>(pos);
    const float frac = pos - static_cast<float>(i0);
    const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
    return dsp::lerpf(line[i0], line[i1], frac);
  }

  /*
   * One head does the resampling; the second exists only to hide the splice.
   *
   * The obvious two-head design — heads locked half a window apart, both
   * audible the whole time, crossfaded triangularly — sounds detuned on
   * anything sustained, and the reason is worth writing down. Both heads
   * resample the same source at the same ratio, so they emit the *same*
   * frequency; their delays differ by a constant W/2, so their phase
   * difference is a constant 2*pi*f*(W/2)/SR. That is a fixed comb across the
   * spectrum with a notch every 2*SR/W Hz — 8 Hz apart at this window length.
   * Sustained notes get shredded into a detuned scatter, which is exactly what
   * it sounds like.
   *
   * So: head A sweeps the full window alone, and only in the last 12% does head
   * B — a whole window behind, i.e. sitting where A is about to jump back to —
   * fade in to cover the wrap. Interference exists only during that splice, and
   * an equal-power crossfade is right there because the two are reading
   * genuinely different content.
   *
   * Shifting down sweeps the delay outward from W to 2W instead, so the splice
   * head never has to read samples that have not been written yet.
   */
  static constexpr float kFade = 0.12f;

  inline float shiftRead(float ratio) {
    const float span = fabsf(ratio - 1.f);
    if (span < 1e-6f) return shiftReadAt(kWindow);  // unity: nothing to do

    /* |increment| < 1, so a compare beats floorf() — a libm call on this FPU. */
    shiftPhase_ += span / static_cast<float>(kWindow);
    if (shiftPhase_ >= 1.f) shiftPhase_ -= 1.f;

    const float q = shiftPhase_;  // 0 -> 1 across one grain
    const bool up = (ratio > 1.f);
    const float dA = up ? (1.f - q) * kWindow : (1.f + q) * kWindow;
    const float a = shiftReadAt(dA);

    if (q < 1.f - kFade) return a;

    const float x = (q - (1.f - kFade)) / kFade;  // 0..1 over the splice
    const float dB = up ? dA + kWindow : dA - kWindow;
    return sqrtf(1.f - x) * a + sqrtf(x) * shiftReadAt(dB);
  }

  inline float shiftReadAt(float delay) const {
    float pos = static_cast<float>(shiftWrite_) - delay;
    if (pos < 0.f) pos += static_cast<float>(kShiftSize);
    const int i0 = static_cast<int>(pos);
    const float frac = pos - static_cast<float>(i0);
    return dsp::lerpf(shiftLine_[i0 & kShiftMask], shiftLine_[(i0 + 1) & kShiftMask],
                      frac);
  }

  Comb combL_[kNumCombs], combR_[kNumCombs];
  Allpass apL_[kNumAllpass], apR_[kNumAllpass];

  float *preLine_ = nullptr;
  float *shiftLine_ = nullptr;
  int preWrite_ = 0;
  int shiftWrite_ = 0;
  float shiftPhase_ = 0.f;
  float shiftLp_ = 0.f;
  float shiftIn_ = 0.f;

  float time_ = 0.7f;
  float depth_ = 0.45f;
  float mix_ = 0.4f;
  float damp_ = 0.5f;
  float preDelaySamples_ = 0.f;
  Pitch pitch_ = kOctaveUp;
  float lowCut_ = 0.2f;

  float hpL_ = 0.f, hpR_ = 0.f;
};
