/*
 *  epiano/dsp.h — four electric piano presets, 8 voices.
 *
 *  2-op FM, which is the right tool here for the reason it is the wrong one for
 *  an acoustic piano (see units/osc/piano): a tine or a reed really does produce a
 *  near-harmonic spectrum whose brightness collapses in the first fraction of a
 *  second. The modulation index has its own fast envelope and scales with
 *  velocity — that is the bark when you dig in, and it is most of the feel.
 *
 *    TINE-type  modulator at 1x for a full body, plus a short 12x ping on the
 *               attack: the metallic strike of a tine against a pickup.
 *    REED-type  modulator at 2x, which puts the sidebands on odd harmonics —
 *               the hollow, square-ish Wurlitzer reed.
 *
 *  Tremolo and preamp drive are global, because on the real instruments they
 *  are: one amp, after all the notes.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in osc.h / unit.cc.
 */
#pragma once

#include "adsr.h"
#include "dsp_util.h"
#include "lfo.h"
#include "sine.h"
#include "voice_pool.h"

class EPianoEngine {
 public:
  static constexpr int kMaxVoices = 8;

  enum ParamId {
    P_ATTACK = 0,  // A knob: added on top of the preset's attack
    P_RELEASE,     // B knob: likewise for release
    P_PRESET,      // EDIT menu, where the display renders preset names
    P_TONE,
    P_TREMOLO,
    P_DRIVE,
    P_VOICES,
    P_COUNT
  };

  enum Preset { kRhodes = 0, kWurli, kDyno, kBark, kNumPresets };

  void init() {
    sine_.init();
    pool_.init();
    tremLfo_.init(0.f);

    tone_ = 0.42f;
    tremAmount_ = 1.f;
    driveAmount_ = 0.2f;
    attackAddMs_ = 0.f;
    releaseAddMs_ = 0.f;
    bendSemis_ = 0.f;
    toneMod_ = 0.f;
    runtimeNote_ = 60;
    loadPreset(kRhodes);
  }

  void reset() { pool_.killAll(); }

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
      case P_TREMOLO:
        tremAmount_ = value * 0.01f;
        break;
      case P_DRIVE:
        driveAmount_ = value * 0.01f;
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

  void setToneMod(float mod) { toneMod_ = mod; }

  // ---- audio --------------------------------------------------------------

  void process(float *__restrict out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = 0.f;

    const float tone = dsp::clampf(0.f, tone_ + toneMod_, 1.f);
    const float pingIndex = p_.ping * (0.4f + 1.4f * tone);

    for (int i = 0; i < pool_.count(); ++i) {
      Voice &v = pool_[i];
      if (v.env.isIdle()) continue;

      const float hz = dsp::noteToHz(v.note + bendSemis_);
      v.inc = dsp::clampf(0.f, hz * dsp::kSampleRateRecip, 0.45f);

      /* Higher notes decay faster, as shorter tines and reeds do. */
      v.env.setDecay(dsp::clampf(120.f, p_.decayMs * dsp::pow2f_fast((60 - v.note) * (0.4f / 12.f)),
                                 12000.f));

      /* Softer keys are darker as well as quieter — that is most of the feel. */
      const float velAmp = dsp::lerpf(1.f, v.velocity, p_.velSens);
      const float velTone = dsp::lerpf(1.f, v.velocity, p_.velSens * 0.85f);
      const float index = (p_.indexBase + p_.indexRange * tone) * velTone;
      const float gain = velAmp * 0.33f * p_.outGain;

      for (uint32_t n = 0; n < frames; ++n) {
        const float e = v.env.next();
        v.modEnv *= modCoef_;
        v.pingEnv *= pingCoef_;

        const float ph = v.phase + dsp::SineTable::kPhaseBias;
        const float mod = sine_.at(ph * p_.modRatio) * index * v.modEnv;
        const float ping = sine_.at(ph * 12.f) * pingIndex * v.pingEnv;
        const float s = sine_.at(ph + mod + ping);

        out[n] += s * e * gain;

        v.phase += v.inc;
        if (v.phase >= 1.f) v.phase -= 1.f;
      }
    }

    /* Tremolo and preamp, both global — they act on the instrument, not a note. */
    const float depth = p_.tremDepth * tremAmount_;
    const float drive = p_.drive + driveAmount_;
    const float preGain = 1.f + drive * 4.f;
    const float postGain = 1.f / (1.f + drive * 1.6f);

    for (uint32_t n = 0; n < frames; ++n) {
      const float trem = 1.f - depth * (0.5f - 0.5f * tremLfo_.next());
      out[n] = dsp::softclipf(out[n] * trem * preGain) * postGain;
    }
  }

