/*
 *  bbd/dsp.h — bucket-brigade delay for the DEL FX slot.
 *
 *  A bucket-brigade chip is not a memory. It is a few thousand capacitors in a
 *  row, and a two-phase clock that tips the charge from each one into the
 *  next. The signal is never digitised — it is sampled in time but not in
 *  amplitude, and it arrives at the far end having been handed along four
 *  thousand times.
 *
 *  Three consequences, and they are the entire sound:
 *
 *  1. Delay time is clock rate. There is no other control: the charge takes
 *     stages/(2*clock) to cross. So asking for a longer delay slows the clock,
 *     and the clock is the sample rate of the line — *the bandwidth falls as
 *     the delay lengthens*. A long setting is dark because it cannot be
 *     anything else. Nothing else in this repo behaves that way, and it is why
 *     a digital delay set dark still does not sound like one of these.
 *
 *  2. Every pass loses more. The anti-alias filter going in and the
 *     reconstruction filter coming out are both gentle and both in the
 *     feedback path, so each repeat is darker and softer than the last until
 *     the tail is a murmur with no top left in it.
 *
 *  3. The compander breathes. The chain's own noise floor is bad enough that
 *     these were always run with a compressor in front and an expander after.
 *     Quiet passages get pushed up into the line and pulled back down on the
 *     way out, which drags the hiss up and down with the music.
 *
 *  Modelled rather than sampled: the line runs at 48 kHz and the clock rate
 *  appears as the cutoff of the filters around it. What that buys is a delay
 *  whose character tracks its time control the way the real thing does.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in delfx.h / unit.cc.
 */
#pragma once

#include "dsp_util.h"
#include "lfo.h"
#include "noise.h"

class BbdEngine {
 public:
  /* 1.2 s a side. Far longer than any real chain managed — those ran out at
   * about 400 ms before the noise swallowed everything — but the slot is
   * called DEL FX and people want a long one. */
  static constexpr uint32_t kMaxDelay = 57600;
  static constexpr uint32_t kBufferSize = kMaxDelay * 2;

  /* Stages in the imaginary chip. Sets how fast the bandwidth falls away as
   * the delay lengthens: bandwidth is roughly kStages/(4*seconds). */
  static constexpr float kStages = 4096.f;

  enum ParamId {
    P_TIME = 0,  // A knob
    P_FEEDBACK,  // B knob
    P_MIX,
    P_TONE,
    P_MOD,
    P_AGE,
    P_SYNC,
    P_SPREAD,
    P_RATE,
    P_COUNT
  };

  enum Sync { kSyncOff = 0, kSync4, kSync8d, kSync8, kSync8t, kSync16, kNumSyncs };

  void init(float *buffer) {
    lineL_ = buffer;
    lineR_ = buffer + kMaxDelay;
    for (uint32_t i = 0; i < kBufferSize; ++i) buffer[i] = 0.f;
    write_ = 0;

    noise_.seed(0x2F9A31BU);
    lfo_.init(0.f);

    timeKnob_ = 0.35f;
    feedback_ = 0.35f;
    mix_ = 0.35f;
    tone_ = 0.7f;
    modDepth_ = 0.15f;
    age_ = 0.5f;
    sync_ = kSyncOff;
    spread_ = 0.3f;
    rate_ = 0.25f;
    tempo_ = 120.f;

    lpInL_ = lpInR_ = 0.f;
    lpOutL_ = lpOutR_ = 0.f;
    lpFbL_ = lpFbR_ = 0.f;
    envIn_ = 0.f;
    delaySmooth_.init(currentDelaySamples(), 60.f);
    updateRate();
  }

  void reset() {
    if (!lineL_) return;
    for (uint32_t i = 0; i < kBufferSize; ++i) lineL_[i] = 0.f;
    lpInL_ = lpInR_ = lpOutL_ = lpOutR_ = lpFbL_ = lpFbR_ = 0.f;
    envIn_ = 0.f;
  }

