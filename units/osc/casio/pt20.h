/*
 *  casio/pt20.h — the tone generator behind the seven Casio PT-20 oscillators.
 *
 *  Portable: no logue-sdk includes, so tests/render.cc builds the same code
 *  natively. Each unit under units/osc/casio/ is a thin dsp.h that pins one tone;
 *  everything else lives here so a fix reaches all seven at once.
 *
 *  The PT-20 (1983) is a divider synth. One master oscillator is divided down
 *  to square waves, a handful of them are mixed at fixed levels, and a simple
 *  envelope opens and closes the result. There is no filter sweep, no
 *  resonance and no velocity — the keyboard has no touch sensitivity at all.
 *  The tones differ only in which harmonics are in the mix, how the envelope
 *  is shaped, and how bright the output stage is.
 *
 *  So that is what this is: up to four pulse partials per voice, a fixed
 *  lowpass, an ADSR, one global vibrato, and a lo-fi stage standing in for the
 *  original's cheap output. Deliberately not a sampled emulation — it is the
 *  same construction, run at 48 kHz with band-limited oscillators so it stays
 *  usable across the whole keyboard instead of tearing above the top octave.
 *
 *  Two departures from the original, both because this is a poly unit on a
 *  synth the PT-20 never was: it plays chords, and the ten parameters let you
 *  move the tone off the factory setting. Zero on every knob is the PT-20.
 */
#pragma once

#include "adsr.h"
#include "blep.h"
#include "dsp_util.h"
#include "lfo.h"
#include "lofi.h"
#include "svf.h"
#include "voice_pool.h"

class Pt20Engine {
 public:
  static constexpr int kMaxVoices = 6;
  static constexpr int kMaxPartials = 4;

  enum ParamId {
    P_ATTACK = 0,  // A knob: added on top of the tone's own attack
    P_RELEASE,     // B knob: likewise for release
    P_TONE,        // brightness
    P_LOFI,        // the cheap converter
    P_VIB,         // vibrato depth, scales the tone's own
    P_DETUNE,      // beating between partials
    P_VELO,        // velocity sensitivity (the PT-20 had none)
    P_VOICES,      // 1..6
    P_OCTAVE,      // -2..+2
    P_COUNT
  };

  enum Tone {
    kPiano = 0,
    kOrgan,
    kViolin,
    kFlute,
    kHorn,
    kFantasy,
    kMellow,
    kNumTones
  };

  /*
   * One divider tap. `ratio` is the multiple of the fundamental, `width` the
   * pulse width — 0.5 is the square a real divider produces, narrower widths
   * hollow the partial out and bring in the even harmonics that make the reed
   * and brass tones nasal.
   */
  struct Partial {
    float ratio;
    float level;
    float width;
  };

  struct ToneSpec {
    Partial partials[kMaxPartials];
    int numPartials;
    float attackMs, decayMs, sustain, releaseMs;
    float cutoffHz;      // fixed lowpass, key-tracked
    float vibHz;         // 0 disables
    float vibDepth;      // semitones
    float vibDelayMs;    // vibrato fades in after the attack
    float detuneCents;   // spread across the partials
    float gain;
  };

