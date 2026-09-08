/*
 *  cz/dsp.h — phase distortion, eight presets, 8 voices.
 *
 *  The trick this borrows is from the Casio CZ line (1984): make a resonant
 *  filter sweep without owning a filter. A phase accumulator runs at the note
 *  frequency, a distortion function warps it, and the warped phase reads a
 *  cosine. Warp it so the cycle rushes through its first half and crawls
 *  through the second and the cosine comes out looking like a sawtooth; hold
 *  it flat in two places and you get a square. Sweep the amount of warping
 *  with an envelope — the "DCW" — and the spectrum opens and closes exactly
 *  the way a lowpass would, on a machine that has no lowpass in it.
 *
 *  The three resonant waves are the signature. They are a window shape times a
 *  cosine running at some multiple of the fundamental:
 *
 *      out = window(p) * cos(2*pi * n * p)
 *
 *  with n swept by the DCW envelope. That reads as a resonant peak climbing
 *  the spectrum. All three windows fall to zero at the end of the cycle, so a
 *  fractional n never leaves a step at the wrap — the buzz is harmonics, not a
 *  discontinuity.
 *
 *  Cheaper than anything else in this repo: a warp is a couple of compares and
 *  a multiply, and then one table read. No filter, no wavetable, no SDRAM —
 *  which matters, because oscillators here get none.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in osc.h / unit.cc.
 */
#pragma once

#include "dsp_util.h"
#include "lfo.h"
#include "opeg.h"
#include "sine.h"
#include "voice_pool.h"

class CzEngine {
 public:
  static constexpr int kMaxVoices = 8;
  static constexpr int kLines = 2;

  enum ParamId {
    P_ATTACK = 0,  // A knob: added on top of the preset's attack
    P_RELEASE,     // B knob: likewise for release
    P_PRESET,      // EDIT menu, where the display renders preset names
    P_DCW,         // depth of the phase distortion sweep: the filter knob
    P_DETUNE,
    P_LFO,
    P_VELO,
    P_VOICES,
    P_OCTAVE,
    P_COUNT
  };

  /* The eight distortion shapes. The first five morph from a plain cosine at
   * DCW 0; the resonant three are a window times a swept harmonic. */
  enum Wave {
    kSaw = 0,
    kSquare,
    kPulse,
    kDoubleSine,
    kSawPulse,
    kResoSaw,
    kResoTri,
    kResoTrap,
    kNumWaves
  };

  enum Preset {
    kReso = 0,
    kBrass,
    kEPiano,
    kBass,
    kBell,
    kStrings,
    kPipe,
    kVox,
    kNumPresets
  };

  struct LineSpec {
    int wave;
    float ratio, detune, level;
    float dcwLo, dcwHi;  // the DCW envelope maps 0..1 onto this range
    float velToDcw;      // velocity opens the sweep — the bark
    dsp::OpEG::Spec dcw;
    dsp::OpEG::Spec dca;
  };

  struct Patch {
    LineSpec line[kLines];
    float lfoHz, lfoPitch, lfoDcw, lfoDelayMs;
    float gain;
  };

