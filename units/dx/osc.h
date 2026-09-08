/*
 *  dx/osc.h — logue-sdk Processor adapter around DxEngine.
 */
#pragma once

#include "dsp.h"
#include "processor.h"

class Osc : public Processor {
 public:
  uint32_t getBufferSize() const override final { return 0; }  // no SDRAM for osc units

  void init(float *) override final { engine_.init(); }

  void reset() override final { engine_.reset(); }
  void resume() override final { engine_.reset(); }

  void setParameter(uint8_t index, int32_t value) override final {
    engine_.setParam(index, value);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    if (index == DxEngine::P_PRESET && value >= 0 && value < DxEngine::kNumPresets)
      return DxEngine::presetName(value);
    return nullptr;
  }

  /* Poly unit: pitch comes from note events, not the runtime. */
  void setPitch(float) {}

  void setRuntimeNote(uint8_t note) { engine_.setRuntimeNote(note); }

  void setShapeLfo(float lfo) { engine_.setToneMod(lfo); }

  void process(const float *__restrict in, float *__restrict out,
               uint32_t frames) override final {
    (void)in;
    engine_.process(out, frames);
  }

  void noteOn(uint8_t note, uint8_t velo) override final { engine_.noteOn(note, velo); }
  void noteOff(uint8_t note) override final { engine_.noteOff(note); }
  void allNoteOff() override final { engine_.allNoteOff(); }

  /* Both preset controls report this, so changing one updates the other. */
  int32_t currentPreset() const { return static_cast<int32_t>(engine_.preset()); }

  /* Hides Processor::pitchBend(uint8_t), which would truncate to 7 bits. */
  void pitchBend(uint16_t bend) { engine_.setBend(bend); }

 private:
  DxEngine engine_;
};
