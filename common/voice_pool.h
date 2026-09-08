/*
 *  voice_pool.h — note bookkeeping and voice stealing, shared by poly units.
 *
 *  VoiceT must provide:
 *      dsp::ADSR env;      uint8_t note;   bool held;   uint32_t age;
 *      void init(int index);   void start(bool retrigger);   void kill();
 *
 *  The engine keeps ownership of the sound: this only decides *which* voice a
 *  note lands on, and leaves velocity and DSP setup to the caller.
 */
#pragma once

#include "dsp_util.h"

namespace dsp {

template <typename VoiceT, int N>
class VoicePool {
 public:
  static constexpr int kMaxVoices = N;

  struct Assignment {
    VoiceT *voice;
    bool retrigger;  // false when the same key was already sounding
  };

  void init() {
    for (int i = 0; i < N; ++i) voices_[i].init(i);
    ageCounter_ = 0;
    count_ = N;
  }

  void killAll() {
    for (int i = 0; i < N; ++i) voices_[i].kill();
  }

  int count() const { return count_; }

  void setCount(int n) {
    n = static_cast<int>(clampf(1.f, static_cast<float>(n), static_cast<float>(N)));
    /* Voices dropped from the pool would otherwise be stranded mid-note. */
    for (int i = n; i < count_; ++i) voices_[i].kill();
    count_ = n;
  }

  VoiceT &operator[](int i) { return voices_[i]; }
  const VoiceT &operator[](int i) const { return voices_[i]; }

  Assignment noteOn(uint8_t note) {
    VoiceT *v = findHeld(note);
    const bool retrigger = (v == nullptr);
    if (v == nullptr) v = allocate();
    v->note = note;
    v->held = true;
    v->age = ++ageCounter_;
    return {v, retrigger};
  }

  void noteOff(uint8_t note) {
    for (int i = 0; i < N; ++i) {
      VoiceT &v = voices_[i];
      if (v.held && v.note == note) {
        v.held = false;
        v.env.gateOff();
      }
    }
  }

  void allNoteOff() {
    for (int i = 0; i < N; ++i) {
      voices_[i].held = false;
      voices_[i].env.gateOff();
    }
  }

  VoiceT *findHeld(uint8_t note) {
    for (int i = 0; i < count_; ++i)
      if (voices_[i].held && voices_[i].note == note) return &voices_[i];
    return nullptr;
  }

  int active() const {
    int n = 0;
    for (int i = 0; i < count_; ++i)
      if (!voices_[i].env.isIdle()) ++n;
    return n;
  }

 private:
  /*
   * Preference order: a silent voice nobody is holding, then any silent voice
   * (a key held past the end of a piano-style decay), then the quietest voice
   * already released, then the oldest. Stealing the quietest release tail is
   * what keeps fast passages from clicking.
   */
  VoiceT *allocate() {
    for (int i = 0; i < count_; ++i)
      if (voices_[i].env.isIdle() && !voices_[i].held) return &voices_[i];

    for (int i = 0; i < count_; ++i)
      if (voices_[i].env.isIdle()) return &voices_[i];

    VoiceT *best = nullptr;
    float quietest = 2.f;
    for (int i = 0; i < count_; ++i) {
      VoiceT &v = voices_[i];
      if (v.held) continue;
      if (v.env.value() < quietest) {
        quietest = v.env.value();
        best = &v;
      }
    }
    if (best) return best;

    best = &voices_[0];
    for (int i = 1; i < count_; ++i)
      if (voices_[i].age < best->age) best = &voices_[i];
    return best;
  }

  VoiceT voices_[N];
  uint32_t ageCounter_ = 0;
  int count_ = N;
};

}  // namespace dsp
