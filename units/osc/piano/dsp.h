/*
 *  piano/dsp.h — five struck-string presets, 6 voices, additive.
 *
 *  FM makes a decent electric piano (see units/rhodes) and a poor acoustic one:
 *  its partials are locked to integer ratios and all decay together, which is
 *  exactly what an acoustic piano does not do. This is additive instead, and
 *  three properties do the work:
 *
 *  INHARMONICITY  real strings are stiff, so partial n does not sit at n*f0 but
 *                 at n*f0*sqrt(1 + B*n^2). B is small for a long grand string
 *                 and large for a short one — push it far enough and you get
 *                 the toy piano's clangy, bell-like spread.
 *
 *  PARTIAL DECAY  high partials die away much faster than the fundamental. This
 *                 is most of what makes a piano note sound struck: bright for a
 *                 moment, then a settling hum. One decay rate for everything
 *                 sounds like an organ with an envelope on it.
 *
 *  UNISON BEAT    each note has two or three strings, never quite in tune. A
 *                 slow LFO moving partial groups in opposite directions stands
 *                 in for that, and turned up it becomes the honky-tonk.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in osc.h / unit.cc.
 */
#pragma once

#include "adsr.h"
#include "dsp_util.h"
#include "lfo.h"
#include "lofi.h"
#include "noise.h"
#include "sine.h"
#include "voice_pool.h"

class PianoEngine {
 public:
  static constexpr int kMaxVoices = 6;
  static constexpr int kNumPartials = 8;

  enum ParamId {
    P_ATTACK = 0,  // A knob: added on top of the preset's attack
    P_RELEASE,     // B knob: likewise for release
    P_PRESET,      // EDIT menu, where the display renders preset names
    P_TONE,
    P_LOFI,
    P_VELO,
    P_VOICES,
    P_COUNT
  };

  enum Preset { kGrand = 0, kBright, kMellow, kHonky, kToy, kNumPresets };

  void init() {
    sine_.init();
    pool_.init();
    noise_.seed(0x51A7C3EU);
    lofi_.reset();

    tone_ = 0.5f;
    velSens_ = 0.6f;
    attackAddMs_ = 0.f;
    releaseAddMs_ = 0.f;
    bendSemis_ = 0.f;
    toneMod_ = 0.f;
    runtimeNote_ = 60;
    lofiAmount_ = 0.25f;
    lofi_.setAmount(lofiAmount_);
    loadPreset(kGrand);
  }

