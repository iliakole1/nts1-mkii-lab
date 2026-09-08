/*
 *  poly8/dsp.h — 8-voice polyphonic synth engine.
 *
 *  Portable: no logue-sdk includes, so tests/render.cc can build the exact same
 *  code natively. The SDK glue lives in osc.h / unit.cc / header.c.
 *
 *  Per voice: 2 detuned polyBLEP oscillators (saw -> square -> narrow pulse
 *  morph) -> TPT state variable lowpass -> ADSR. The ADSR also modulates the
 *  filter, positively or negatively.
 *
 *  Why this exists at all: the NTS-1's own amp EG and filter sit *after* the
 *  oscillator slot and are monophonic, so a chord played through them would be
 *  gated and filtered as one lump. A poly unit has to bring its own.
 */
#pragma once

#include "adsr.h"
#include "blep.h"
#include "dsp_util.h"
#include "svf.h"
#include "voice_pool.h"

class PolyEngine {
 public:
  static constexpr int kMaxVoices = 8;

  enum ParamId {
    P_SHAPE = 0,  // A knob: saw -> square -> narrow pulse
    P_CUTOFF,     // B knob: filter cutoff
    P_DETUNE,     // cents between the two oscillators of a voice
    P_VOICES,     // 1..8
    P_ATTACK,     // ms
    P_DECAY,      // ms
    P_SUSTAIN,    // %
    P_RELEASE,    // ms
    P_RESO,       // %
    P_EGAMT,      // filter envelope amount, -100..100 %
    P_COUNT
  };

  void init() {
    pool_.init();
    shape_ = 0.f;
    cutoffKnob_ = 0.75f;
    detuneCents_ = 8.f;
    attackMs_ = 4.f;
    decayMs_ = 400.f;
    sustain_ = 0.75f;
    releaseMs_ = 250.f;
    reso_ = 0.25f;
    egAmount_ = 0.35f;
    bendSemis_ = 0.f;
    shapeMod_ = 0.f;
    applyEnvelopeSettings();
  }

  void reset() { pool_.killAll(); }

  // ---- parameters ---------------------------------------------------------

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      case P_SHAPE:
        shape_ = dsp::knob(value);
        break;
      case P_CUTOFF:
        cutoffKnob_ = dsp::knob(value);
        break;
      case P_DETUNE:
        detuneCents_ = static_cast<float>(value);
        break;
      case P_VOICES:
        pool_.setCount(value);
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
      case P_RESO:
        reso_ = value * 0.01f;
        break;
      case P_EGAMT:
        egAmount_ = value * 0.01f;
        break;
      default:
        break;
    }
  }

  // ---- note events --------------------------------------------------------

  /*
   * Sequencer/arpeggiator gate events arrive with note = 0xFF when no explicit
   * gate handler is provided; the pitch to use is the runtime's, which unit.cc
   * feeds us every block.
   */
  static constexpr uint8_t kGateNote = 0xFFU;

  void setRuntimeNote(uint8_t note) { runtimeNote_ = note; }

  void noteOn(uint8_t note, uint8_t velo) {
    if (note == kGateNote) note = runtimeNote_;

    /*
     * Velocity 0 means note-off only if that key is actually sounding. NTS-1
     * mkII firmware before 1.2 reported velocity as always zero (logue-sdk
     * issue #105), and treating that as note-off would mute the unit entirely.
     */
    if (velo == 0) {
      if (pool_.findHeld(note)) {
        noteOff(note);
        return;
      }
      velo = 100;
    }

    const Pool::Assignment a = pool_.noteOn(note);
    a.voice->velocity = 0.25f + 0.75f * (velo * (1.f / 127.f));
    a.voice->start(a.retrigger);
  }

  void noteOff(uint8_t note) {
    if (note == kGateNote) {  // gate off carries no pitch: release everything
      allNoteOff();
      return;
    }
    pool_.noteOff(note);
  }

  void allNoteOff() { pool_.allNoteOff(); }

  /* 14-bit MIDI bend, 8192 = centre. +/- 2 semitones. */
  void setBend(uint16_t bend) {
    bendSemis_ = (static_cast<int32_t>(bend) - 8192) * (2.f / 8192.f);
  }

  /* NTS-1 shape LFO, -1..1, added to the SHAPE knob. */
  void setShapeMod(float mod) { shapeMod_ = mod; }

  // ---- audio --------------------------------------------------------------

  void process(float *__restrict out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = 0.f;

    /* Shape morph: 0 -> saw, 0.5 -> square, 1 -> 5% pulse. */
    const float shape = dsp::clampf(0.f, shape_ + shapeMod_, 1.f);
    const float sawMix = shape < 0.5f ? 1.f - shape * 2.f : 0.f;
    const float pulseWidth = shape < 0.5f ? 0.5f : dsp::lerpf(0.5f, 0.05f, (shape - 0.5f) * 2.f);

    const float baseCutoff = 20.f * dsp::pow2f_fast(cutoffKnob_ * 9.8f);
    const float detune = detuneCents_ * (1.f / 1200.f);

    for (int i = 0; i < pool_.count(); ++i) {
      Voice &v = pool_[i];
      if (v.env.isIdle()) continue;

      /* Control rate: pitch and filter coefficients once per block. */
      const float hz = dsp::noteToHz(v.note + bendSemis_);
      v.oscA.setFreq(hz * dsp::pow2f_fast(-detune));
      v.oscB.setFreq(hz * dsp::pow2f_fast(detune));

      const float keyTrack = (v.note - 60) * (0.35f / 12.f);
      const float envOct = egAmount_ * 6.f * v.env.value();
      v.filter.setCutoff(baseCutoff * dsp::pow2f_fast(keyTrack + envOct), reso_);

      const float gain = v.velocity * 0.3f;

      for (uint32_t n = 0; n < frames; ++n) {
        const float e = v.env.next();

        float a, b;
        if (sawMix >= 1.f) {
          a = v.oscA.saw();
          b = v.oscB.saw();
        } else if (sawMix <= 0.f) {
          a = v.oscA.pulse(pulseWidth);
          b = v.oscB.pulse(pulseWidth);
        } else {
          a = dsp::lerpf(v.oscA.pulse(pulseWidth), v.oscA.saw(), sawMix);
          b = dsp::lerpf(v.oscB.pulse(pulseWidth), v.oscB.saw(), sawMix);
        }
        v.oscA.advance();
        v.oscB.advance();

        out[n] += v.filter.lowpass((a + b) * 0.5f) * e * gain;
      }
    }

    for (uint32_t i = 0; i < frames; ++i) out[i] = dsp::softclipf(out[i]);
  }

  int activeVoices() const { return pool_.active(); }

 private:
  struct Voice {
    dsp::BlepOsc oscA, oscB;
    dsp::SVF filter;
    dsp::ADSR env;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      filter.reset();
      /*
       * Stagger phases so stacked voices don't start perfectly in sync.
       * The A/B offset is deliberately not 0.5: two antiphase saws at identical
       * frequency cancel their fundamental, which guts the sound at DTUN = 0.
       */
      oscA.reset(index * 0.11f);
      oscB.reset(index * 0.11f + 0.13f);
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

  float shape_ = 0.f;
  float cutoffKnob_ = 0.75f;
  float detuneCents_ = 8.f;
  float attackMs_ = 4.f;
  float decayMs_ = 400.f;
  float sustain_ = 0.75f;
  float releaseMs_ = 250.f;
  float reso_ = 0.25f;
  float egAmount_ = 0.35f;
  float bendSemis_ = 0.f;
  float shapeMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
};
