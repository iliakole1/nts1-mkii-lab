/*
 *  reso/dsp.h — tuned string resonators for the DEL FX slot.
 *
 *  A delay line short enough to be a pitch rather than an echo. Feed anything
 *  into a loop 1/f seconds long and the components of it that fit the loop
 *  survive, the ones that do not cancel themselves out, and what comes back is
 *  the input played on a string. That is [units/pluck](../pluck) with the
 *  excitation taken from the audio input instead of a burst of noise — which
 *  is the older idea of the two, and the more useful one in an effects slot.
 *
 *  Four of them, tuned to a chord. A drum loop through a minor seventh comes
 *  back as a chord voiced by the transients; a pad through a unison becomes a
 *  resonant peak that rings.
 *
 *  The damping filter in each loop is the string: how much top it keeps sets
 *  both the brightness and how long the note rings, because the loop loses a
 *  little more energy on every pass. That is one control, not two, and
 *  pretending otherwise is how these end up sounding synthetic.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in delfx.h / unit.cc.
 */
#pragma once

#include "dsp_util.h"
#include "svf.h"

class ResoEngine {
 public:
  static constexpr int kStrings = 4;
  /* Power of two so the read index masks. 4096 reaches down to ~12 Hz, well
   * below the lowest root this offers. */
  static constexpr uint32_t kLineSize = 4096;
  static constexpr uint32_t kLineMask = kLineSize - 1;
  static constexpr uint32_t kBufferSize = kLineSize * kStrings;

  enum ParamId {
    P_NOTE = 0,  // A knob: root pitch
    P_DECAY,     // B knob
    P_MIX,
    P_CHORD,
    P_DAMP,
    P_STRINGS,
    P_SPREAD,
    P_TONE,
    P_FINE,
    P_COUNT
  };

  enum Chord { kUnison = 0, kOctave, kFifth, kMajor, kMinor, kMajor7, kMinor7, kSus4, kNumChords };

  /* Semitone offsets from the root, four strings each. */
  static const int8_t *chordSteps(int chord) {
    static const int8_t kSteps[kNumChords][kStrings] = {
        {0, 0, 12, 12},   // UNIS — doubled at the octave for body
        {0, 12, 24, 36},  // OCT
        {0, 7, 12, 19},   // 5TH
        {0, 4, 7, 12},    // MAJ
        {0, 3, 7, 12},    // MIN
        {0, 4, 7, 11},    // MAJ7
        {0, 3, 7, 10},    // MIN7
        {0, 5, 7, 12}};   // SUS4
    return kSteps[chord < 0 ? 0 : (chord >= kNumChords ? kNumChords - 1 : chord)];
  }

  static const char *chordName(int chord) {
    static const char *kNames[kNumChords] = {"UNIS", "OCT",  "5TH",  "MAJ",
                                             "MIN",  "MAJ7", "MIN7", "SUS4"};
    return kNames[chord < 0 ? 0 : (chord >= kNumChords ? kNumChords - 1 : chord)];
  }

  void init(float *buffer) {
    for (int s = 0; s < kStrings; ++s) lines_[s] = buffer + s * kLineSize;
    for (uint32_t i = 0; i < kBufferSize; ++i) buffer[i] = 0.f;
    write_ = 0;

    note_ = 48.f;
    decay_ = 0.7f;
    mix_ = 0.5f;
    chord_ = kMinor7;
    damp_ = 0.6f;
    strings_ = kStrings;
    spread_ = 0.7f;
    tone_ = 0.6f;
    fine_ = 0.f;

    for (int s = 0; s < kStrings; ++s) lp_[s] = 0.f;
    inLp_ = 0.f;
    dcL_ = dcR_ = inHp_ = 0.f;
  }