  static const Patch &patch(int preset) {
    static const Patch kPatches[kNumPresets] = {
        /*
         * RESO — the sound the machine is remembered for. A resonant sawtooth
         * whose peak climbs on the attack and falls back, which on any other
         * synth would be a filter with the envelope on cutoff.
         */
        {{{kResoSaw, 1.00f, 0.f, 1.00f, 0.06f, 0.92f, 0.55f,
           {{1.f, 0.42f, 0.30f, 0.f}, {6.f, 700.f, 1.f, 500.f}},
           {{1.f, 0.90f, 0.85f, 0.f}, {3.f, 300.f, 1.f, 340.f}}},
          {kResoSaw, 1.00f, 7.f, 0.55f, 0.05f, 0.80f, 0.55f,
           {{1.f, 0.38f, 0.26f, 0.f}, {9.f, 800.f, 1.f, 520.f}},
           {{1.f, 0.88f, 0.82f, 0.f}, {5.f, 320.f, 1.f, 360.f}}}},
         5.1f, 0.f, 0.05f, 800.f,
         0.90f},

        /*
         * BRASS — a resonant trapezoid, which has more body under the peak
         * than the saw does, plus the swell: the DCW arrives after the level.
         */
        {{{kResoTrap, 1.00f, 0.f, 1.00f, 0.10f, 0.62f, 0.60f,
           {{1.f, 0.72f, 0.66f, 0.f}, {150.f, 420.f, 1.f, 260.f}},
           {{1.f, 0.90f, 0.86f, 0.f}, {55.f, 300.f, 1.f, 220.f}}},
          {kSaw, 2.00f, -4.f, 0.34f, 0.20f, 0.70f, 0.40f,
           {{1.f, 0.62f, 0.56f, 0.f}, {180.f, 440.f, 1.f, 260.f}},
           {{1.f, 0.86f, 0.80f, 0.f}, {70.f, 320.f, 1.f, 240.f}}}},
         4.8f, 0.05f, 0.04f, 650.f,
         0.82f},

        /*
         * EPIANO — the glassy one. A narrow pulse whose distortion collapses
         * fast, so the strike is bright and the tail is nearly a sine.
         */
        {{{kPulse, 1.00f, 0.f, 1.00f, 0.04f, 0.88f, 0.75f,
           {{1.f, 0.14f, 0.f, 0.f}, {1.f, 180.f, 700.f, 200.f}},
           {{1.f, 0.55f, 0.f, 0.f}, {2.f, 600.f, 2600.f, 420.f}}},
          {kDoubleSine, 2.00f, 5.f, 0.32f, 0.10f, 0.70f, 0.65f,
           {{1.f, 0.10f, 0.f, 0.f}, {1.f, 120.f, 450.f, 160.f}},
           {{1.f, 0.40f, 0.f, 0.f}, {2.f, 450.f, 1800.f, 380.f}}}},
         0.f, 0.f, 0.f, 0.f,
         0.95f},

        /*
         * BASS — the rubbery one. A resonant saw kept low in the sweep, with
         * the second line an octave down for weight.
         */
        {{{kResoSaw, 1.00f, 0.f, 1.00f, 0.05f, 0.46f, 0.70f,
           {{1.f, 0.30f, 0.16f, 0.f}, {2.f, 260.f, 900.f, 130.f}},
           {{1.f, 0.82f, 0.60f, 0.f}, {2.f, 400.f, 1600.f, 150.f}}},
          {kSquare, 0.50f, 0.f, 0.60f, 0.30f, 0.86f, 0.30f,
           {{1.f, 0.44f, 0.28f, 0.f}, {2.f, 300.f, 1100.f, 140.f}},
           {{1.f, 0.86f, 0.66f, 0.f}, {2.f, 420.f, 1700.f, 160.f}}}},
         0.f, 0.f, 0.f, 0.f,
         0.88f},

        /*
         * BELL — two lines at an inharmonic interval, both decaying, the
         * distortion dying faster than the level so the strike is the bright
         * part. Long tail.
         */
        {{{kSaw, 1.00f, 0.f, 1.00f, 0.03f, 0.80f, 0.60f,
           {{1.f, 0.16f, 0.f, 0.f}, {1.f, 260.f, 900.f, 400.f}},
           {{1.f, 0.52f, 0.f, 0.f}, {2.f, 900.f, 3400.f, 900.f}}},
          {kDoubleSine, 3.47f, 6.f, 0.42f, 0.05f, 0.62f, 0.70f,
           {{1.f, 0.08f, 0.f, 0.f}, {1.f, 150.f, 500.f, 250.f}},
           {{1.f, 0.30f, 0.f, 0.f}, {2.f, 600.f, 2200.f, 700.f}}}},
         0.f, 0.f, 0.f, 0.f,
         0.85f},

        /*
         * STRINGS — a resonant triangle held low and wide, slow on both ends,
         * with the two lines detuned far enough to shimmer without beating
         * into a null.
         */
        {{{kResoTri, 1.00f, -6.f, 1.00f, 0.12f, 0.48f, 0.30f,
           {{1.f, 0.66f, 0.60f, 0.f}, {320.f, 700.f, 1.f, 500.f}},
           {{1.f, 0.90f, 0.86f, 0.f}, {280.f, 640.f, 1.f, 480.f}}},
          {kResoTri, 2.00f, 7.f, 0.46f, 0.10f, 0.42f, 0.30f,
           {{1.f, 0.60f, 0.54f, 0.f}, {360.f, 740.f, 1.f, 520.f}},
           {{1.f, 0.86f, 0.82f, 0.f}, {320.f, 680.f, 1.f, 500.f}}}},
         4.3f, 0.04f, 0.10f, 900.f,
         0.78f},

        /*
         * PIPE — square and a square an octave up, distortion nearly static.
         * Flat as a drawbar: no sweep, no movement, on and off.
         */
        {{{kSquare, 1.00f, 0.f, 1.00f, 0.60f, 0.72f, 0.10f,
           {{1.f, 0.96f, 0.94f, 0.f}, {12.f, 120.f, 1.f, 90.f}},
           {{1.f, 0.96f, 0.94f, 0.f}, {14.f, 100.f, 1.f, 80.f}}},
          {kSquare, 2.00f, 3.f, 0.52f, 0.55f, 0.66f, 0.10f,
           {{1.f, 0.94f, 0.92f, 0.f}, {14.f, 130.f, 1.f, 90.f}},
           {{1.f, 0.94f, 0.92f, 0.f}, {16.f, 110.f, 1.f, 80.f}}}},
         0.f, 0.f, 0.f, 0.f,
         0.72f},

        /*
         * VOX — a resonant trapezoid parked high with almost no sweep, which
         * puts a fixed peak up in the formant range and reads as a vowel. The
         * second line sits a fifth up and keeps still.
         */
        {{{kResoTrap, 1.00f, 0.f, 1.00f, 0.52f, 0.68f, 0.25f,
           {{1.f, 0.86f, 0.80f, 0.f}, {90.f, 320.f, 1.f, 300.f}},
           {{1.f, 0.92f, 0.88f, 0.f}, {70.f, 280.f, 1.f, 280.f}}},
          {kResoSaw, 1.50f, 4.f, 0.38f, 0.44f, 0.58f, 0.25f,
           {{1.f, 0.82f, 0.76f, 0.f}, {110.f, 340.f, 1.f, 300.f}},
           {{1.f, 0.80f, 0.74f, 0.f}, {95.f, 300.f, 1.f, 290.f}}}},
         5.4f, 0.03f, 0.06f, 500.f,
         0.80f}};
    return kPatches[preset < 0 ? 0 : (preset >= kNumPresets ? kNumPresets - 1 : preset)];
  }