  static const ToneSpec &spec(int tone) {
    /*
     * The seven factory voices. Levels are the mix straight out of the divider
     * chain; the envelope and the lowpass do the rest.
     */
    static const ToneSpec kSpecs[kNumTones] = {
        /* PIANO — square struck hard and left to decay. No sustain, so the
           voice frees itself; that is what the original does when you hold a
           key and the note dies anyway. */
        {{{1.f, 1.00f, 0.5f}, {2.f, 0.42f, 0.5f}, {3.f, 0.18f, 0.5f}, {0.f, 0.f, 0.f}},
         3, 1.5f, 900.f, 0.f, 160.f, 4600.f, 0.f, 0.f, 0.f, 0.f, 0.95f},

        /* ORGAN — octaves stacked off one divider, on and off almost instantly.
           The 8' / 4' / 2' / 1' drawbar shape. */
        {{{1.f, 1.00f, 0.5f}, {2.f, 0.70f, 0.5f}, {4.f, 0.42f, 0.5f}, {8.f, 0.18f, 0.5f}},
         4, 3.f, 60.f, 0.95f, 45.f, 6200.f, 0.f, 0.f, 0.f, 0.f, 0.80f},

        /* VIOLIN — narrow pulses for the buzzy odd-and-even spectrum, a slow
           bow-like attack, and the vibrato the PT is remembered for. */
        {{{1.f, 1.00f, 0.18f}, {2.f, 0.70f, 0.24f}, {3.f, 0.52f, 0.30f}, {4.f, 0.40f, 0.36f}},
         4, 95.f, 220.f, 0.85f, 190.f, 6000.f, 5.2f, 0.30f, 240.f, 4.f, 0.68f},

        /* FLUTE — almost the bare fundamental, rolled off hard. The softest
           thing the divider can make. */
        {{{1.f, 1.00f, 0.5f}, {2.f, 0.10f, 0.5f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}},
         2, 48.f, 140.f, 0.90f, 130.f, 1500.f, 4.6f, 0.10f, 420.f, 0.f, 1.00f},

        /* HORN — hollow narrow pulse with a midrange bump, medium attack. */
        {{{1.f, 1.00f, 0.30f}, {2.f, 0.48f, 0.5f}, {3.f, 0.26f, 0.5f}, {0.f, 0.f, 0.f}},
         3, 38.f, 260.f, 0.80f, 160.f, 2600.f, 4.8f, 0.07f, 500.f, 3.f, 0.90f},

        /* FANTASY — the famous one. Partials pulled slightly off the harmonic
           series so they beat against each other, a long decay and a longer
           release. Chime rather than tone. */
        {{{1.f, 1.00f, 0.5f}, {2.01f, 0.62f, 0.5f}, {3.02f, 0.36f, 0.4f}, {4.98f, 0.22f, 0.4f}},
         4, 6.f, 1800.f, 0.22f, 950.f, 5200.f, 3.4f, 0.06f, 300.f, 9.f, 0.78f},

        /* MELLOW — the dark one. Square with the harmonics pulled down and the
           lowpass closed, plus enough detune to keep it from sounding dead. */
        {{{1.f, 1.00f, 0.5f}, {2.f, 0.32f, 0.5f}, {3.f, 0.11f, 0.5f}, {0.f, 0.f, 0.f}},
         3, 28.f, 420.f, 0.72f, 300.f, 1450.f, 0.f, 0.f, 0.f, 5.f, 1.00f}};
    return kSpecs[tone < 0 ? 0 : (tone >= kNumTones ? kNumTones - 1 : tone)];
  }

  static const char *toneName(int tone) {
    static const char *kNames[kNumTones] = {"PIAN", "ORGN", "VIOL",
                                            "FLUT", "HORN", "FANT", "MELO"};
    return kNames[tone < 0 ? 0 : (tone >= kNumTones ? kNumTones - 1 : tone)];
  }

  void init(int tone) {
    tone_ = tone;
    pool_.init();
    lofi_.reset();
    vibLfo_.init(0.f);

    toneKnob_ = 0.5f;
    vibScale_ = 1.f;
    detuneScale_ = 1.f;
    velSens_ = 0.f;
    lofiAmount_ = 0.30f;
    octave_ = 0;
    attackAddMs_ = 0.f;
    releaseAddMs_ = 0.f;
    bendSemis_ = 0.f;
    toneMod_ = 0.f;
    runtimeNote_ = 60;

    lofi_.setAmount(lofiAmount_);
    vibLfo_.setRate(spec(tone_).vibHz);
    applyEnvelope();
  }

  void reset() {
    pool_.killAll();
    lofi_.reset();
  }

  int tone() const { return tone_; }

  // ---- parameters ---------------------------------------------------------

