/*
 *  shimmer/revfx.h — logue-sdk Processor adapter around ShimmerEngine.
 *
 *  Class must be named RevFx: unit.cc instantiates it.
 */
#pragma once

#include "dsp.h"
#include "processor.h"

class RevFx : public Processor {
 public:
  uint32_t getBufferSize() const override final { return ShimmerEngine::kBufferSize; }

  void init(float *buffer) override final { engine_.init(buffer); }

  void reset() override final { engine_.reset(); }
  void resume() override final { engine_.reset(); }

  void setParameter(uint8_t index, int32_t value) override final {
    engine_.setParam(index, value);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final {
    static const char *kPitches[ShimmerEngine::kNumPitches] = {"+12", "+7", "+24", "-12"};
    if (index == ShimmerEngine::P_PITCH && value >= 0 && value < ShimmerEngine::kNumPitches)
      return kPitches[value];
    return nullptr;
  }

  void process(const float *__restrict in, float *__restrict out,
               uint32_t frames) override final {
    engine_.process(in, out, frames);
  }

 private:
  ShimmerEngine engine_;
};