  static const char *presetName(int preset) {
    static const char *kNames[kNumPresets] = {"RESO", "BRAS", "EPNO", "BASS",
                                              "BELL", "STRG", "PIPE", "VOX"};
    return kNames[preset < 0 ? 0 : (preset >= kNumPresets ? kNumPresets - 1 : preset)];
  }

  /* Test hook: the shape functions in isolation. */
  float probeShape(int wave, float p, float amount, float resoCap) const {
    return shape(wave, p, amount, resoCap);
  }

  void init() {
    sine_.init();
    pool_.init();
    lfo_.init(0.f);

    dcwScale_ = 1.f;
    detuneScale_ = 1.f;
    lfoScale_ = 1.f;
    velSens_ = 0.6f;
    octave_ = 0;
    attackAddMs_ = 0.f;
    releaseAddMs_ = 0.f;
    bendSemis_ = 0.f;
    dcwMod_ = 0.f;
    runtimeNote_ = 60;
    lfoValue_ = 0.f;

    loadPreset(kReso);
  }

  void reset() { pool_.killAll(); }

  int preset() const { return preset_; }

  // ---- parameters ---------------------------------------------------------

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_PRESET:
        loadPreset(static_cast<Preset>(static_cast<int>(
            dsp::clampf(0.f, static_cast<float>(value), kNumPresets - 1.f))));
        break;
      case P_ATTACK: {
        const float k = dsp::knob(value);
        attackAddMs_ = k * k * 2000.f;
        applyEnvelopes();
        break;
      }
      case P_RELEASE: {
        const float k = dsp::knob(value);
        releaseAddMs_ = k * k * 3000.f;
        applyEnvelopes();
        break;
      }
      case P_DCW:
        dcwScale_ = dsp::knob(value) * 2.f;  // 512 is the preset's own sweep
        break;
      case P_DETUNE:
        detuneScale_ = value * 0.02f;
        break;
      case P_LFO:
        lfoScale_ = value * 0.02f;
        break;
      case P_VELO:
        velSens_ = value * 0.01f;
        break;
      case P_VOICES:
        pool_.setCount(value);
        break;
      case P_OCTAVE:
        octave_ = value;
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
    a.voice->velocity = velo * (1.f / 127.f);
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