  int activeVoices() const { return pool_.active(); }

 private:
  struct Params {
    float modRatio;     // 1 = tine-like body, 2 = hollow reed
    float indexBase;    // FM index floor
    float indexRange;   // how far TONE pushes it
    float modDecayMs;   // how fast the strike gives way to the body
    float ping;         // 12x attack ping: the tine strike
    float pingDecayMs;
    float decayMs;      // note decay at middle C
    float releaseMs;
    float tremDepth;
    float tremRateHz;
    float velSens;
    float drive;        // baked-in preamp, on top of the DRIV control
    float outGain;
  };

  struct Voice {
    dsp::ADSR env;
    float phase = 0.f;
    float inc = 0.f;
    float modEnv = 0.f;
    float pingEnv = 0.f;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      phase = index * 0.037f;
      inc = 0.f;
      modEnv = 0.f;
      pingEnv = 0.f;
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      if (retrigger) phase = 0.f;  // consistent attack transient
      modEnv = 1.f;
      pingEnv = 1.f;
      env.gateOn(retrigger);
    }

    void kill() {
      env.kill();
      modEnv = 0.f;
      pingEnv = 0.f;
      held = false;
    }
  };

  static float decayCoefFor(float ms) {
    return expf(-4.6f / (fmaxf(ms, 1.f) * 0.001f * dsp::kSampleRate));
  }

  void loadPreset(Preset p) {
    //             ratio idxB  idxR  modMs  ping pingMs  decMs  relMs  trem rate  vel  drv  gain
    const Params kPresets[kNumPresets] = {
        /* RHDS */ {1.f, 0.55f, 3.4f, 300.f, 0.70f, 70.f, 3600.f, 260.f, 0.26f, 5.0f, 0.70f, 0.20f, 1.00f},
        /* WURL */ {2.f, 0.90f, 5.5f, 130.f, 0.35f, 55.f, 2400.f, 200.f, 0.42f, 6.0f, 0.75f, 0.35f, 0.95f},
        /* DYNO */ {1.f, 0.95f, 4.6f, 420.f, 1.00f, 90.f, 3000.f, 220.f, 0.14f, 4.6f, 0.85f, 0.45f, 0.90f},
        /* BARK */ {2.f, 1.60f, 6.5f, 170.f, 0.55f, 45.f, 1900.f, 170.f, 0.30f, 6.6f, 1.00f, 0.60f, 0.85f},
    };

    preset_ = p;
    p_ = kPresets[p];
    modCoef_ = decayCoefFor(p_.modDecayMs);
    pingCoef_ = decayCoefFor(p_.pingDecayMs);
    tremLfo_.setRate(p_.tremRateHz);

    for (int i = 0; i < kMaxVoices; ++i) {
      dsp::ADSR &e = pool_[i].env;
      e.setAttack((p_.modRatio > 1.5f ? 2.f : 3.f) + attackAddMs_);
      e.setDecay(p_.decayMs);
      e.setSustain(0.f);  // electric pianos decay away under a held key
      e.setRelease(p_.releaseMs + releaseAddMs_);
    }
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;
  dsp::SineTable sine_;
  dsp::SineLfo tremLfo_;

  Preset preset_ = kRhodes;
  Params p_ = {1.f, 0.55f, 3.4f, 300.f, 0.70f, 70.f, 3600.f, 260.f, 0.26f, 5.0f, 0.70f, 0.20f, 1.00f};

  float tone_ = 0.42f;
  float tremAmount_ = 1.f;
  float driveAmount_ = 0.2f;
  float attackAddMs_ = 0.f;
  float releaseAddMs_ = 0.f;
  float bendSemis_ = 0.f;
  float toneMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
  float modCoef_ = 0.f;
  float pingCoef_ = 0.f;
};