  void setParam(uint8_t id, int32_t value) {
    switch (id) {
      /*
       * ATK and REL add to the tone's own times rather than replacing them, so
       * zero is the factory voice and the knob only ever stretches it. The
       * square curve keeps the useful short end spread across most of the
       * travel.
       */
      case P_ATTACK: {
        const float k = dsp::knob(value);
        attackAddMs_ = k * k * 2000.f;
        applyEnvelope();
        break;
      }
      case P_RELEASE: {
        const float k = dsp::knob(value);
        releaseAddMs_ = k * k * 3000.f;
        applyEnvelope();
        break;
      }
      case P_TONE:
        toneKnob_ = dsp::knob(value);
        break;
      case P_LOFI:
        lofiAmount_ = value * 0.01f;
        lofi_.setAmount(lofiAmount_);
        break;
      case P_VIB:
        vibScale_ = value * 0.02f;  // 0..200 %, so 50 is the factory depth
        break;
      case P_DETUNE:
        detuneScale_ = value * 0.02f;
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
    /* The internal sequencer gates without a pitch; use the runtime's note. */
    if (note == kGateNote) note = runtimeNote_;
    /*
     * Velocity 0 is a note-off on the wire, and firmware before 1.2 sent 0 for
     * everything — so treat it as an off only if that key is actually down.
     */
    if (velo == 0) {
      if (pool_.findHeld(note)) {
        noteOff(note);
        return;
      }
      velo = 100;
    }

    const Pool::Assignment a = pool_.noteOn(note);
    /* velSens_ 0 is the PT-20: every key the same loudness. */
    const float norm = velo * (1.f / 127.f);
    a.voice->velocity = dsp::lerpf(1.f, norm, velSens_);
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

  /* 14-bit MIDI bend, 8192 = centre, +/- 2 semitones. */
  void setBend(uint16_t bend) {
    bendSemis_ = (static_cast<int32_t>(bend) - 8192) * (2.f / 8192.f);
  }

  /* The hardware shape LFO, -1..1, nudges brightness. */
  void setToneMod(float mod) { toneMod_ = mod; }

  // ---- audio --------------------------------------------------------------

  void process(float *__restrict out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = 0.f;

    const ToneSpec &s = spec(tone_);
    const float tone = dsp::clampf(0.f, toneKnob_ + toneMod_, 1.f);
    /* TONE is a +/- 2 octave trim on the tone's own fixed cutoff. */
    const float cutoff = s.cutoffHz * dsp::pow2f_fast((tone - 0.5f) * 4.f);
    const float detune = s.detuneCents * detuneScale_ * (1.f / 1200.f);
    const float transpose = static_cast<float>(octave_) * 12.f + bendSemis_;

    /*
     * One vibrato for the whole instrument. The PT-20's is a single modulator
     * on the master oscillator, so every voice moves together — running one
     * per voice would sound wider than the thing being modelled, and cost six
     * times as much.
     */
    const float vibDepth = s.vibDepth * vibScale_;
    const bool vibrato = s.vibHz > 0.f && vibDepth > 0.f;
    const float vibDelayFrames = s.vibDelayMs * 0.001f * dsp::kSampleRate;

    for (int i = 0; i < pool_.count(); ++i) {
      Voice &v = pool_[i];
      if (v.env.isIdle()) continue;

      /* Control rate: pitch and filter once per block. */
      const float base = dsp::noteToHz(v.note + transpose);
      const float keyTrack = (v.note - 60) * (0.30f / 12.f);
      v.filter.setCutoff(cutoff * dsp::pow2f_fast(keyTrack), 0.f);

      const float gain = v.velocity * s.gain * 0.32f;

      for (uint32_t n = 0; n < frames; ++n) {
        const float e = v.env.next();

        float pitchMod = 0.f;
        if (vibrato) {
          /* Fade the vibrato in, so the attack lands before it starts. */
          if (v.vibRamp < vibDelayFrames) v.vibRamp += 1.f;
          const float ramp = vibDelayFrames > 0.f
                                 ? dsp::clampf(0.f, v.vibRamp / vibDelayFrames, 1.f)
                                 : 1.f;
          pitchMod = vibValue_ * vibDepth * ramp;
        }
        const float hz = pitchMod != 0.f
                             ? base * dsp::pow2f_fast(pitchMod * (1.f / 12.f))
                             : base;

        float sum = 0.f;
        for (int p = 0; p < s.numPartials; ++p) {
          /* Spread the detune across the partials so they beat, not drift. */
          const float d = detune * static_cast<float>(p);
          v.osc[p].setFreq(hz * s.partials[p].ratio * dsp::pow2f_fast(d));
          sum += v.osc[p].pulse(s.partials[p].width) * s.partials[p].level;
          v.osc[p].advance();
        }

        out[n] += v.filter.lowpass(sum) * e * gain;
      }
    }

    /* The vibrato phasor runs whether or not anything is sounding. */
    for (uint32_t n = 0; n < frames; ++n) {
      if (vibrato) vibValue_ = vibLfo_.next();
      out[n] = dsp::softclipf(lofi_.process(out[n]));
    }
  }

  int activeVoices() const { return pool_.active(); }

 private:
  struct Voice {
    dsp::BlepOsc osc[kMaxPartials];
    dsp::SVF filter;
    dsp::ADSR env;
    uint8_t note = 60;
    float velocity = 1.f;
    float vibRamp = 0.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      filter.reset();
      /* Stagger the phases: six voices starting together click on the first
       * chord, and the divider they are imitating never lined up anyway. */
      for (int p = 0; p < kMaxPartials; ++p)
        osc[p].reset(index * 0.13f + p * 0.21f);
      vibRamp = 0.f;
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      if (retrigger) {
        filter.reset();
        vibRamp = 0.f;
      }
      env.gateOn(retrigger);
    }

    void kill() {
      env.kill();
      filter.reset();
      held = false;
    }
  };

  void applyEnvelope() {
    const ToneSpec &s = spec(tone_);
    for (int i = 0; i < kMaxVoices; ++i) {
      pool_[i].env.setAttack(s.attackMs + attackAddMs_);
      pool_[i].env.setDecay(s.decayMs);
      pool_[i].env.setSustain(s.sustain);
      pool_[i].env.setRelease(s.releaseMs + releaseAddMs_);
    }
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;
  dsp::LoFi lofi_;
  dsp::SineLfo vibLfo_;

  int tone_ = kPiano;
  float vibValue_ = 0.f;

  float toneKnob_ = 0.5f;
  float vibScale_ = 1.f;
  float detuneScale_ = 1.f;
  float velSens_ = 0.f;
  float lofiAmount_ = 0.30f;
  int32_t octave_ = 0;
  float attackAddMs_ = 0.f;
  float releaseAddMs_ = 0.f;
  float bendSemis_ = 0.f;
  float toneMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
};