  /* The hardware shape LFO nudges the distortion depth. */
  void setToneMod(float mod) { dcwMod_ = mod; }

  // ---- audio --------------------------------------------------------------

  void process(float *__restrict out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = 0.f;

    const Patch &p = patch(preset_);
    const float transpose = static_cast<float>(octave_) * 12.f + bendSemis_;
    const float dcwDepth = dsp::clampf(0.f, dcwScale_ + dcwMod_ * 0.25f, 2.f);

    const float lfoPitch = p.lfoPitch * lfoScale_;
    const float lfoDcw = p.lfoDcw * lfoScale_;
    const bool lfoOn = p.lfoHz > 0.f && (lfoPitch > 0.f || lfoDcw > 0.f);
    const float lfoDelayFrames = p.lfoDelayMs * 0.001f * dsp::kSampleRate;

    for (int v = 0; v < pool_.count(); ++v) {
      Voice &vo = pool_[v];
      if (vo.env.isIdle()) continue;

      const float f0 = dsp::noteToHz(vo.note + transpose);

      float inc[kLines];
      float resoCap[kLines];
      for (int l = 0; l < kLines; ++l) {
        const LineSpec &ls = p.line[l];
        const float cents = ls.detune * detuneScale_ * (1.f / 1200.f);
        const float hz = f0 * ls.ratio * dsp::pow2f_fast(cents);
        inc[l] = hz * dsp::kSampleRateRecip;
        /*
         * A resonant wave puts its peak at n times the note. Cap n so that
         * peak stays under Nyquist, or the sweep runs off the top of the band
         * and folds back down through the note it came from.
         */
        resoCap[l] = 0.45f * dsp::kSampleRate / fmaxf(hz, 1.f);
      }

      const float gain = p.gain * 0.26f;

      for (uint32_t n = 0; n < frames; ++n) {
        float pitchScale = 1.f;
        float dcwLfo = 0.f;
        if (lfoOn) {
          if (vo.lfoRamp < lfoDelayFrames) vo.lfoRamp += 1.f;
          const float ramp = lfoDelayFrames > 0.f
                                 ? dsp::clampf(0.f, vo.lfoRamp / lfoDelayFrames, 1.f)
                                 : 1.f;
          const float l = lfoValue_ * ramp;
          if (lfoPitch > 0.f) pitchScale = dsp::pow2f_fast(l * lfoPitch * (1.f / 12.f));
          dcwLfo = l * lfoDcw;
        }

        float sum = 0.f;
        for (int l = 0; l < kLines; ++l) {
          const LineSpec &ls = p.line[l];
          const float amp = vo.env.dca[l].next();
          const float sweep = vo.env.dcw[l].next();
          if (amp <= 0.f) continue;

          /* Velocity opens the sweep rather than just the level: on this kind
           * of synth digging in is a brightness control. */
          const float vel = dsp::lerpf(1.f, vo.velocity, ls.velToDcw * velSens_);
          float amount = dsp::lerpf(ls.dcwLo, ls.dcwHi, sweep) * vel * dcwDepth + dcwLfo;
          amount = dsp::clampf(0.f, amount, 0.995f);

          vo.phase[l] += inc[l] * pitchScale;
          if (vo.phase[l] >= 1.f) vo.phase[l] -= 1.f;

          sum += shape(ls.wave, vo.phase[l], amount, resoCap[l]) * ls.level * amp *
                 vo.velocity;
        }

        out[n] += sum * gain;
      }

      vo.env.refresh();
      if (vo.env.isIdle()) vo.held = false;
    }

    if (lfoOn)
      for (uint32_t n = 0; n < frames; ++n) lfoValue_ = lfo_.next();

    for (uint32_t i = 0; i < frames; ++i) out[i] = dsp::softclipf(out[i]);
  }

