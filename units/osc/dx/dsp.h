/*
 *  dx/dsp.h — six-operator FM, eight presets, 6 voices.
 *
 *  Every operator is a sine with its own four-stage envelope. What separates
 *  the presets is the routing — which operators modulate which, and which reach
 *  the output — plus the envelope on each modulator, because a modulator's
 *  envelope is a brightness contour rather than a loudness one. That is the
 *  whole trick of the machine this borrows from: a bell is a carrier whose
 *  modulator dies faster than it does, and brass is a carrier whose modulator
 *  arrives *later* than it does.
 *
 *  Routing is per patch rather than a fixed list of algorithm numbers: each
 *  patch carries a modulator bitmask per operator, a carrier mask, and one
 *  feedback operator. That is a superset of the classic 32 algorithms and it
 *  costs a byte per operator to store. The one invariant it keeps from them is
 *  that a modulator is always a higher-numbered operator than what it feeds,
 *  which is what lets the render loop evaluate 6 down to 1 in a single pass.
 *
 *  Anti-aliasing is deliberate and not authentic. FM sidebands are unbounded,
 *  so a bright patch up the keyboard folds them back at 48 kHz. The originals
 *  did that too and people liked it; here each modulator's depth is capped at
 *  control rate so the topmost sideband stays under Nyquist. The cap only
 *  engages high up, where the alternative is not period charm but a wrong note.
 *
 *  Portable: no logue-sdk includes. SDK glue lives in osc.h / unit.cc.
 */
#pragma once

#include "dsp_util.h"
#include "lfo.h"
#include "opeg.h"
#include "sine.h"
#include "voice_pool.h"

class DxEngine {
 public:
  static constexpr int kMaxVoices = 6;
  static constexpr int kOps = 6;

  enum ParamId {
    P_ATTACK = 0,  // A knob: added on top of the preset's attack
    P_RELEASE,     // B knob: likewise for release
    P_PRESET,      // EDIT menu, where the display renders preset names
    P_TONE,        // scales every modulator: the one control FM really wants
    P_FEEDBACK,
    P_LFO,
    P_VELO,
    P_VOICES,
    P_DETUNE,
    P_COUNT
  };

  enum Preset {
    kBell = 0,
    kMarimba,
    kVibes,
    kBass,
    kBrass,
    kStrings,
    kHarpsi,
    kClav,
    kNumPresets
  };

  struct OpSpec {
    float ratio;     // multiple of the note frequency
    float fixedHz;   // > 0 pins the operator, ignoring the key — bells need this
    float detune;    // cents
    float level;     // 0..1 output
    float velSens;   // 0..1, how much velocity scales this operator
    float keyScale;  // amplitude octaves per keyboard octave above middle C
    dsp::OpEG::Spec eg;
  };

  struct Patch {
    OpSpec op[kOps];
    uint8_t mod[kOps];  // bit j set: operator j modulates this one (j > i always)
    uint8_t carriers;   // bit i set: operator i reaches the output
    int8_t fbOp;        // operator carrying the feedback loop, -1 for none
    float fbAmount;
    float lfoHz, lfoPitch, lfoAmp, lfoDelayMs;
    float gain;
  };

