/*
 *  pluck/dsp.h — six plucked-string presets, 6 voices, Karplus-Strong.
 *
 *  Each note fills a delay line with a burst of filtered noise, then circulates
 *  it through a one-pole lowpass. The filter is the string: how much high end
 *  it keeps sets both the tone and how long the note rings, because the loop
 *  loses a little more energy on every pass.
 *
 *  Three things separate a banjo from a guitar here:
 *
 *    pick brightness  how much high end is in the burst — a fingernail on steel
 *                     against a thumb on nylon
 *    pick position    the burst has a delayed copy of itself subtracted, which
 *                     notches out the harmonic whose wavelength matches. Picking
 *                     near the bridge is thin and nasal; over the hole is round.
 *    body             two resonant bandpasses across the voice mix. Instrument
 *                     size lives here: a ukulele body resonates near 400 Hz, a
 *                     guitar near 100.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in osc.h / unit.cc.
 */
#pragma once

#include "adsr.h"
#include "dsp_util.h"
#include "lofi.h"
#include "noise.h"
#include "svf.h"
#include "voice_pool.h"

class PluckEngine {
 public:
  static constexpr int kMaxVoices = 6;

  /* Power of two so the read index can be masked. 1024 samples reaches down to
   * ~47 Hz — below a guitar's low E, and below every other preset here. */
  static constexpr int kKsSize = 1024;
  static constexpr int kKsMask = kKsSize - 1;

  /* Floats of delay-line storage the caller must hand to init(). */
  static constexpr uint32_t kBufferSize = kMaxVoices * kKsSize;

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

  enum Preset { kBanjo = 0, kGuitar, kUkulele, kMandolin, kHarp, kKoto, kNumPresets };

  /*
   * The delay lines are supplied by the caller rather than held as a member.
   * PluckEngine cannot be zero-initialised — dsp::LoFi and dsp::Noise both
   * carry non-zero defaults — so everything inside the object is emitted as
   * initialised .data. With 24 KB of lines in there the unit shipped 24 KB of
   * zeros inside the ELF, over half its ~48 KB budget. Held outside as a
   * zero-initialised static it lands in .bss and costs nothing in the file.
   */
  void init(float *buffer) {
    lines_ = buffer;
    pool_.init();
    noise_.seed(0x7C1D93BU);
    body1_.reset();
    body2_.reset();
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
    loadPreset(kBanjo);
    clearLines();
  }

  void reset() {
    pool_.killAll();
    body1_.reset();
    body2_.reset();
    lofi_.reset();
    clearLines();
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
    Voice &v = *a.voice;
    v.velocity = velo * (1.f / 127.f);
    v.start(a.retrigger);
    pluck(v);
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
    if (!lines_) return;

    /* TONE opens or closes the loop filter: dull and short, or bright and long. */
    const float tone = dsp::clampf(0.f, tone_ + toneMod_, 1.f);
    const float loopLp = dsp::clampf(0.10f, p_.loopBright * (0.6f + 0.8f * tone), 0.965f);
    const float loopDelay = (1.f - loopLp) / loopLp;

    for (int i = 0; i < pool_.count(); ++i) {
      Voice &v = pool_[i];
      if (v.env.isIdle()) continue;

      float *line = lines(v.index);
      const float hz = dsp::noteToHz(v.note + bendSemis_);
      /* The loop filter has a delay of its own; subtract it or strings ring flat. */
      v.ksDelay = dsp::clampf(4.f, dsp::kSampleRate / fmaxf(hz, 20.f) - loopDelay,
                              static_cast<float>(kKsSize - 2));

      /*
       * The amp envelope shapes what you hear and frees the voice; the string
       * itself is set to ring longer, so it stays alive and bright underneath.
       */
      const float ringSamples = 2.f * p_.decayMs * 0.001f * dsp::kSampleRate;
      const float damp = dsp::clampf(0.9f, expf(-v.ksDelay * 4.6f / ringSamples), 0.99995f);
      const float gain = dsp::lerpf(1.f, v.velocity, velSens_) * 0.5f * p_.outGain;

      for (uint32_t n = 0; n < frames; ++n) {
        const float e = v.env.next();

        float pos = static_cast<float>(v.ksWrite) - v.ksDelay;
        if (pos < 0.f) pos += static_cast<float>(kKsSize);
        const int i0 = static_cast<int>(pos);
        const float frac = pos - static_cast<float>(i0);
        const float sample = dsp::lerpf(line[i0 & kKsMask], line[(i0 + 1) & kKsMask], frac);

        v.ksLp += (sample - v.ksLp) * loopLp;
        line[v.ksWrite] = v.ksLp * damp;
        v.ksWrite = (v.ksWrite + 1) & kKsMask;

        out[n] += sample * e * gain;
      }
    }

    /* Body resonance, on the mix: the instrument, not the string. */
    if (p_.bodyMix > 0.f) {
      for (uint32_t n = 0; n < frames; ++n) {
        const float s = out[n];
        out[n] = s * (1.f - 0.35f * p_.bodyMix) +
                 p_.bodyMix * (body1_.bandpass(s) + 0.55f * body2_.bandpass(s));
      }
    }

    for (uint32_t n = 0; n < frames; ++n) out[n] = lofi_.process(dsp::softclipf(out[n]));
  }