  void reset() {
    pool_.killAll();
    lofi_.reset();
  }

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_PRESET:
        loadPreset(static_cast<Preset>(static_cast<int>(
            dsp::clampf(0.f, static_cast<float>(value), kNumPresets - 1.f))));
        break;
      case P_TONE:
        tone_ = dsp::knob(value);
        break;
      /*
       * ATK and REL add to whatever the preset asked for rather than replacing
       * it, so a preset keeps its character and you stretch it from there. The
       * square curve puts the useful short end across most of the knob travel.
       */
      case P_ATTACK: {
        const float k = dsp::knob(value);
        attackAddMs_ = k * k * 2000.f;
        loadPreset(preset_);  // re-applies the envelope times
        break;
      }
      case P_RELEASE: {
        const float k = dsp::knob(value);
        releaseAddMs_ = k * k * 3000.f;
        loadPreset(preset_);
        break;
      }
      case P_LOFI:
        lofiAmount_ = value * 0.01f;
        lofi_.setAmount(lofiAmount_);
        break;
      case P_VELO:
        velSens_ = value * 0.01f;
        break;
      case P_VOICES:
        pool_.setCount(value);
        break;
      default:
        break;
    }
  }

  Preset preset() const { return preset_; }

  // ---- note events --------------------------------------------------------

  static constexpr uint8_t kGateNote = 0xFFU;

  void setRuntimeNote(uint8_t note) { runtimeNote_ = note; }

  void noteOn(uint8_t note, uint8_t velo) {
    if (note == kGateNote) note = runtimeNote_;
    if (velo == 0) {  // see docs/polyphony.md
      if (pool_.findHeld(note)) {
        noteOff(note);
        return;
      }
      velo = 100;
    }
    const Pool::Assignment a = pool_.noteOn(note);
    Voice &v = *a.voice;
    v.velocity = velo * (1.f / 127.f);
    v.start(a.retrigger);
    strike(v);
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

  void setToneMod(float mod) { toneMod_ = mod; }

  // ---- audio --------------------------------------------------------------

  void process(float *__restrict out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = 0.f;

    /* TONE tilts the partial balance: dark and woody, or open and bright. */
    const float tone = dsp::clampf(0.f, tone_ + toneMod_, 1.f);
    const float tilt = 0.45f + 1.3f * tone;

    for (int i = 0; i < pool_.count(); ++i) {
      Voice &v = pool_[i];
      if (v.env.isIdle()) continue;

      /* Bend retunes the whole partial set, keeping the stretch intact. */
      const float bendRatio = dsp::pow2f_fast(bendSemis_ * (1.f / 12.f));
      const float gain = dsp::lerpf(1.f, v.velocity, velSens_) * 0.30f * p_.outGain;

      for (uint32_t n = 0; n < frames; ++n) {
        const float e = v.env.next();
        v.noiseEnv *= noiseCoef_;

        /* Unison beat: partial groups pull against each other. */
        const float beat = v.beatLfo.next() * p_.beatDepth;

        float s = 0.f;
        for (int k = 0; k < v.numPartials; ++k) {
          const float amp = v.amp[k];
          if (amp > 1e-5f) {
            const float group = (k & 1) ? (1.f + beat) : (1.f - beat);
            s += amp * group * sine_.at(v.phase[k] + dsp::SineTable::kPhaseBias);
          }
          v.amp[k] *= v.coef[k];
          v.phase[k] += v.inc[k] * bendRatio;
          if (v.phase[k] >= 1.f) v.phase[k] -= 1.f;
        }

        /* Hammer: a short noise thunk under the attack. */
        s += noise_.next() * v.noiseEnv * v.noiseEnv * p_.hammer;

        out[n] += s * e * gain * tilt;
      }

      /* The fundamental is the slowest partial; once it is 60 dB down the note
       * is over, and holding the voice any longer just costs polyphony. */
      if (v.amp[0] < 1e-3f) v.env.kill();
    }

    for (uint32_t n = 0; n < frames; ++n) out[n] = lofi_.process(dsp::softclipf(out[n]));
  }

  int activeVoices() const { return pool_.active(); }

 private:
  struct Params {
    float inharmonic;   // B in n*f0*sqrt(1 + B*n^2)
    float rolloff;      // partial n starts at 1/n^rolloff
    float velBright;    // extra roll-off when played softly
    float decayMs;      // fundamental decay
    float decaySpread;  // how much faster each higher partial dies
    float beatDepth;    // unison detune, as amplitude push-pull
    float beatRateHz;
    float hammer;
    float releaseMs;
    float outGain;
  };

  struct Voice {
    dsp::ADSR env;
    dsp::SineLfo beatLfo;
    float phase[kNumPartials] = {0.f};
    float inc[kNumPartials] = {0.f};
    float amp[kNumPartials] = {0.f};
    float coef[kNumPartials] = {0.f};
    int numPartials = 0;  // partials below Nyquist for this note
    float noiseEnv = 0.f;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      beatLfo.init(index * 0.17f);
      for (int k = 0; k < kNumPartials; ++k) {
        phase[k] = 0.f;
        inc[k] = 0.f;
        amp[k] = 0.f;
        coef[k] = 0.f;
      }
      numPartials = 0;
      noiseEnv = 0.f;
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      (void)retrigger;
      noiseEnv = 1.f;
      env.gateOn(true);  // every strike is a fresh one
    }

    void kill() {
      env.kill();
      for (int k = 0; k < kNumPartials; ++k) amp[k] = 0.f;
      noiseEnv = 0.f;
      held = false;
    }
  };

  static float decayCoefFor(float ms) {
    return expf(-4.6f / (fmaxf(ms, 1.f) * 0.001f * dsp::kSampleRate));
  }

  /* Lay out one note's partials: frequency, level and decay rate for each. */
  void strike(Voice &v) {
    const float f0 = dsp::noteToHz(v.note);
    /* Short strings are stiffer, so the stretch grows as you play up. */
    const float stretch = p_.inharmonic * dsp::pow2f_fast((v.note - 48) * (1.f / 24.f));
    /* Softer playing rolls the top off, hard playing opens it up. */
    const float rolloff = p_.rolloff + (1.f - v.velocity) * p_.velBright;
    /* Higher notes decay faster, as shorter strings do. */
    const float decayMs = p_.decayMs * dsp::pow2f_fast((60 - v.note) * (0.6f / 12.f));

    v.numPartials = 0;
    for (int k = 0; k < kNumPartials; ++k) {
      const float n = static_cast<float>(k + 1);
      /*
       * Normalised so partial 1 lands exactly on f0 and the stretch applies to
       * the partials above it. Without the division the fundamental itself goes
       * sharp — 20 cents on the toy piano — and the instrument is out of tune
       * with everything else on the synth.
       */
      const float hz = f0 * n * sqrtf((1.f + stretch * n * n) / (1.f + stretch));
      if (hz > 0.45f * dsp::kSampleRate) break;

      v.phase[k] = 0.f;
      v.inc[k] = hz * dsp::kSampleRateRecip;
      v.amp[k] = 1.f / powf(n, rolloff);
      v.coef[k] = decayCoefFor(decayMs / (1.f + static_cast<float>(k) * p_.decaySpread));
      ++v.numPartials;
    }

    /* Normalise so a dense partial set is not louder than a sparse one. */
    float sum = 0.f;
    for (int k = 0; k < v.numPartials; ++k) sum += v.amp[k];
    const float norm = 1.f / fmaxf(sum, 0.3f);
    for (int k = 0; k < v.numPartials; ++k) v.amp[k] *= norm;

    v.beatLfo.setRate(p_.beatRateHz);
  }

  void loadPreset(Preset p) {
    //               inharm  roll  velBr  decMs  spread beat  rate  hammer relMs gain
    const Params kPresets[kNumPresets] = {
        /* GRND */ {0.00045f, 1.25f, 0.90f, 3500.f, 0.55f, 0.10f, 1.2f, 0.22f, 220.f, 1.00f},
        /* BRIT */ {0.00070f, 0.45f, 1.20f, 3000.f, 0.12f, 0.08f, 1.5f, 0.38f, 190.f, 0.75f},
        /* MELO */ {0.00025f, 2.80f, 0.60f, 5200.f, 1.90f, 0.05f, 0.9f, 0.06f, 320.f, 1.35f},
        /* HONK */ {0.00190f, 1.00f, 0.80f, 1700.f, 0.45f, 0.48f, 4.8f, 0.45f, 160.f, 0.95f},
        /* TOY  */ {0.02400f, 0.50f, 0.55f, 550.f, 1.20f, 0.18f, 3.1f, 0.55f, 100.f, 0.85f},
    };

    preset_ = p;
    p_ = kPresets[p];
    noiseCoef_ = decayCoefFor(p == kToy ? 18.f : 32.f);

    for (int i = 0; i < kMaxVoices; ++i) {
      dsp::ADSR &e = pool_[i].env;
      e.setAttack(1.5f + attackAddMs_);
      e.setDecay(200.f);
      /*
       * The partials carry the decay themselves, so the master envelope only
       * opens, holds, and closes on key release.
       */
      e.setSustain(1.f);
      e.setRelease(p_.releaseMs + releaseAddMs_);
    }
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;
  dsp::SineTable sine_;
  dsp::Noise noise_;
  dsp::LoFi lofi_;

  Preset preset_ = kGrand;
  Params p_ = {0.00045f, 1.30f, 0.9f, 3500.f, 0.55f, 0.10f, 1.2f, 0.22f, 220.f, 1.f};

  float tone_ = 0.5f;
  float velSens_ = 0.6f;
  float lofiAmount_ = 0.25f;
  float attackAddMs_ = 0.f;
  float releaseAddMs_ = 0.f;
  float bendSemis_ = 0.f;
  float toneMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
  float noiseCoef_ = 0.f;
};