  static const Patch &patch(int preset) {
    static const Patch kPatches[kNumPresets] = {
        /*
         * BELL — three carrier/modulator pairs, modulator ratios pulled off the
         * harmonic series so the partials never resolve into a pitch you could
         * hum. The strike is the third pair, whose modulator is gone in 60 ms.
         */
        {{{1.00f, 0.f, 0.f, 1.00f, 0.30f, -0.15f, {{1.f, 0.55f, 0.f, 0.f}, {2.f, 900.f, 4000.f, 1200.f}}},
          {3.51f, 0.f, 3.f, 0.62f, 0.55f, -0.35f, {{1.f, 0.20f, 0.f, 0.f}, {1.f, 320.f, 1400.f, 500.f}}},
          {2.00f, 0.f, -2.f, 0.55f, 0.30f, -0.15f, {{1.f, 0.45f, 0.f, 0.f}, {2.f, 700.f, 3000.f, 1000.f}}},
          {7.02f, 0.f, 5.f, 0.42f, 0.60f, -0.45f, {{1.f, 0.12f, 0.f, 0.f}, {1.f, 180.f, 700.f, 300.f}}},
          {4.98f, 0.f, 4.f, 0.30f, 0.30f, -0.25f, {{1.f, 0.30f, 0.f, 0.f}, {2.f, 500.f, 2000.f, 800.f}}},
          {11.03f, 0.f, -6.f, 0.34f, 0.70f, -0.55f, {{1.f, 0.05f, 0.f, 0.f}, {1.f, 60.f, 250.f, 150.f}}}},
         {0x02, 0x00, 0x08, 0x00, 0x20, 0x00},
         0x15,
         -1, 0.f,
         0.f, 0.f, 0.f, 0.f,
         0.85f},

        /*
         * MARIMBA — one carrier with a hard 4:1 knock on the attack, plus a
         * quiet partial a twelfth up. Rosewood, so everything is gone fast.
         */
        {{{1.00f, 0.f, 0.f, 1.00f, 0.25f, -0.10f, {{1.f, 0.40f, 0.f, 0.f}, {1.f, 260.f, 700.f, 180.f}}},
          {4.00f, 0.f, 0.f, 0.58f, 0.65f, -0.50f, {{1.f, 0.06f, 0.f, 0.f}, {1.f, 55.f, 180.f, 90.f}}},
          {3.00f, 0.f, 2.f, 0.28f, 0.30f, -0.35f, {{1.f, 0.20f, 0.f, 0.f}, {1.f, 150.f, 450.f, 140.f}}},
          {9.00f, 0.f, 0.f, 0.22f, 0.70f, -0.60f, {{1.f, 0.02f, 0.f, 0.f}, {1.f, 30.f, 90.f, 60.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}}},
         {0x02, 0x00, 0x08, 0x00, 0x00, 0x00},
         0x05,
         -1, 0.f,
         0.f, 0.f, 0.f, 0.f,
         0.95f},

        /*
         * VIBES — the same mallet shape as marimba with the knock pulled right
         * back, an aluminium-length decay, and the motor: an amplitude LFO,
         * which is what actually makes a vibraphone sound like one.
         */
        {{{1.00f, 0.f, 0.f, 1.00f, 0.20f, -0.10f, {{1.f, 0.60f, 0.f, 0.f}, {2.f, 700.f, 3200.f, 700.f}}},
          {4.00f, 0.f, 0.f, 0.26f, 0.55f, -0.55f, {{1.f, 0.05f, 0.f, 0.f}, {1.f, 90.f, 260.f, 120.f}}},
          {4.00f, 0.f, 3.f, 0.24f, 0.20f, -0.30f, {{1.f, 0.35f, 0.f, 0.f}, {2.f, 500.f, 1800.f, 500.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}}},
         {0x02, 0x00, 0x00, 0x00, 0x00, 0x00},
         0x05,
         -1, 0.f,
         5.6f, 0.f, 0.55f, 0.f,
         0.95f},

        /*
         * BASS — a 1:1 modulator with feedback, which is how you get a reedy
         * saw out of two sines, and a fast 2:1 click on top. No sustain to
         * speak of: it is a plucked electric bass, not a synth pad.
         */
        {{{1.00f, 0.f, 0.f, 1.00f, 0.20f, 0.f, {{1.f, 0.80f, 0.55f, 0.f}, {1.f, 350.f, 1200.f, 180.f}}},
          {1.00f, 0.f, 0.f, 0.72f, 0.55f, -0.30f, {{1.f, 0.30f, 0.12f, 0.f}, {1.f, 120.f, 500.f, 150.f}}},
          {2.00f, 0.f, 1.f, 0.34f, 0.70f, -0.45f, {{1.f, 0.05f, 0.f, 0.f}, {1.f, 45.f, 160.f, 90.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}}},
         {0x06, 0x00, 0x00, 0x00, 0x00, 0x00},
         0x01,
         1, 0.62f,
         0.f, 0.f, 0.f, 0.f,
         1.00f},

        /*
         * BRASS — the swell. The carrier is up in 60 ms but its modulator takes
         * 190 ms to arrive, so the note opens after it speaks. Getting that one
         * relationship right is most of what makes a brass patch convincing.
         */
        {{{1.00f, 0.f, 0.f, 1.00f, 0.25f, -0.05f, {{1.f, 0.85f, 0.80f, 0.f}, {60.f, 300.f, 1.f, 220.f}}},
          {1.00f, 0.f, 2.f, 0.78f, 0.55f, -0.25f, {{1.f, 0.62f, 0.55f, 0.f}, {190.f, 400.f, 1.f, 260.f}}},
          {2.00f, 0.f, -3.f, 0.42f, 0.25f, -0.15f, {{1.f, 0.75f, 0.70f, 0.f}, {80.f, 320.f, 1.f, 220.f}}},
          {1.00f, 0.f, 4.f, 0.46f, 0.50f, -0.30f, {{1.f, 0.55f, 0.48f, 0.f}, {230.f, 420.f, 1.f, 260.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}}},
         {0x02, 0x00, 0x08, 0x00, 0x00, 0x00},
         0x05,
         1, 0.22f,
         4.9f, 0.045f, 0.f, 700.f,
         0.82f},

        /*
         * STRINGS — thin on purpose. Two carriers a few cents apart beat
         * against each other for the ensemble, the attack is slow, and the
         * modulators stay quiet so it never turns brassy.
         */
        {{{1.00f, 0.f, -5.f, 1.00f, 0.20f, -0.10f, {{1.f, 0.88f, 0.85f, 0.f}, {260.f, 600.f, 1.f, 420.f}}},
          {1.00f, 0.f, 3.f, 0.34f, 0.35f, -0.30f, {{1.f, 0.55f, 0.50f, 0.f}, {320.f, 700.f, 1.f, 400.f}}},
          {2.00f, 0.f, 6.f, 0.55f, 0.20f, -0.10f, {{1.f, 0.85f, 0.82f, 0.f}, {300.f, 640.f, 1.f, 440.f}}},
          {2.00f, 0.f, -4.f, 0.26f, 0.35f, -0.35f, {{1.f, 0.48f, 0.44f, 0.f}, {360.f, 700.f, 1.f, 400.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}}},
         {0x02, 0x00, 0x08, 0x00, 0x00, 0x00},
         0x05,
         -1, 0.f,
         4.2f, 0.035f, 0.10f, 900.f,
         0.78f},

        /*
         * HARPSI — bright and short. Odd modulator ratios put energy high up
         * where a quill belongs, and every envelope is over inside a second.
         */
        {{{1.00f, 0.f, 0.f, 1.00f, 0.15f, -0.15f, {{1.f, 0.45f, 0.f, 0.f}, {1.f, 320.f, 900.f, 200.f}}},
          {3.00f, 0.f, 2.f, 0.66f, 0.45f, -0.45f, {{1.f, 0.42f, 0.f, 0.f}, {1.f, 220.f, 800.f, 150.f}}},
          {2.00f, 0.f, -2.f, 0.52f, 0.15f, -0.20f, {{1.f, 0.38f, 0.f, 0.f}, {1.f, 260.f, 750.f, 180.f}}},
          {5.00f, 0.f, 4.f, 0.50f, 0.50f, -0.55f, {{1.f, 0.32f, 0.f, 0.f}, {1.f, 180.f, 620.f, 120.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}}},
         {0x02, 0x00, 0x08, 0x00, 0x00, 0x00},
         0x05,
         -1, 0.f,
         0.f, 0.f, 0.f, 0.f,
         0.88f},

        /*
         * CLAV — the grit is the feedback operator, not a high ratio. Shorter
         * and darker than the harpsichord, with just enough sustain to hold
         * under a held chord.
         */
        {{{1.00f, 0.f, 0.f, 1.00f, 0.25f, -0.10f, {{1.f, 0.38f, 0.f, 0.f}, {1.f, 170.f, 620.f, 110.f}}},
          {1.00f, 0.f, 0.f, 0.70f, 0.60f, -0.35f, {{1.f, 0.36f, 0.f, 0.f}, {1.f, 190.f, 520.f, 100.f}}},
          {4.00f, 0.f, 3.f, 0.44f, 0.65f, -0.55f, {{1.f, 0.20f, 0.f, 0.f}, {1.f, 130.f, 420.f, 80.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}},
          {1.00f, 0.f, 0.f, 0.f, 0.f, 0.f, {{0.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}}}},
         {0x06, 0x00, 0x00, 0x00, 0x00, 0x00},
         0x01,
         1, 0.70f,
         0.f, 0.f, 0.f, 0.f,
         0.95f}};
    return kPatches[preset < 0 ? 0 : (preset >= kNumPresets ? kNumPresets - 1 : preset)];
  }

  static const char *presetName(int preset) {
    static const char *kNames[kNumPresets] = {"BELL", "MRMB", "VIBE", "BASS",
                                              "BRAS", "STRG", "HPSI", "CLAV"};
    return kNames[preset < 0 ? 0 : (preset >= kNumPresets ? kNumPresets - 1 : preset)];
  }

  void init() {
    sine_.init();
    pool_.init();
    lfo_.init(0.f);

    tone_ = 0.5f;
    fbScale_ = 1.f;
    lfoScale_ = 1.f;
    velSens_ = 0.6f;
    detuneScale_ = 1.f;
    attackAddMs_ = 0.f;
    releaseAddMs_ = 0.f;
    bendSemis_ = 0.f;
    toneMod_ = 0.f;
    runtimeNote_ = 60;
    lfoValue_ = 0.f;

    loadPreset(kBell);
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
      /*
       * ATK and REL add to whatever the preset asked for rather than replacing
       * it, so a preset keeps its shape and you stretch it from there. They
       * reach every operator, which is why stretching the attack on BRASS keeps
       * the swell instead of flattening it.
       */
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
      case P_TONE:
        tone_ = dsp::knob(value);
        break;
      case P_FEEDBACK:
        fbScale_ = value * 0.02f;  // 0..200 %, 50 is the preset's own amount
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
      case P_DETUNE:
        detuneScale_ = value * 0.02f;
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

  void setToneMod(float mod) { toneMod_ = mod; }

  // ---- audio --------------------------------------------------------------

  void process(float *__restrict out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = 0.f;

    const Patch &p = patch(preset_);
    /* TONE is a scale on every modulator, centred so 512 is the preset. */
    const float tone = dsp::clampf(0.f, tone_ + toneMod_ * 0.25f, 1.f) * 2.f;
    const float fb = p.fbAmount * fbScale_;

    const float lfoPitch = p.lfoPitch * lfoScale_;
    const float lfoAmp = dsp::clampf(0.f, p.lfoAmp * lfoScale_, 1.f);
    const bool lfoOn = p.lfoHz > 0.f && (lfoPitch > 0.f || lfoAmp > 0.f);
    const float lfoDelayFrames = p.lfoDelayMs * 0.001f * dsp::kSampleRate;

    for (int v = 0; v < pool_.count(); ++v) {
      Voice &vo = pool_[v];
      if (vo.env.isIdle()) continue;

      /* ---- control rate: frequencies, levels, and the anti-alias cap ---- */
      const float f0 = dsp::noteToHz(vo.note + bendSemis_);
      float freq[kOps];
      float level[kOps];
      for (int i = 0; i < kOps; ++i) {
        const OpSpec &os = p.op[i];
        const float cents = os.detune * detuneScale_ * (1.f / 1200.f);
        freq[i] = os.fixedHz > 0.f ? os.fixedHz
                                   : f0 * os.ratio * dsp::pow2f_fast(cents);
        /* Key scaling: modulators are quieter up the keyboard, or the top
         * octave turns to glass. */
        const float ks = dsp::pow2f_fast(os.keyScale * (vo.note - 60) * (1.f / 12.f));
        const float vel = dsp::lerpf(1.f, vo.velocity, os.velSens * velSens_);
        level[i] = os.level * ks * vel;
        if (!(p.carriers & (1 << i))) level[i] *= tone;  // TONE moves modulators only
      }

      /*
       * Cap each modulator so its topmost sideband stays under Nyquist. For a
       * phase modulation of A turns the index is 2*pi*A and the band reaches
       * carrier + (index + 1) * modulator, so solve that for A and clamp. An
       * operator feeding several carriers takes the tightest of them.
       */
      for (int i = 0; i < kOps; ++i) {
        float cap = 1e9f;
        for (int c = 0; c < kOps; ++c) {
          if (!(p.mod[c] & (1 << i))) continue;
          const float room = 0.45f * dsp::kSampleRate - freq[c];
          const float beta = room > 0.f ? room / fmaxf(freq[i], 1.f) - 1.f : 0.f;
          cap = fminf(cap, fmaxf(0.f, beta) * (1.f / (2.f * dsp::kPi)));
        }
        if (cap < 1e8f) level[i] = fminf(level[i], cap);
      }

      float inc[kOps];
      for (int i = 0; i < kOps; ++i) inc[i] = freq[i] * dsp::kSampleRateRecip;

      const float gain = p.gain * 0.30f;

      for (uint32_t n = 0; n < frames; ++n) {
        float pitchScale = 1.f;
        float ampScale = 1.f;
        if (lfoOn) {
          if (vo.lfoRamp < lfoDelayFrames) vo.lfoRamp += 1.f;
          const float ramp = lfoDelayFrames > 0.f
                                 ? dsp::clampf(0.f, vo.lfoRamp / lfoDelayFrames, 1.f)
                                 : 1.f;
          const float l = lfoValue_ * ramp;
          if (lfoPitch > 0.f) pitchScale = dsp::pow2f_fast(l * lfoPitch * (1.f / 12.f));
          if (lfoAmp > 0.f) ampScale = 1.f - lfoAmp * 0.5f * (1.f - l);
        }

        /*
         * Operators run 6 down to 1. A modulator is always higher-numbered than
         * what it feeds, so one pass is enough and every source is already
         * computed by the time it is read.
         */
        float opOut[kOps];
        float sum = 0.f;
        for (int i = kOps - 1; i >= 0; --i) {
          const float e = vo.env.eg[i].next();
          if (e <= 0.f && level[i] <= 0.f) {
            opOut[i] = 0.f;
            continue;
          }

          float phaseMod = 0.f;
          const uint8_t m = p.mod[i];
          for (int j = i + 1; j < kOps; ++j)
            if (m & (1 << j)) phaseMod += opOut[j];
          if (i == p.fbOp) phaseMod += vo.fbState * fb;

          vo.phase[i] += inc[i] * pitchScale;
          if (vo.phase[i] >= 1.f) vo.phase[i] -= 1.f;

          const float o =
              sine_.at(vo.phase[i] + phaseMod + dsp::SineTable::kPhaseBias) * e * level[i];
          if (i == p.fbOp) {
            /* Average the last two, or a feedback operator turns into noise
             * the moment the amount goes past about half. */
            vo.fbState = (o + vo.fbPrev) * 0.5f;
            vo.fbPrev = o;
          }
          opOut[i] = o;
          if (p.carriers & (1 << i)) sum += o;
        }

        out[n] += sum * gain * ampScale;
      }

      /* A percussive patch reaches zero with the key still down; releasing
       * `held` here is what lets the pool hand the voice straight back. */
      vo.env.refresh(p.carriers);
      if (vo.env.isIdle()) vo.held = false;
    }

    if (lfoOn)
      for (uint32_t n = 0; n < frames; ++n) lfoValue_ = lfo_.next();

    for (uint32_t i = 0; i < frames; ++i) out[i] = dsp::softclipf(out[i]);
  }

  int activeVoices() const { return pool_.active(); }

 private:
  /*
   * The pool drives a voice through `env`, expecting gateOff / isIdle / value.
   * A six-operator voice has six envelopes and no single one of them says
   * whether it is finished, so the operator envelopes live in here and this
   * answers for the voice as a whole: idle once every *carrier* is done, since
   * a modulator still running makes no sound on its own, and value() reports
   * the loudest carrier so the pool steals the quietest tail.
   */
  struct VoiceEnv {
    dsp::OpEG eg[kOps];

    void init() {
      for (int i = 0; i < kOps; ++i) eg[i].init();
      idle_ = true;
      level_ = 0.f;
    }

    void gateOn(bool retrigger) {
      for (int i = 0; i < kOps; ++i) eg[i].gateOn(retrigger);
      idle_ = false;
      level_ = 1.f;
    }

    void gateOff() {
      for (int i = 0; i < kOps; ++i) eg[i].gateOff();
    }

    void kill() {
      for (int i = 0; i < kOps; ++i) eg[i].kill();
      idle_ = true;
      level_ = 0.f;
    }

    /* Refreshed once per block — per sample would be six comparisons a voice
     * for an answer that cannot change fast enough to matter. */
    void refresh(uint8_t carriers) {
      bool live = false;
      float peak = 0.f;
      for (int i = 0; i < kOps; ++i) {
        if (!(carriers & (1 << i))) continue;
        if (!eg[i].isIdle()) live = true;
        peak = fmaxf(peak, eg[i].value());
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
    float phase[kOps] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    VoiceEnv env;
    float fbState = 0.f, fbPrev = 0.f;
    float lfoRamp = 0.f;
    uint8_t note = 60;
    float velocity = 1.f;
    bool held = false;
    uint32_t age = 0;

    void init(int index) {
      env.init();
      for (int i = 0; i < kOps; ++i) {
        /* Stagger phases: six voices starting at zero click on the first
         * chord, and two operators at the same ratio would cancel rather than
         * beat against each other. */
        float ph = index * 0.11f + i * 0.17f;
        phase[i] = ph - static_cast<float>(static_cast<int>(ph));
      }
      fbState = fbPrev = 0.f;
      lfoRamp = 0.f;
      held = false;
      age = 0;
    }

    void start(bool retrigger) {
      if (retrigger) {
        fbState = fbPrev = 0.f;
        lfoRamp = 0.f;
      }
      env.gateOn(retrigger);
    }

    void kill() {
      env.kill();
      fbState = fbPrev = 0.f;
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
      for (int i = 0; i < kOps; ++i)
        pool_[v].env.eg[i].setSpec(p.op[i].eg, attackAddMs_, releaseAddMs_);
    lfo_.setRate(p.lfoHz);
  }

  typedef dsp::VoicePool<Voice, kMaxVoices> Pool;
  Pool pool_;
  dsp::SineTable sine_;
  dsp::SineLfo lfo_;

  Preset preset_ = kBell;
  float lfoValue_ = 0.f;

  float tone_ = 0.5f;
  float fbScale_ = 1.f;
  float lfoScale_ = 1.f;
  float velSens_ = 0.6f;
  float detuneScale_ = 1.f;
  float attackAddMs_ = 0.f;
  float releaseAddMs_ = 0.f;
  float bendSemis_ = 0.f;
  float toneMod_ = 0.f;
  uint8_t runtimeNote_ = 60;
};