  int activeVoices() const { return pool_.active(); }

 private:
  /* cos(2*pi*x) from the shared sine table. */
  inline float cosAt(float x) const {
    return sine_.at(x + 0.25f + dsp::SineTable::kPhaseBias);
  }

  /*
   * The distortion itself. `amount` is 0..1: zero is an undistorted cosine for
   * the five morphing shapes, and the bottom of the sweep for the resonant
   * three. `resoCap` is the highest harmonic the note can carry.
   */
  inline float shape(int wave, float p, float amount, float resoCap) const {
    switch (wave) {
      case kSaw: {
        /* Move the half-cycle point down: the first half of the cosine happens
         * in a shrinking slice of the period and the rest stretches out. */
        const float k = 0.5f - 0.49f * amount;
        const float w = p < k ? 0.5f * p / k : 0.5f + 0.5f * (p - k) / (1.f - k);
        return cosAt(w);
      }
      case kSquare:
        /* Flat, edge, flat, edge. The transitions sit at a quarter and three
         * quarters and sharpen as the amount rises; at zero the map is the
         * identity and this is a plain cosine. */
        return cosAt(edges(p, 0.25f, 0.75f, amount));
      case kPulse:
        /* The same, with the transitions pulled together as the amount rises,
         * so the high part narrows and the even harmonics come in. */
        return cosAt(edges(p, dsp::lerpf(0.25f, 0.11f, amount),
                           dsp::lerpf(0.75f, 0.53f, amount), amount));
      case kDoubleSine: {
        /* Run the phase faster than the period: a second cycle grows in. */
        float w = p * (1.f + amount);
        w -= static_cast<float>(static_cast<int>(w));
        return cosAt(w);
      }
      case kSawPulse: {
        /* A saw with its tail windowed away, which is what puts a pulse edge
         * on it. The window has a ramp rather than a step — a step here is a
         * click at the fundamental on every cycle. */
        const float k = 0.5f - 0.45f * amount;
        const float w = p < k ? 0.5f * p / k : 0.5f + 0.5f * (p - k) / (1.f - k);
        const float d = 1.f - 0.7f * amount;
        const float win = dsp::clampf(0.f, (d - p) * 20.f + 1.f, 1.f);
        return cosAt(w) * win;
      }
      case kResoSaw:
        return (1.f - p) * cosAt(p * resoIndex(amount, resoCap));
      case kResoTri: {
        const float win = 1.f - fabsf(2.f * p - 1.f);
        return win * cosAt(p * resoIndex(amount, resoCap));
      }
      case kResoTrap:
      default: {
        const float win = p < 0.5f ? 1.f : 2.f * (1.f - p);
        return win * cosAt(p * resoIndex(amount, resoCap));
      }
    }
  }

