/*
 *  organ/dsp.h — six drawbar organ presets, 8 voices.
 *
 *  Additive, on the tonewheel plan: nine drawbars at 16', 5 1/3', 8', 4',
 *  2 2/3', 2', 1 3/5', 1 1/3' and 1'.
 *
 *  The trick that makes this cheap is running the phasor at *half* the note's
 *  frequency. Against that sub-octave every drawbar is an integer multiple —
 *  1, 2, 3, 4, 6, 8, 10, 12, 16 — so all nine come from one accumulator by
 *  multiplying its phase. Run the phasor at the note frequency instead and the
 *  16' and 5 1/3' drawbars need ratios of 0.5 and 1.5, which click audibly
 *  every time the phase wraps.
 *
 *  Voices skip drawbars set to zero, so a three-drawbar jazz registration costs
 *  a third of a full one.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in osc.h / unit.cc.
 */
#pragma once

#include "adsr.h"
#include "dsp_util.h"
#include "lfo.h"
#include "noise.h"
#include "sine.h"
#include "voice_pool.h"

class OrganEngine {
 public:
  static constexpr int kMaxVoices = 8;
  static constexpr int kNumBars = 9;

  enum ParamId {
    P_ATTACK = 0,  // A knob: added on top of the preset's attack
    P_RELEASE,     // B knob: likewise for release
    P_PRESET,      // EDIT menu, where the display renders preset names
    P_TONE,
    P_VIBRATO,
    P_PERC,
    P_DRIVE,
    P_VOICES,
    P_COUNT
  };

  enum Preset { kJazz = 0, kRock, kVox, kFarfisa, kPipe, kTheatre, kNumPresets };

  void init() {
    sine_.init();
    pool_.init();
    noise_.seed(0x3B9F17DU);
    vibLfo_.init(0.f);
    tremLfo_.init(0.25f);

    tone_ = 0.5f;
    vibAmount_ = 1.f;
    percAmount_ = 1.f;
    driveAmount_ = 0.12f;
    attackAddMs_ = 0.f;
    releaseAddMs_ = 0.f;
    bendSemis_ = 0.f;
    toneMod_ = 0.f;
    runtimeNote_ = 60;
    loadPreset(kJazz);
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
      case P_VIBRATO:
        vibAmount_ = value * 0.01f;
        break;
      case P_PERC:
        percAmount_ = value * 0.01f;
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

    /*
     * Hammond percussion is single-trigger: it fires only when the whole
     * keyboard was up. Play legato and the second note has no ping, which is
     * half of why organ players phrase the way they do.
     */
    const bool keyboardWasClear = (pool_.active() == 0);

    const Pool::Assignment a = pool_.noteOn(note);
    Voice &v = *a.voice;
    v.velocity = velo * (1.f / 127.f);
    v.start(a.retrigger);
    v.percEnv = keyboardWasClear ? 1.f : 0.f;
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

    /* Drawbar ratios against a phasor running an octave below the note. */
    const float kRatio[kNumBars] = {1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 10.f, 12.f, 16.f};

    /*
     * TONE pulls the upper drawbars in and out — the one gesture an organist
     * makes constantly. Below halfway it also thins the lower ones out.
     */
    const float tone = dsp::clampf(0.f, tone_ + toneMod_, 1.f);
    float level[kNumBars];
    float sum = 0.f;
    for (int k = 0; k < kNumBars; ++k) {
      const float pull = dsp::clampf(0.f, tone * 12.f + 1.5f - static_cast<float>(k), 1.f);
      level[k] = p_.bars[k] * pull;
      sum += level[k];
    }
    const float norm = 1.f / fmaxf(sum, 0.5f);
    for (int k = 0; k < kNumBars; ++k) level[k] *= norm;

    const float vibDepth = p_.vibDepth * vibAmount_;
    const float percLevel = p_.perc * percAmount_;
    const float dirt = p_.edge + driveAmount_ * 1.2f;
    const float drive = 1.f + dirt * 2.5f;
    const float makeup = 1.f / (1.f + dirt * 1.3f);
    const float clickScale = 1.f / drive;

    for (int i = 0; i < pool_.count(); ++i) {
      Voice &v = pool_[i];
      if (v.env.isIdle()) continue;

      /* Half the note frequency: the 16' drawbar is the phasor itself. */
      const float subHz = dsp::noteToHz(v.note + bendSemis_) * 0.5f;
      const float gain = 0.34f * p_.outGain;

      for (uint32_t n = 0; n < frames; ++n) {
        const float e = v.env.next();
        v.percEnv *= percCoef_;
        v.clickEnv *= clickCoef_;

        const float semis = vibDepth * vibLfo_.next();
        v.phase += subHz * dsp::pow2f_fast(semis * (1.f / 12.f)) * dsp::kSampleRateRecip;
        if (v.phase >= 1.f) v.phase -= 1.f;

        const float ph = v.phase + dsp::SineTable::kPhaseBias;

        float s = 0.f;
        for (int k = 0; k < kNumBars; ++k)
          if (level[k] > 1e-4f) s += level[k] * sine_.at(ph * kRatio[k]);

        /* Percussion sits on the 2nd or 3rd harmonic and decays fast. */
        if (percLevel > 0.f) s += percLevel * v.percEnv * sine_.at(ph * p_.percRatio);

        /*
         * Key click: the contacts, not the tone. Scaled against the preamp
         * gain, because it is a mechanical noise ahead of the amp — left
         * unscaled, a driven preset turns the click into the loudest thing in
         * the instrument.
         */
        s += noise_.next() * v.clickEnv * v.clickEnv * p_.click * clickScale;

        out[n] += s * e * gain;
      }
    }

    /*
     * Tremulant and tube edge, both shared: a theatre organ swings the whole
     * instrument, and the amp is downstream of every drawbar.
     */
    const float trem = p_.tremDepth * vibAmount_;
    for (uint32_t n = 0; n < frames; ++n) {
      const float wobble = 1.f - trem * (0.5f - 0.5f * tremLfo_.next());
      out[n] = dsp::softclipf(out[n] * wobble * drive) * makeup;
    }
  }

