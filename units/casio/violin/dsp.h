/*
 *  casio/violin/dsp.h — the PT-20 VIOLIN voice.
 *
 *  All seven Casio units share one engine; this only pins the tone. The
 *  spec for it — partial mix, envelope, cutoff, vibrato — lives in
 *  ../pt20.h next to the other six, so they can be read against each other.
 *
 *  narrow buzzing pulses, bowed attack, the PT's signature vibrato.
 *
 *  Portable: no logue-sdk includes. SDK glue is in osc.h / unit.cc / header.c.
 */
#pragma once

#include "../pt20.h"

class CasioEngine : public Pt20Engine {
 public:
  void init() { Pt20Engine::init(Pt20Engine::kViolin); }
};