  /*
   * Phase map with two transitions, at t0 and t1. The edge width shrinks with
   * the amount, and at amount 0 it is wide enough that the map is exactly the
   * identity — so every shape built on this starts life as a plain cosine.
   * Both branches meet at 0.5, so there is no step at the crossover.
   */
  static inline float edges(float p, float t0, float t1, float amount) {
    const float e = 0.5f - 0.49f * amount;
    const float mid = 0.5f * (t0 + t1);
    if (p < mid) return 0.5f * dsp::clampf(0.f, (p - t0) / e + 0.5f, 1.f);
    return 0.5f + 0.5f * dsp::clampf(0.f, (p - t1) / e + 0.5f, 1.f);
  }

  /* Resonant peak position, in harmonics of the note. */
  static inline float resoIndex(float amount, float cap) {
    const float n = 1.f + amount * amount * 30.f;
    return n > cap ? fmaxf(1.f, cap) : n;
  }

  /*
   * The pool drives a voice through `env`. Each line has an amplitude envelope
   * and a distortion envelope; only the amplitude ones decide whether the
   * voice is still sounding, but both have to be gated together.
   */
  struct VoiceEnv {
    dsp::OpEG dca[kLines];
    dsp::OpEG dcw[kLines];

    void init() {
      for (int l = 0; l < kLines; ++l) {
        dca[l].init();
        dcw[l].init();
      }
      idle_ = true;
      level_ = 0.f;
    }

    void gateOn(bool retrigger) {
      for (int l = 0; l < kLines; ++l) {
        dca[l].gateOn(retrigger);
        dcw[l].gateOn(retrigger);
      }
      idle_ = false;
      level_ = 1.f;
    }

    void gateOff() {
      for (int l = 0; l < kLines; ++l) {
        dca[l].gateOff();
        dcw[l].gateOff();
      }
    }

    void kill() {
      for (int l = 0; l < kLines; ++l) {
        dca[l].kill();
        dcw[l].kill();
      }
      idle_ = true;
      level_ = 0.f;
    }

    void refresh() {
      bool live = false;
      float peak = 0.f;
      for (int l = 0; l < kLines; ++l) {
        if (!dca[l].isIdle()) live = true;
        peak = fmaxf(peak, dca[l].value());
      }
      idle_ = !live;
      level_ = peak;
    }

    bool isIdle() const { return idle_; }
    float value() const { return level_; }

   private:
    bool idle_ = true;
    float level_ = 0.f;
  };

  struct Voice {
    float phase[kLines] = {0.f, 0.f};
    VoiceEnv env;
    float lfoRamp = 0.f;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      for (int l = 0; l < kLines; ++l) {
        float ph = index * 0.09f + l * 0.31f;
        phase[l] = ph - static_cast<float>(static_cast<int>(ph));
      }
      lfoRamp = 0.f;
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      if (retrigger) lfoRamp = 0.f;
      env.gateOn(retrigger);
    }

    void kill() {
      env.kill();
      held = false;
    }
  };

  void loadPreset(Preset p) {
    preset_ = p;
    applyEnvelopes();
  }

  void applyEnvelopes() {
    const Patch &p = patch(preset_);
    for (int v = 0; v < kMaxVoices; ++v)
      for (int l = 0; l < kLines; ++l) {
        pool_[v].env.dca[l].setSpec(p.line[l].dca, attackAddMs_, releaseAddMs_);
        pool_[v].env.dcw[l].setSpec(p.line[l].dcw, attackAddMs_, releaseAddMs_);
      }
    lfo_.setRate(p.lfoHz);
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;
  dsp::SineTable sine_;
  dsp::SineLfo lfo_;

  Preset preset_ = kReso;
  float lfoValue_ = 0.f;

  float dcwScale_ = 1.f;
  float detuneScale_ = 1.f;
  float lfoScale_ = 1.f;
  float velSens_ = 0.6f;
  int32_t octave_ = 0;
  float attackAddMs_ = 0.f;
  float releaseAddMs_ = 0.f;
  float bendSemis_ = 0.f;
  float dcwMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
};
