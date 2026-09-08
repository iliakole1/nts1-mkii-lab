/*
 *  ensemble/modfx.h — logue-sdk Processor adapter around EnsembleEngine.
 *
 *  The class must be named ModFx: unit.cc and the websim wasm.cc shell
 *  instantiate `ModFx`.
 */
#pragma once

#include "dsp.h"
#include "processor.h"

class ModFx : public Processor {
 public:
  /* In floats. The runtime multiplies by sizeof(float) when allocating. */
  uint32_t getBufferSize() const override final { return EnsembleEngine::kBufferSize; }

  void init(float *buffer) override final { engine_.init(buffer); }

  void reset() override final { engine_.reset(); }
  void resume() override final { engine_.reset(); }

  void setParameter(uint8_t index, int32_t value) override final {
    engine_.setParam(index, value);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    static const char *kModes[EnsembleEngine::kNumModes] = {"I", "II", "DUAL"};
    if (index == EnsembleEngine::P_MODE && value >= 0 && value < EnsembleEngine::kNumModes)
      return kModes[value];
    return nullptr;
  }

  void process(const float *__restrict in, float *__restrict out,
               uint32_t frames) override final {
    engine_.process(in, out, frames);
  }

 private:
  EnsembleEngine engine_;
};
