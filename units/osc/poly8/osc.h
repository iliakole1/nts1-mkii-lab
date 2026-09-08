/*
 *  poly8/osc.h — logue-sdk Processor adapter around PolyEngine.
 *
 *  The class must be named Osc: both unit.cc and the websim wasm.cc shell
 *  instantiate `Osc`.
 */
#pragma once

#include "dsp.h"
#include "processor.h"

class Osc : public Processor {
 public:
  /* NTS-1 mkII oscillators get no SDRAM. */
  uint32_t getBufferSize() const override final { return 0; }

  void init(float *) override final { engine_.init(); }

  void reset() override final { engine_.reset(); }

  void resume() override final { engine_.reset(); }

  void setParameter(uint8_t index, int32_t value) override final {
    engine_.setParam(index, value);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    (void)index;
    (void)value;
    return nullptr;  // every parameter renders numerically
  }

  /*
   * The runtime's pitch follows the *last* note only, so a poly unit ignores it
   * and tracks note events itself. See docs/polyphony.md.
   */
  void setPitch(float) {}

  /* Note number the runtime is currently on; only used for 0xFF gate events. */
  void setRuntimeNote(uint8_t note) { engine_.setRuntimeNote(note); }

  void setShapeLfo(float lfo) { engine_.setShapeMod(lfo); }

  void process(const float *__restrict in, float *__restrict out,
               uint32_t frames) override final {
    (void)in;  // audio input unused; runtime is told so in unit_init()
    engine_.process(out, frames);
  }

  void noteOn(uint8_t note, uint8_t velo) override final { engine_.noteOn(note, velo); }
  void noteOff(uint8_t note) override final { engine_.noteOff(note); }
  void allNoteOff() override final { engine_.allNoteOff(); }

  /*
   * Note: Processor declares pitchBend(uint8_t), which truncates the 14-bit
   * value the runtime passes. This overload hides it and keeps all 14 bits.
   */
  void pitchBend(uint16_t bend) { engine_.setBend(bend); }

 private:
  PolyEngine engine_;
};