  void setTempo(float bpm) {
    tempo_ = bpm > 1.f ? bpm : 120.f;
    delaySmooth_.set(currentDelaySamples());
  }

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_TIME:
        timeKnob_ = dsp::knob(value);
        delaySmooth_.set(currentDelaySamples());
        break;
      case P_FEEDBACK:
        feedback_ = value * 0.01f;
        break;
      case P_MIX:
        mix_ = value * 0.01f;
        break;
      case P_TONE:
        tone_ = value * 0.01f;
        break;
      case P_MOD:
        modDepth_ = value * 0.01f;
        break;
      case P_AGE:
        age_ = value * 0.01f;
        break;
      case P_SYNC:
        sync_ = static_cast<Sync>(
            static_cast<int>(dsp::clampf(0.f, static_cast<float>(value), kNumSyncs - 1.f)));
        delaySmooth_.set(currentDelaySamples());
        break;
      case P_SPREAD:
        spread_ = value * 0.01f;
        break;
      case P_RATE:
        rate_ = value * 0.01f;
        updateRate();
        break;
      default:
        break;
    }
  }

  static const char *syncName(int s) {
    static const char *kNames[kNumSyncs] = {"OFF", "1/4", "1/8.", "1/8", "1/8T", "1/16"};
    return kNames[s < 0 ? 0 : (s >= kNumSyncs ? kNumSyncs - 1 : s)];
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) {
    if (!lineL_) {
      for (uint32_t i = 0; i < frames * 2; ++i) out[i] = in[i];
      return;
    }

    const float wet = mix_;
    const float dry = 1.f - mix_ * 0.5f;
    const float fb = feedback_ * 1.05f;  // just past unity is a runaway on purpose

    /*
     * The clock. Delay time sets it, and it sets the bandwidth — one control,
     * two consequences, which is the whole point. AGE decides how much of that
     * behaviour to keep; at 0 this is a clean digital delay.
     */
    const float seconds = fmaxf(delaySmooth_.value() * dsp::kSampleRateRecip, 0.002f);
    const float bbdHz = dsp::clampf(700.f, kStages / (4.f * seconds), 16000.f);
    const float clean = 16000.f;
    const float bandwidth = dsp::lerpf(clean, bbdHz, age_);
    const float aa = onePole(bandwidth);
    /* TONE darkens on top of whatever the clock already took away. */
    const float post = onePole(dsp::lerpf(700.f, 15000.f, tone_ * tone_));
    /* Each trip through the feedback path loses a little more. */
    const float fbLp = onePole(dsp::lerpf(clean, bbdHz * 0.75f, age_));

    const float modSamples = modDepth_ * modDepth_ * 0.004f * dsp::kSampleRate;
    const float hiss = age_ * age_ * 0.0016f;
    const float offsetR = spread_ * 0.35f * delaySmooth_.value();

    for (uint32_t n = 0; n < frames; ++n) {
      const float inL = in[n * 2];
      const float inR = in[n * 2 + 1];

      const float base = delaySmooth_.next();
      const float m = lfo_.next() * modSamples;

      /*
       * Compress going in. A real compander works on a rectified average and a
       * log law; this is the audible part of it — quiet material is pushed up
       * toward the line's headroom so the chain's own noise sits under it.
       */
      const float mag = 0.5f * (fabsf(inL) + fabsf(inR));
      envIn_ += (mag - envIn_) * 0.0015f;
      const float compand = age_ * 0.9f;
      const float cGain = dsp::clampf(
          1.f, dsp::lerpf(1.f, 1.f / sqrtf(fmaxf(envIn_, 0.02f)), compand), 6.f);

      float dL = readLine(lineL_, base + m);
      float dR = readLine(lineR_, base + offsetR - m);

      /* Reconstruction filter on the way out of the chain. */
      lpOutL_ += (dL - lpOutL_) * aa;
      lpOutR_ += (dR - lpOutR_) * aa;
      dL = lpOutL_;
      dR = lpOutR_;

      /* The chain's own noise belongs inside the compander, where the expander
       * pulls it back down along with everything else — which is the whole
       * reason these were built with one. */
      if (hiss > 0.f) {
        dL += noise_.next() * hiss;
        dR += noise_.next() * hiss;
      }

      /*
       * Feedback is taken here, still in the compressed domain, so the loop
       * gain is the feedback control and nothing else. Expanding first and
       * feeding *that* back puts the expander's gain inside the loop, and
       * since it sits well under unity on quiet material the repeats then die
       * out however high the feedback is set.
       */
      lpFbL_ += (dL - lpFbL_) * fbLp;
      lpFbR_ += (dR - lpFbR_) * fbLp;

      /* Anti-alias filter going in, then soft clip: the chain saturates
       * gently rather than tearing, which is what makes a runaway musical. */
      const float sendL = dsp::softclipf(inL * cGain + lpFbL_ * fb);
      const float sendR = dsp::softclipf(inR * cGain + lpFbR_ * fb);
      lpInL_ += (sendL - lpInL_) * aa;
      lpInR_ += (sendR - lpInR_) * aa;

      lineL_[write_] = lpInL_;
      lineR_[write_] = lpInR_;
      write_ = (write_ + 1) % kMaxDelay;

      /* Expand on the way out. The compressor gain moves over about half a
       * second, far slower than the line is long, so undoing the current one
       * is close enough to undoing the one this sample went in with — and the
       * mismatch that remains is the breathing everyone remembers. */
      float wetL = dL / cGain;
      float wetR = dR / cGain;
      lpOutL2_ += (wetL - lpOutL2_) * post;
      lpOutR2_ += (wetR - lpOutR2_) * post;

      out[n * 2] = dsp::softclipf(inL * dry + lpOutL2_ * wet);
      out[n * 2 + 1] = dsp::softclipf(inR * dry + lpOutR2_ * wet);
    }
  }

 private:
  static inline float onePole(float hz) {
    const float x = dsp::clampf(1.f, hz, 0.45f * dsp::kSampleRate) * dsp::kSampleRateRecip;
    return dsp::clampf(0.0005f, 1.f - expf(-2.f * dsp::kPi * x), 1.f);
  }

  float currentDelaySamples() const {
    float samples;
    if (sync_ == kSyncOff) {
      /* 20 ms to 1.2 s, exponential — the useful settings are short. */
      samples = 0.02f * dsp::pow2f_fast(timeKnob_ * 5.9f) * dsp::kSampleRate;
    } else {
      const float beat = 60.f / tempo_;  // seconds per quarter note
      float mult = 1.f;
      switch (sync_) {
        case kSync4: mult = 1.f; break;
        case kSync8d: mult = 0.75f; break;
        case kSync8: mult = 0.5f; break;
        case kSync8t: mult = 1.f / 3.f; break;
        case kSync16: mult = 0.25f; break;
        default: break;
      }
      samples = beat * mult * dsp::kSampleRate;
    }
    return dsp::clampf(64.f, samples, static_cast<float>(kMaxDelay - 4));
  }

  void updateRate() {
    /* 0.05 Hz to 6 Hz: slow is chorus, fast is warble. */
    lfo_.setRate(0.05f * dsp::pow2f_fast(rate_ * 6.9f));
  }

  inline float readLine(const float *line, float delay) const {
    const float d = dsp::clampf(1.f, delay, static_cast<float>(kMaxDelay - 2));
    float pos = static_cast<float>(write_) - d;
    while (pos < 0.f) pos += static_cast<float>(kMaxDelay);
    const uint32_t i0 = static_cast<uint32_t>(pos);
    const uint32_t i1 = (i0 + 1) % kMaxDelay;
    const float frac = pos - static_cast<float>(i0);
    return dsp::lerpf(line[i0], line[i1], frac);
  }

  float *lineL_ = nullptr;
  float *lineR_ = nullptr;
  uint32_t write_ = 0;

  dsp::Noise noise_;
  dsp::SineLfo lfo_;
  dsp::Smoother delaySmooth_;

  float timeKnob_ = 0.35f;
  float feedback_ = 0.35f;
  float mix_ = 0.35f;
  float tone_ = 0.7f;
  float modDepth_ = 0.15f;
  float age_ = 0.5f;
  Sync sync_ = kSyncOff;
  float spread_ = 0.3f;
  float rate_ = 0.25f;
  float tempo_ = 120.f;

  float lpInL_ = 0.f, lpInR_ = 0.f;
  float lpOutL_ = 0.f, lpOutR_ = 0.f;
  float lpOutL2_ = 0.f, lpOutR2_ = 0.f;
  float lpFbL_ = 0.f, lpFbR_ = 0.f;
  float envIn_ = 0.f;
};