  int activeVoices() const { return pool_.active(); }

 private:
  struct Params {
    float pickBright;   // one-pole coefficient for the excitation burst
    float pickPos;      // fraction of the string: sets the notched-out harmonic
    float loopBright;   // base loop filter opening, scaled by TONE
    float decayMs;      // ring time
    float releaseMs;    // damping when the key is released
    float bodyHz1;
    float bodyHz2;
    float bodyQ;
    float bodyMix;
    float outGain;
  };

  struct Voice {
    dsp::ADSR env;
    int index = 0;  // which delay line belongs to this voice
    int ksWrite = 0;
    float ksDelay = 100.f;
    float ksLp = 0.f;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int i) {
      env.init();
      index = i;
      ksWrite = 0;
      ksDelay = 100.f;
      ksLp = 0.f;
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      (void)retrigger;
      ksLp = 0.f;
      env.gateOn(true);  // every pluck is a fresh strike
    }

    void kill() {
      env.kill();
      ksLp = 0.f;
      held = false;
    }
  };

  void loadPreset(Preset p) {
    //              pickBr pickPos loopBr  decMs   relMs  body1  body2   Q     mix   gain
    const Params kPresets[kNumPresets] = {
        /* BANJ */ {0.92f, 0.14f, 0.96f, 1100.f, 90.f, 240.f, 520.f, 0.78f, 0.70f, 0.95f},
        /* GTAR */ {0.28f, 0.26f, 0.50f, 2800.f, 190.f, 95.f, 185.f, 0.55f, 0.85f, 1.15f},
        /* UKUL */ {0.42f, 0.21f, 0.68f, 1500.f, 130.f, 450.f, 830.f, 0.74f, 0.85f, 1.05f},
        /* MAND */ {1.00f, 0.04f, 0.86f, 420.f, 60.f, 640.f, 1150.f, 0.70f, 0.75f, 1.00f},
        /* HARP */ {0.58f, 0.33f, 0.80f, 7000.f, 600.f, 150.f, 320.f, 0.42f, 0.30f, 1.15f},
        /* KOTO */ {0.78f, 0.03f, 0.88f, 2600.f, 150.f, 175.f, 700.f, 0.80f, 0.80f, 0.95f},
    };

    preset_ = p;
    p_ = kPresets[p];

    body1_.setCutoff(p_.bodyHz1, p_.bodyQ);
    body2_.setCutoff(p_.bodyHz2, p_.bodyQ * 0.8f);

    for (int i = 0; i < kMaxVoices; ++i) {
      dsp::ADSR &e = pool_[i].env;
      e.setAttack(0.6f + attackAddMs_);
      e.setDecay(p_.decayMs);
      e.setSustain(0.f);  // a plucked string decays under a held key
      e.setRelease(p_.releaseMs + releaseAddMs_);
    }
  }

  /* Fill the line with a burst of filtered noise: the pluck. */
  void pluck(Voice &v) {
    float *line = lines(v.index);
    const float hz = dsp::noteToHz(v.note);
    v.ksDelay = dsp::clampf(4.f, dsp::kSampleRate / fmaxf(hz, 20.f),
                            static_cast<float>(kKsSize - 2));
    v.ksLp = 0.f;

    const int len = static_cast<int>(v.ksDelay) + 1;
    /* Harder playing means a brighter pick, on any instrument. */
    const float coef = dsp::clampf(0.1f, p_.pickBright * (0.6f + 0.6f * v.velocity), 0.98f);

    float lp = 0.f;
    for (int i = 0; i < len; ++i) {
      lp += (noise_.next() - lp) * coef;
      line[i] = lp;
    }

    /* Pick position: subtracting a delayed copy notches the matching harmonic. */
    const int d = static_cast<int>(p_.pickPos * len);
    if (d > 0)
      for (int i = len - 1; i >= d; --i) line[i] -= line[i - d] * 0.7f;

    for (int i = len; i < kKsSize; ++i) line[i] = 0.f;

    /*
     * Start the write head past the excitation. At 0 it would overwrite the
     * burst with silence before the read head ever reached it, and the string
     * would never sound.
     */
    v.ksWrite = len & kKsMask;
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;
  dsp::Noise noise_;
  dsp::SVF body1_, body2_;
  dsp::LoFi lofi_;

  Preset preset_ = kBanjo;
  Params p_ = {0.95f, 0.10f, 0.97f, 700.f, 70.f, 300.f, 620.f, 0.76f, 0.60f, 0.95f};

  float tone_ = 0.5f;
  float velSens_ = 0.6f;
  float lofiAmount_ = 0.25f;
  float attackAddMs_ = 0.f;
  float releaseAddMs_ = 0.f;
  float bendSemis_ = 0.f;
  float toneMod_ = 0.f;
  uint8_t runtimeNote_ = 60;

  void clearLines() {
    if (!lines_) return;
    for (uint32_t i = 0; i < kBufferSize; ++i) lines_[i] = 0.f;
  }

  float *lines(int v) { return lines_ + v * kKsSize; }

  float *lines_ = nullptr;
};