  void reset() {
    if (!lines_[0]) return;
    for (int s = 0; s < kStrings; ++s) {
      for (uint32_t i = 0; i < kLineSize; ++i) lines_[s][i] = 0.f;
      lp_[s] = 0.f;
    }
    inLp_ = 0.f;
    dcL_ = dcR_ = inHp_ = 0.f;
  }

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_NOTE:
        /* 24..72 — two octaves below middle C up to an octave above. */
        note_ = 24.f + dsp::knob(value) * 48.f;
        break;
      case P_DECAY:
        decay_ = value * 0.01f;
        break;
      case P_MIX:
        mix_ = value * 0.01f;
        break;
      case P_CHORD:
        chord_ = static_cast<Chord>(
            static_cast<int>(dsp::clampf(0.f, static_cast<float>(value), kNumChords - 1.f)));
        break;
      case P_DAMP:
        damp_ = value * 0.01f;
        break;
      case P_STRINGS:
        strings_ = static_cast<int>(dsp::clampf(1.f, static_cast<float>(value), kStrings));
        break;
      case P_SPREAD:
        spread_ = value * 0.01f;
        break;
      case P_TONE:
        tone_ = value * 0.01f;
        break;
      case P_FINE:
        fine_ = static_cast<float>(value);  // cents
        break;
      default:
        break;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) {
    if (!lines_[0]) {
      for (uint32_t i = 0; i < frames * 2; ++i) out[i] = in[i];
      return;
    }

    const int8_t *steps = chordSteps(chord_);
    const float wet = mix_;
    const float dry = 1.f - mix_ * 0.5f;

    /*
     * The damping filter is the string. Its coefficient sets brightness *and*
     * ring time together, because the loop loses whatever the filter takes on
     * every pass — so DECAY and DAMP both end up here, one setting the floor
     * and the other the slope.
     */
    const float loopLp = dsp::clampf(0.12f, 0.30f + damp_ * 0.68f, 0.985f);
    const float feedback = dsp::clampf(0.f, 0.86f + decay_ * 0.136f, 0.9985f);

    /* A filter's worth of delay lives in the loop; subtract it or every string
     * rings flat, and the error grows as the note goes up. */
    const float loopDelay = (1.f - loopLp) / loopLp;

    float delay[kStrings];
    float panL[kStrings], panR[kStrings];
    for (int s = 0; s < kStrings; ++s) {
      const float hz = dsp::noteToHz(note_ + steps[s] + fine_ * (s - 1.5f) * 0.01f);
      delay[s] = dsp::clampf(4.f, dsp::kSampleRate / fmaxf(hz, 20.f) - loopDelay,
                             static_cast<float>(kLineSize - 2));
      /* Fan the strings across the image, low on the left. */
      const float pos = kStrings > 1 ? static_cast<float>(s) / (kStrings - 1) : 0.5f;
      const float p = 0.5f + (pos - 0.5f) * spread_;
      panL[s] = sqrtf(1.f - p);
      panR[s] = sqrtf(p);
    }

    /* Excitation tone: rolling the top off the input keeps the strings from
     * being driven by hiss they would then ring on forever. */
    const float inCoef = dsp::clampf(0.02f, tone_ * tone_, 1.f);
    const float norm = 1.f / sqrtf(static_cast<float>(strings_));

    for (uint32_t n = 0; n < frames; ++n) {
      const float inL = in[n * 2];
      const float inR = in[n * 2 + 1];
      const float mono = (inL + inR) * 0.5f;

      inLp_ += (mono - inLp_) * inCoef;
      /*
       * Block DC before it reaches the strings, not after. A resonator fed a
       * biased signal integrates that bias straight into its loop and then
       * sits on it — the ring is still there but it is riding on an offset
       * that eats the headroom the strings needed. Cleaning it up at the
       * output leaves the loop just as full.
       */
      inHp_ += (inLp_ - inHp_) * 0.0004f;
      const float drive = (inLp_ - inHp_) * 0.5f;

      float wetL = 0.f, wetR = 0.f;
      for (int s = 0; s < strings_; ++s) {
        float *line = lines_[s];

        const float d = delay[s];
        const uint32_t i0 = (write_ - static_cast<uint32_t>(d)) & kLineMask;
        const uint32_t i1 = (i0 - 1) & kLineMask;
        const float frac = d - static_cast<float>(static_cast<uint32_t>(d));
        const float sample = dsp::lerpf(line[i0], line[i1], frac);

        /* One-pole damping in the loop, then feedback. */
        lp_[s] += (sample - lp_[s]) * loopLp;
        line[write_ & kLineMask] = dsp::softclipf(lp_[s] * feedback + drive);

        wetL += sample * panL[s];
        wetR += sample * panR[s];
      }
      ++write_;

      wetL *= norm;
      wetR *= norm;

      /*
       * A resonator fed a signal with any offset in it will happily integrate
       * that offset into the loop and sit there with a DC bias, eating
       * headroom for the rest of the session. One-pole blockers on the way
       * out, and on the way in via the same path.
       */
      dcL_ += (wetL - dcL_) * 0.0004f;
      dcR_ += (wetR - dcR_) * 0.0004f;

      out[n * 2] = dsp::softclipf(inL * dry + (wetL - dcL_) * wet);
      out[n * 2 + 1] = dsp::softclipf(inR * dry + (wetR - dcR_) * wet);
    }
  }

 private:
  float *lines_[kStrings] = {nullptr, nullptr, nullptr, nullptr};
  uint32_t write_ = 0;

  float lp_[kStrings] = {0.f, 0.f, 0.f, 0.f};
  float inLp_ = 0.f;
  float dcL_ = 0.f, dcR_ = 0.f, inHp_ = 0.f;

  float note_ = 48.f;
  float decay_ = 0.7f;
  float mix_ = 0.5f;
  Chord chord_ = kMinor7;
  float damp_ = 0.6f;
  int strings_ = kStrings;
  float spread_ = 0.7f;
  float tone_ = 0.6f;
  float fine_ = 0.f;
};
