/*
 *  micro/modfx.h — logue-sdk Processor adapter around MicroEngine.
 *
 *  The class must be named ModFx: unit.cc and the websim wasm.cc shell both
 *  refer to it by name.
 */
#pragma once

#include "dsp.h"
#include "processor.h"

class ModFx : public Processor {
 public:
  /* In floats. The runtime multiplies by sizeof(float) when allocating. */
  uint32_t getBufferSize() const override final { return MicroEngine::kBufferSize; }

  void init(float *buffer) override final { engine_.init(buffer); }

  void reset() override final { engine_.reset(); }
  void resume() override final { engine_.reset(); }

  void setParameter(uint8_t index, int32_t value) override final {
    engine_.setParam(index, value);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    if (index == MicroEngine::P_MODE && value >= 0 && value < MicroEngine::kNumModes)
      return MicroEngine::modeName(value);
    return nullptr;
  }

  void process(const float *__restrict in, float *__restrict out,
               uint32_t frames) override final {
    engine_.process(in, out, frames);
  }

 private:
  MicroEngine engine_;
};