  int activeVoices() const { return pool_.active(); }

 private:
  struct Params {
    float bars[kNumBars];  // 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'
    float perc;            // percussion level
    float percRatio;       // against the sub phasor: 4 = 2nd harmonic, 6 = 3rd
    float percDecayMs;
    float click;
    float vibDepth;        // semitones
    float tremDepth;       // amplitude wobble: a theatre organ's tremulant
    float vibRateHz;
    float attackMs;
    float releaseMs;
    float edge;            // waveshaping: the reedy buzz of a divider organ
    float outGain;
  };

  struct Voice {
    dsp::ADSR env;
    float phase = 0.f;
    float percEnv = 0.f;
    float clickEnv = 0.f;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      /* Tonewheels never start in phase with each other. */
      phase = index * 0.061f;
      percEnv = 0.f;
      clickEnv = 0.f;
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      (void)retrigger;  // a tonewheel keeps spinning; never reset the phase
      clickEnv = 1.f;
      env.gateOn(true);
    }

    void kill() {
      env.kill();
      percEnv = 0.f;
      clickEnv = 0.f;
      held = false;
    }
  };

  static float decayCoefFor(float ms) {
    return expf(-4.6f / (fmaxf(ms, 1.f) * 0.001f * dsp::kSampleRate));
  }

  void loadPreset(Preset p) {
    //            16'   5 1/3'  8'    4'   2 2/3'  2'  1 3/5' 1 1/3'  1'   perc pRat pMs  click  vib  trem rate atk  rel  edge gain
    const Params kPresets[kNumPresets] = {
        /* JAZZ */ {{0.85f, 0.70f, 0.80f, 0.10f, 0.05f, 0.05f, 0.f, 0.f, 0.f},
                    0.55f, 6.f, 220.f, 0.10f, 0.06f, 0.00f, 6.6f, 4.f, 12.f, 0.05f, 1.05f},
        /* ROCK */ {{1.00f, 0.80f, 1.00f, 0.70f, 0.30f, 0.35f, 0.15f, 0.12f, 0.20f},
                    0.f, 4.f, 200.f, 0.16f, 0.05f, 0.00f, 6.9f, 3.f, 10.f, 0.50f, 0.85f},
        /* VOX  */ {{0.10f, 0.04f, 1.00f, 0.70f, 0.12f, 0.62f, 0.08f, 0.08f, 0.50f},
                    0.f, 4.f, 150.f, 0.06f, 0.14f, 0.00f, 5.4f, 6.f, 16.f, 0.70f, 0.95f},
        /* FARF */ {{0.02f, 0.02f, 0.18f, 0.55f, 0.60f, 1.00f, 0.95f, 0.95f, 1.00f},
                    0.f, 4.f, 150.f, 0.04f, 0.22f, 0.00f, 7.2f, 5.f, 14.f, 2.60f, 0.58f},
        /* PIPE */ {{0.08f, 0.02f, 1.00f, 0.72f, 0.48f, 0.55f, 0.22f, 0.28f, 0.45f},
                    0.f, 4.f, 150.f, 0.00f, 0.00f, 0.00f, 4.0f, 45.f, 90.f, 0.00f, 1.00f},
        /* THTR */ {{1.00f, 0.06f, 1.00f, 0.08f, 0.f, 0.f, 0.f, 0.f, 0.f},
                    0.f, 4.f, 150.f, 0.02f, 0.30f, 0.55f, 5.9f, 18.f, 40.f, 0.05f, 1.15f},
    };

    preset_ = p;
    p_ = kPresets[p];
    percCoef_ = decayCoefFor(p_.percDecayMs);
    clickCoef_ = decayCoefFor(6.f);
    vibLfo_.setRate(p_.vibRateHz);
    tremLfo_.setRate(p_.vibRateHz * 0.82f);  // the tremulant runs a touch slower

    for (int i = 0; i < kMaxVoices; ++i) {
      dsp::ADSR &e = pool_[i].env;
      e.setAttack(p_.attackMs + attackAddMs_);
      e.setDecay(100.f);
      e.setSustain(1.f);  // an organ holds for as long as the key is down
      e.setRelease(p_.releaseMs + releaseAddMs_);
    }
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;
  dsp::SineTable sine_;
  dsp::SineLfo vibLfo_, tremLfo_;
  dsp::Noise noise_;

  Preset preset_ = kJazz;
  Params p_ = {{0.85f, 0.70f, 0.80f, 0.10f, 0.05f, 0.05f, 0.f, 0.f, 0.f},
               0.55f, 6.f, 220.f, 0.10f, 0.06f, 0.00f, 6.6f, 4.f, 12.f, 0.05f, 1.05f};

  float tone_ = 0.5f;
  float vibAmount_ = 1.f;
  float percAmount_ = 1.f;
  float driveAmount_ = 0.12f;
  float attackAddMs_ = 0.f;
  float releaseAddMs_ = 0.f;
  float bendSemis_ = 0.f;
  float toneMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
  float percCoef_ = 0.f;
  float clickCoef_ = 0.f;
};
