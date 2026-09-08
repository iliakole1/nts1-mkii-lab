/*
 *  wt/dsp.h — wavetable scanning, 6 voices, two oscillators each.
 *
 *  The gesture the early wavetable machines were built around: a bank of
 *  single-cycle waves laid out as a progression, and a pointer that moves
 *  through it while the note sounds. Sweeping the pointer is not a filter
 *  opening and not a modulator arriving — it is the waveform itself being
 *  replaced, continuously, which is why it sounds like nothing else.
 *
 *  The table is *generated* at load rather than shipped. Oscillators here get
 *  no SDRAM and about 48 KB for everything, so a stored bank is the one thing
 *  that will not fit; but a bank is only a list of harmonic recipes, and those
 *  cost nothing to write down. 32 waves in four zones: sine opening into a
 *  sawtooth, sawtooth closing into a square, square narrowing into a pulse,
 *  and a resonant bump climbing the harmonic series for the vocal end.
 *
 *  Aliasing is the whole difficulty with wavetables. There is no cap to apply
 *  the way FM or phase distortion allows, because every harmonic is already
 *  in the table — so each wave is built four times, band-limited to 32, 16, 8
 *  and 4 harmonics, and the note picks the widest one that still fits under
 *  Nyquist. That is 16 KB, which is why the table is 8-bit: the machines this
 *  borrows from were too, and it is the difference between fitting and not.
 *
 *  Storage comes from the caller. Held as a member it would be emitted as
 *  initialised .data and cost 16 KB of the budget in zeros — see
 *  docs/platform.md, and units/osc/pluck, which learned it the hard way.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in osc.h / unit.cc.
 */
#pragma once

#include "adsr.h"
#include "dsp_util.h"
#include "sine.h"
#include "svf.h"
#include "voice_pool.h"

class WtEngine {
 public:
  static constexpr int kMaxVoices = 6;
  static constexpr int kOscs = 2;

  static constexpr int kWaves = 32;
  static constexpr int kLen = 128;  // power of two: the read index masks
  static constexpr int kMask = kLen - 1;
  static constexpr int kLevels = 4;
  /* Harmonics kept at each band-limited level, widest first. A function rather
   * than an array: subscripting a static constexpr array member odr-uses it,
   * which in C++14 needs an out-of-line definition this header cannot have. */
  static constexpr int levelHarmonics(int i) {
    return i == 0 ? 32 : (i == 1 ? 16 : (i == 2 ? 8 : 4));
  }
  static constexpr int kMaxHarmonics = 32;

  /* Bytes of table storage the caller must supply. */
  static constexpr uint32_t kBufferSize = kWaves * kLevels * kLen;

  enum ParamId {
    P_WAVE = 0,  // A knob: position in the table
    P_CUTOFF,    // B knob
    P_SWEEP,     // envelope -> wave position, bipolar
    P_ATTACK,
    P_DECAY,
    P_SUSTAIN,
    P_RELEASE,
    P_EGFILTER,
    P_DETUNE,
    P_VOICES,
    P_COUNT
  };

  /*
   * Fill a table. Deterministic and identical for every instance, so the
   * caller builds it once and hands the same buffer to init().
   */
  static void buildTable(int8_t *dest) {
    dsp::SineTable sine;
    sine.init();

    float scratch[kLen];
    for (int w = 0; w < kWaves; ++w) {
      float amp[kMaxHarmonics + 1];
      recipe(w, amp);

      for (int lvl = 0; lvl < kLevels; ++lvl) {
        const int maxH = levelHarmonics(lvl);
        for (int i = 0; i < kLen; ++i) scratch[i] = 0.f;

        for (int h = 1; h <= maxH; ++h) {
          const float a = amp[h];
          if (a == 0.f) continue;
          for (int i = 0; i < kLen; ++i) {
            const float ph = static_cast<float>(h) * static_cast<float>(i) /
                             static_cast<float>(kLen);
            scratch[i] += a * sine.at(ph + dsp::SineTable::kPhaseBias);
          }
        }

        /* Normalise per level: dropping harmonics changes the peak, and a
         * level that is quieter than its neighbour makes the octave boundary
         * audible as a step in loudness. */
        float peak = 1e-6f;
        for (int i = 0; i < kLen; ++i) peak = fmaxf(peak, fabsf(scratch[i]));
        const float norm = 127.f / peak;
        int8_t *row = dest + (w * kLevels + lvl) * kLen;
        for (int i = 0; i < kLen; ++i) {
          const float v = scratch[i] * norm;
          row[i] = static_cast<int8_t>(v < -127.f ? -127.f : (v > 127.f ? 127.f : v));
        }
      }
    }
  }

