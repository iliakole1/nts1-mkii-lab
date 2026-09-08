/*
 *  casio/organ/dsp.h — the PT-20 ORGAN voice.
 *
 *  All seven Casio units share one engine; this only pins the tone. The
 *  spec for it — partial mix, envelope, cutoff, vibrato — lives in
 *  ../pt20.h next to the other six, so they can be read against each other.
 *
 *  8'/4'/2'/1' octaves off one divider, on and off instantly.
 *
 *  Portable: no logue-sdk includes. SDK glue is in osc.h / unit.cc / header.c.
 */
#pragma once

#include "../pt20.h"

class CasioEngine : public Pt20Engine {
 public:
  void init() { Pt20Engine::init(Pt20Engine::kOrgan); }
};
