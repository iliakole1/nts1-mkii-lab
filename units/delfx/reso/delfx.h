/*
 *  reso/delfx.h — logue-sdk Processor adapter around ResoEngine.
 *
 *  The class must be named DelFx: unit.cc and the websim wasm.cc shell both
 *  refer to it by name.
 */
#pragma once

#include "dsp.h"
#include "processor.h"

class DelFx : public Processor {
 public:
  /* In floats. The runtime multiplies by sizeof(float) when allocating. */
  uint32_t getBufferSize() const override final { return ResoEngine::kBufferSize; }

  void init(float *buffer) override final { engine_.init(buffer); }

  void reset() override final { engine_.reset(); }
  void resume() override final { engine_.reset(); }

  void setParameter(uint8_t index, int32_t value) override final {
    engine_.setParam(index, value);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    if (index == ResoEngine::P_CHORD && value >= 0 && value < ResoEngine::kNumChords)
      return ResoEngine::chordName(value);
    return nullptr;
  }

  void process(const float *__restrict in, float *__restrict out,
               uint32_t frames) override final {
    engine_.process(in, out, frames);
  }

 private:
  ResoEngine engine_;
};