  void init(const int8_t *table) {
    table_ = table;
    pool_.init();

    wave_ = 0.f;
    cutoffKnob_ = 0.8f;
    sweep_ = 0.f;
    attackMs_ = 4.f;
    decayMs_ = 500.f;
    sustain_ = 0.7f;
    releaseMs_ = 300.f;
    egFilter_ = 0.25f;
    detuneCents_ = 8.f;
    bendSemis_ = 0.f;
    waveMod_ = 0.f;
    runtimeNote_ = 60;
    applyEnvelopeSettings();
  }

  void reset() { pool_.killAll(); }

  // ---- parameters ---------------------------------------------------------

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_WAVE:
        wave_ = dsp::knob(value);
        break;
      case P_CUTOFF:
        cutoffKnob_ = dsp::knob(value);
        break;
      case P_SWEEP:
        sweep_ = value * 0.01f;  // -100..100 %
        break;
      case P_ATTACK:
        attackMs_ = static_cast<float>(value);
        applyEnvelopeSettings();
        break;
      case P_DECAY:
        decayMs_ = static_cast<float>(value);
        applyEnvelopeSettings();
        break;
      case P_SUSTAIN:
        sustain_ = value * 0.01f;
        applyEnvelopeSettings();
        break;
      case P_RELEASE:
        releaseMs_ = static_cast<float>(value);
        applyEnvelopeSettings();
        break;
      case P_EGFILTER:
        egFilter_ = value * 0.01f;
        break;
      case P_DETUNE:
        detuneCents_ = static_cast<float>(value);
        break;
      case P_VOICES:
        pool_.setCount(value);
        break;
      default:
        break;
    }
  }

  // ---- note events --------------------------------------------------------

  static constexpr uint8_t kGateNote = 0xFFU;

  void setRuntimeNote(uint8_t note) { runtimeNote_ = note; }

  void noteOn(uint8_t note, uint8_t velo) {
    if (note == kGateNote) note = runtimeNote_;
    if (velo == 0) {
      if (pool_.findHeld(note)) {
        noteOff(note);
        return;
      }
      velo = 100;
    }
    const Pool::Assignment a = pool_.noteOn(note);
    a.voice->velocity = 0.3f + 0.7f * (velo * (1.f / 127.f));
    a.voice->start(a.retrigger);
  }

  void noteOff(uint8_t note) {
    if (note == kGateNote) {
      allNoteOff();
      return;
    }
    pool_.noteOff(note);
  }

  void allNoteOff() { pool_.allNoteOff(); }

  void setBend(uint16_t bend) {
    bendSemis_ = (static_cast<int32_t>(bend) - 8192) * (2.f / 8192.f);
  }

  /* The hardware shape LFO moves the table pointer, which is the good place
   * for it on this kind of oscillator. */
  void setWaveMod(float mod) { waveMod_ = mod; }

  // ---- audio --------------------------------------------------------------

  void process(float *__restrict out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = 0.f;
    if (!table_) return;

    const float baseCutoff = 20.f * dsp::pow2f_fast(cutoffKnob_ * 9.8f);
    const float detune = detuneCents_ * (1.f / 1200.f);
    const float basePos = dsp::clampf(0.f, wave_ + waveMod_ * 0.25f, 1.f);

    for (int v = 0; v < pool_.count(); ++v) {
      Voice &vo = pool_[v];
      if (vo.env.isIdle()) continue;

      const float hz = dsp::noteToHz(vo.note + bendSemis_);
      const float f[kOscs] = {hz * dsp::pow2f_fast(-detune), hz * dsp::pow2f_fast(detune)};

      /*
       * Pick the widest band-limited copy that still fits under Nyquist. One
       * choice per block per oscillator: the note cannot move far enough
       * inside 64 samples to want a different one, and switching mid-block
       * would put a step in the output.
       */
      int level[kOscs];
      float inc[kOscs];
      for (int o = 0; o < kOscs; ++o) {
        inc[o] = f[o] * dsp::kSampleRateRecip;
        const float maxH = 0.45f * dsp::kSampleRate / fmaxf(f[o], 1.f);
        int l = kLevels - 1;
        for (int i = 0; i < kLevels; ++i)
          if (static_cast<float>(levelHarmonics(i)) <= maxH) {
            l = i;
            break;
          }
        level[o] = l;
      }

      const float keyTrack = (vo.note - 60) * (0.3f / 12.f);
      const float envOct = egFilter_ * 6.f * vo.env.value();
      vo.filter.setCutoff(baseCutoff * dsp::pow2f_fast(keyTrack + envOct), 0.25f);

      const float gain = vo.velocity * 0.28f;

      for (uint32_t n = 0; n < frames; ++n) {
        const float e = vo.env.next();

        /* The envelope moves the pointer as well as the level — that sweep is
         * the instrument. Bipolar, so it can travel either way. */
        const float pos = dsp::clampf(0.f, basePos + sweep_ * e, 1.f);

        float mix = 0.f;
        for (int o = 0; o < kOscs; ++o) {
          mix += readWave(pos, level[o], vo.phase[o]);
          vo.phase[o] += inc[o];
          if (vo.phase[o] >= 1.f) vo.phase[o] -= 1.f;
        }

        out[n] += vo.filter.lowpass(mix * 0.5f) * e * gain;
      }
    }

    for (uint32_t i = 0; i < frames; ++i) out[i] = dsp::softclipf(out[i]);
  }

  int activeVoices() const { return pool_.active(); }

 private:
  /*
   * The harmonic recipe for one wave. Four zones of eight, each morphing into
   * the next, so the pointer travelling the table is a continuous change of
   * timbre rather than a series of jumps.
   */
  static void recipe(int w, float *amp) {
    for (int h = 0; h <= kMaxHarmonics; ++h) amp[h] = 0.f;

    const int zone = w / 8;
    const float t = static_cast<float>(w % 8) / 7.f;

    for (int h = 1; h <= kMaxHarmonics; ++h) {
      const float saw = 1.f / static_cast<float>(h);
      const float square = (h & 1) ? 1.f / static_cast<float>(h) : 0.f;
      float a = 0.f;
      switch (zone) {
        case 0:
          /* Sine opening into a sawtooth: the fundamental stays, everything
           * above it fades up. */
          a = (h == 1) ? 1.f : t * saw;
          break;
        case 1:
          /* Sawtooth closing into a square: the even harmonics fade out. */
          a = dsp::lerpf(saw, square, t);
          break;
        case 2: {
          /* Square narrowing into a pulse. A pulse of duty d has harmonic
           * amplitudes sin(pi*h*d)/(pi*h), which at d = 0.5 is the square. */
          const float duty = dsp::lerpf(0.5f, 0.12f, t);
          const float x = dsp::kPi * static_cast<float>(h) * duty;
          a = sinf(x) / (dsp::kPi * static_cast<float>(h));
          break;
        }
        default: {
          /* A resonant bump climbing the series, over a thin sawtooth floor.
           * This is the vocal end of the table. */
          const float centre = dsp::lerpf(2.5f, 13.f, t);
          const float d = (static_cast<float>(h) - centre) / 2.2f;
          a = 0.22f * saw + expf(-d * d) * 0.9f / (1.f + 0.12f * centre);
          break;
        }
      }
      amp[h] = a;
    }
  }

  /* Two neighbouring waves, linearly interpolated, each read with linear
   * interpolation of its own. */
  inline float readWave(float pos, int level, float phase) const {
    const float wf = pos * static_cast<float>(kWaves - 1);
    const int w0 = static_cast<int>(wf);
    const int w1 = w0 < kWaves - 1 ? w0 + 1 : w0;
    const float wfrac = wf - static_cast<float>(w0);

    const float x = phase * static_cast<float>(kLen);
    const int i0 = static_cast<int>(x) & kMask;
    const int i1 = (i0 + 1) & kMask;
    const float xfrac = x - static_cast<float>(static_cast<int>(x));

    const int8_t *r0 = table_ + (w0 * kLevels + level) * kLen;
    const int8_t *r1 = table_ + (w1 * kLevels + level) * kLen;
    const float a = dsp::lerpf(r0[i0], r0[i1], xfrac);
    const float b = dsp::lerpf(r1[i0], r1[i1], xfrac);
    return dsp::lerpf(a, b, wfrac) * (1.f / 127.f);
  }

  struct Voice {
    float phase[kOscs] = {0.f, 0.f};
    dsp::SVF filter;
    dsp::ADSR env;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      filter.reset();
      for (int o = 0; o < kOscs; ++o) {
        float ph = index * 0.13f + o * 0.29f;
        phase[o] = ph - static_cast<float>(static_cast<int>(ph));
      }
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      if (retrigger) filter.reset();
      env.gateOn(retrigger);
    }

    void kill() {
      env.kill();
      filter.reset();
      held = false;
    }
  };

  void applyEnvelopeSettings() {
    for (int i = 0; i < kMaxVoices; ++i) {
      pool_[i].env.setAttack(attackMs_);
      pool_[i].env.setDecay(decayMs_);
      pool_[i].env.setSustain(sustain_);
      pool_[i].env.setRelease(releaseMs_);
    }
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;

  const int8_t *table_ = nullptr;

  float wave_ = 0.f;
  float cutoffKnob_ = 0.8f;
  float sweep_ = 0.f;
  float attackMs_ = 4.f;
  float decayMs_ = 500.f;
  float sustain_ = 0.7f;
  float releaseMs_ = 300.f;
  float egFilter_ = 0.25f;
  float detuneCents_ = 8.f;
  float bendSemis_ = 0.f;
  float waveMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
};
