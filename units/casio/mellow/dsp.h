/*
 *  casio/mellow/dsp.h — the PT-20 MELLOW voice.
 *
 *  All seven Casio units share one engine; this only pins the tone. The
 *  spec for it — partial mix, envelope, cutoff, vibrato — lives in
 *  ../pt20.h next to the other six, so they can be read against each other.
 *
 *  square with the harmonics pulled down and the lowpass closed.
 *
 *  Portable: no logue-sdk includes. SDK glue is in osc.h / unit.cc / header.c.
 */
#pragma once

#include "../pt20.h"

class CasioEngine : public Pt20Engine {
 public:
  void init() { Pt20Engine::init(Pt20Engine::kMellow); }
};
