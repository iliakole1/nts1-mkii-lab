/*
 *  flute/dsp.h — five breath-blown presets, 6 voices.
 *
 *  A flute is close to a sine with a little odd-harmonic colour and a lot of
 *  air. What sells it is not the waveform: it is the chiff at the start of a
 *  note, the steady breath underneath, and vibrato that arrives *after* the
 *  attack rather than with it — players do not vibrate a note until it lands.
 *
 *  Presets set the harmonic mix, how much air, and how quick and deep the
 *  vibrato is. An ocarina is nearly pure with a fat second; a pan flute is
 *  hollow and breathy; a recorder leans on odd harmonics.
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
#include "svf.h"
#include "voice_pool.h"

class FluteEngine {
 public:
  static constexpr int kMaxVoices = 6;

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

  enum Preset { kFlute = 0, kOcarina, kPanFlute, kRecorder, kPiccolo, kNumPresets };

  void init() {
    sine_.init();
    pool_.init();
    noise_.seed(0x2F19B7DU);
    vibLfo_.init(0.f);
    lofi_.reset();

    tone_ = 0.5f;
    velSens_ = 0.5f;
    attackAddMs_ = 0.f;
    releaseAddMs_ = 0.f;
    bendSemis_ = 0.f;
    toneMod_ = 0.f;
    runtimeNote_ = 60;
    lofiAmount_ = 0.3f;
    lofi_.setAmount(lofiAmount_);
    loadPreset(kFlute);
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

  void setToneMod(float mod) { toneMod_ = mod; }

  // ---- audio --------------------------------------------------------------

  void process(float *__restrict out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = 0.f;

    /* TONE trades body for air: down is pure and round, up is bright and windy. */
    const float tone = dsp::clampf(0.f, tone_ + toneMod_, 1.f);
    const float harmScale = 0.4f + 1.6f * tone;
    const float airScale = 0.5f + 1.2f * tone;

    for (int i = 0; i < pool_.count(); ++i) {
      Voice &v = pool_[i];
      if (v.env.isIdle()) continue;

      const float baseHz = dsp::noteToHz(v.note + bendSemis_ + p_.octave);
      v.breath.setCutoff(dsp::clampf(300.f, baseHz * p_.breathRatio, 9000.f), p_.breathQ);

      /* Blowing harder is louder *and* brighter, and adds noise. */
      const float velAmp = dsp::lerpf(1.f, v.velocity, velSens_);
      const float velTone = dsp::lerpf(1.f, v.velocity, velSens_ * 0.7f);
      const float h2 = p_.second * harmScale * velTone;
      const float h3 = p_.third * harmScale * velTone;
      const float h5 = p_.fifth * harmScale * velTone;
      const float air = p_.breath * airScale;
      const float gain = velAmp * 0.42f * p_.outGain;

      for (uint32_t n = 0; n < frames; ++n) {
        const float e = v.env.next();
        v.chiffEnv *= chiffCoef_;

        /* Vibrato fades in after the note lands. */
        v.vibRamp += (1.f - v.vibRamp) * vibRampCoef_;
        const float semis = p_.vibDepth * v.vibRamp * vibLfo_.next();
        const float inc =
            baseHz * dsp::pow2f_fast(semis * (1.f / 12.f)) * dsp::kSampleRateRecip;

        const float ph = v.phase + dsp::SineTable::kPhaseBias;
        float s = sine_.at(ph) + h2 * sine_.at(ph * 2.f) + h3 * sine_.at(ph * 3.f) +
                  h5 * sine_.at(ph * 5.f);

        const float wind = v.breath.bandpass(noise_.next());
        s += wind * (air + v.chiffEnv * p_.chiff);

        out[n] += s * e * gain;

        v.phase += inc;
        if (v.phase >= 1.f) v.phase -= 1.f;
      }
    }

    for (uint32_t n = 0; n < frames; ++n) out[n] = lofi_.process(dsp::softclipf(out[n]));
  }

  int activeVoices() const { return pool_.active(); }

 private:
  struct Params {
    float second;       // 2nd harmonic level
    float third;        // 3rd harmonic level
    float fifth;        // 5th harmonic — the reedy, hollow one
    float octave;       // transposition in semitones: a piccolo is a flute, up
    float breath;       // steady air
    float breathRatio;  // where the air sits, relative to the note
    float breathQ;
    float chiff;        // attack noise burst
    float attackMs;
    float releaseMs;
    float vibDepth;     // semitones
    float vibRateHz;
    float vibOnsetMs;
    float outGain;
  };

  struct Voice {
    dsp::ADSR env;
    dsp::SVF breath;
    float phase = 0.f;
    float chiffEnv = 0.f;
    float vibRamp = 0.f;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      breath.reset();
      phase = index * 0.043f;
      chiffEnv = 0.f;
      vibRamp = 0.f;
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      if (retrigger) {
        phase = 0.f;
        breath.reset();
      }
      chiffEnv = 1.f;
      vibRamp = 0.f;  // every new note re-earns its vibrato
      env.gateOn(retrigger);
    }

    void kill() {
      env.kill();
      chiffEnv = 0.f;
      held = false;
    }
  };

  static float decayCoefFor(float ms) {
    return expf(-4.6f / (fmaxf(ms, 1.f) * 0.001f * dsp::kSampleRate));
  }

  void loadPreset(Preset p) {
    //              2nd    3rd    5th   oct    air   ratio   Q   chiff atkMs relMs  vib   rate onset gain
    const Params kPresets[kNumPresets] = {
        /* FLUT */ {0.16f, 0.20f, 0.08f, 0.f, 0.22f, 3.5f, 0.60f, 0.55f, 40.f, 140.f, 0.25f, 5.2f, 450.f, 0.92f},
        /* OCAR */ {0.20f, 0.00f, 0.00f, 0.f, 0.015f, 2.0f, 0.80f, 0.14f, 85.f, 190.f, 0.11f, 4.1f, 700.f, 1.20f},
        /* PANF */ {0.04f, 0.30f, 0.14f, 0.f, 0.65f, 5.0f, 0.35f, 1.40f, 22.f, 110.f, 0.20f, 5.8f, 380.f, 0.80f},
        /* RECD */ {0.05f, 0.44f, 0.34f, 0.f, 0.18f, 6.0f, 0.50f, 0.80f, 12.f, 85.f, 0.05f, 5.0f, 900.f, 0.80f},
        /* PICC */ {0.18f, 0.12f, 0.05f, 12.f, 0.24f, 4.0f, 0.50f, 1.00f, 14.f, 80.f, 0.38f, 6.8f, 300.f, 0.80f},
    };

    preset_ = p;
    p_ = kPresets[p];
    chiffCoef_ = decayCoefFor(70.f);
    vibRampCoef_ = 1.f - expf(-4.6f / (p_.vibOnsetMs * 0.001f * dsp::kSampleRate));
    vibLfo_.setRate(p_.vibRateHz);

    for (int i = 0; i < kMaxVoices; ++i) {
      dsp::ADSR &e = pool_[i].env;
      e.setAttack(p_.attackMs + attackAddMs_);
      e.setDecay(150.f);
      e.setSustain(1.f);  // wind instruments hold as long as you blow
      e.setRelease(p_.releaseMs + releaseAddMs_);
    }
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;
  dsp::SineTable sine_;
  dsp::SineLfo vibLfo_;
  dsp::Noise noise_;
  dsp::LoFi lofi_;

  Preset preset_ = kFlute;
  Params p_ = {0.12f, 0.07f, 0.02f, 0.f, 0.10f, 3.5f, 0.60f, 0.50f,
               40.f,  140.f, 0.25f, 5.2f, 450.f, 1.00f};

  float tone_ = 0.5f;
  float velSens_ = 0.5f;
  float lofiAmount_ = 0.3f;
  float attackAddMs_ = 0.f;
  float releaseAddMs_ = 0.f;
  float bendSemis_ = 0.f;
  float toneMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
  float chiffCoef_ = 0.f;
  float vibRampCoef_ = 0.f;
};
