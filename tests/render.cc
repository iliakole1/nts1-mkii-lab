/*
 *  render.cc — offline harness: run the unit DSP natively, dump WAVs, check
 *  the numbers. Nothing here touches the logue-sdk, so it builds with plain
 *  clang/g++ and runs in a second.
 *
 *    make test        # from the repo root
 *
 *  Writes WAV files into dist/test — listen to those before flashing anything.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <cstring>
#include <string>
#include <vector>

#include "../units/modfx/ensemble/dsp.h"
#include "../units/osc/poly8/dsp.h"
#include "../units/osc/epiano/dsp.h"
#include "../units/osc/organ/dsp.h"
#include "../units/modfx/drive/dsp.h"
#include "../units/osc/flute/dsp.h"
#include "../units/osc/piano/dsp.h"
#include "../units/osc/pluck/dsp.h"
#include "../units/revfx/shimmer/dsp.h"

/*
 * The seven Casio units each declare their own CasioEngine pinning one tone,
 * so their dsp.h files cannot all be included here at once. They are three
 * lines apiece; everything they pin lives in the shared engine, which is what
 * these tests drive. `make lint` checks that each unit pins the tone its
 * directory is named after.
 */
#include "../units/osc/casio/pt20.h"
#include "../units/osc/dx/dsp.h"
#include "../units/osc/cz/dsp.h"
#include "../units/osc/wt/dsp.h"
#include "../units/delfx/bbd/dsp.h"
#include "../units/delfx/reso/dsp.h"
#include "../units/modfx/micro/dsp.h"

/*
 * WtEngine takes its wave bank from the caller so the 16 KB lands in .bss
 * rather than being baked into the shipped ELF. The bank is deterministic and
 * read-only, so every engine here can share one.
 */
struct WtHarness : WtEngine {
  void init() {
    static int8_t table[WtEngine::kBufferSize];
    static bool built = false;
    if (!built) {
      WtEngine::buildTable(table);
      built = true;
    }
    WtEngine::init(table);
  }
};

/*
 * PluckEngine takes its delay-line storage from the caller so that the 24 KB
 * of lines land in .bss rather than being baked into the shipped ELF (see
 * units/osc/pluck/osc.h). The generic checks below all call init() with no
 * arguments, and several of them compare two live engines, so each one needs
 * storage of its own.
 */
struct PluckHarness : PluckEngine {
  void init() { PluckEngine::init(storage_); }
  float storage_[PluckEngine::kBufferSize];
};

namespace {

constexpr int kSR = 48000;
constexpr uint32_t kBlock = 64;  // NTS-1 mkII renders in small blocks

int g_failures = 0;

void check(bool ok, const std::string &what) {
  printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++g_failures;
}

void writeWav(const std::string &path, const std::vector<float> &samples, int channels) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) {
    printf("  cannot write %s\n", path.c_str());
    ++g_failures;
    return;
  }
  const uint32_t dataBytes = static_cast<uint32_t>(samples.size()) * 2;
  const uint32_t byteRate = kSR * channels * 2;
  const uint16_t blockAlign = static_cast<uint16_t>(channels * 2);
  const uint32_t riffSize = 36 + dataBytes;
  const uint16_t fmt = 1, bits = 16, ch = static_cast<uint16_t>(channels);
  const uint32_t fmtSize = 16, sr = kSR;

  fwrite("RIFF", 1, 4, f);
  fwrite(&riffSize, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmtSize, 4, 1, f);
  fwrite(&fmt, 2, 1, f);
  fwrite(&ch, 2, 1, f);
  fwrite(&sr, 4, 1, f);
  fwrite(&byteRate, 4, 1, f);
  fwrite(&blockAlign, 2, 1, f);
  fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f);
  fwrite(&dataBytes, 4, 1, f);
  for (float s : samples) {
    const float c = s < -1.f ? -1.f : (s > 1.f ? 1.f : s);
    const int16_t v = static_cast<int16_t>(lrintf(c * 32767.f));
    fwrite(&v, 2, 1, f);
  }
  fclose(f);
  printf("  wrote %s (%zu frames)\n", path.c_str(), samples.size() / channels);
}

struct Stats {
  float peak = 0.f;
  float rms = 0.f;
  bool finite = true;
};

Stats analyse(const std::vector<float> &x) {
  Stats s;
  double acc = 0.0;
  for (float v : x) {
    if (!std::isfinite(v)) s.finite = false;
    const float a = fabsf(v);
    if (a > s.peak) s.peak = a;
    acc += static_cast<double>(v) * v;
  }
  s.rms = x.empty() ? 0.f : static_cast<float>(sqrt(acc / x.size()));
  return s;
}

/*
 * Autocorrelation pitch estimate. Every lag correlates the same fixed-length
 * window, otherwise short lags win on term count alone and report octave-up.
 */
float estimateHz(const std::vector<float> &x, size_t offset, size_t window) {
  const int minLag = kSR / 2000;  // 2 kHz
  const int maxLag = kSR / 50;    // 50 Hz
  if (offset + window + maxLag > x.size()) return 0.f;
  double best = -1e30;
  int bestLag = minLag;
  for (int lag = minLag; lag <= maxLag; ++lag) {
    double acc = 0.0;
    for (size_t i = 0; i < window; ++i)
      acc += static_cast<double>(x[offset + i]) * x[offset + i + lag];
    if (acc > best) {
      best = acc;
      bestLag = lag;
    }
  }
  return static_cast<float>(kSR) / bestLag;
}

/* Energy at one frequency, via Goertzel-style correlation. */
double energyAt(const std::vector<float> &x, size_t offset, size_t len, double hz,
                int stride = 1) {
  if (offset + len * stride > x.size()) return 0.0;
  double re = 0.0, im = 0.0;
  for (size_t i = 0; i < len; ++i) {
    const double a = 2.0 * M_PI * hz * static_cast<double>(i) / kSR;
    const double v = x[offset + i * stride];
    re += v * cos(a);
    im += v * sin(a);
  }
  return sqrt(re * re + im * im) / static_cast<double>(len);
}

std::vector<float> renderPoly(PolyEngine &e, int frames) {
  std::vector<float> out(frames, 0.f);
  for (int i = 0; i < frames; i += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min<int>(kBlock, frames - i));
    e.process(&out[i], n);
  }
  return out;
}

// ---------------------------------------------------------------------------

void testTuning() {
  printf("\npoly8: tuning\n");
  PolyEngine e;
  e.init();
  e.setParam(PolyEngine::P_DETUNE, 0);
  e.setParam(PolyEngine::P_CUTOFF, 1023);
  e.setParam(PolyEngine::P_EGAMT, 0);
  e.setParam(PolyEngine::P_ATTACK, 2);
  e.setParam(PolyEngine::P_SUSTAIN, 100);
  e.setParam(PolyEngine::P_SHAPE, 0);  // saw

  e.noteOn(69, 100);  // A4
  auto buf = renderPoly(e, kSR / 2);
  const float hz = estimateHz(buf, kSR / 8, kSR / 8);
  printf("  A4 measured %.2f Hz\n", hz);
  check(fabsf(hz - 440.f) < 2.f, "A4 renders at 440 Hz +/- 2");

  PolyEngine e2;
  e2.init();
  e2.setParam(PolyEngine::P_DETUNE, 0);
  e2.setParam(PolyEngine::P_CUTOFF, 1023);
  e2.setParam(PolyEngine::P_EGAMT, 0);
  e2.setParam(PolyEngine::P_SUSTAIN, 100);
  e2.noteOn(33, 100);  // A1, 55 Hz
  auto low = renderPoly(e2, kSR / 2);
  const float lowHz = estimateHz(low, kSR / 8, kSR / 8);
  printf("  A1 measured %.2f Hz\n", lowHz);
  check(fabsf(lowHz - 55.f) < 1.f, "A1 renders at 55 Hz +/- 1");
}

void testVoiceLifecycle() {
  printf("\npoly8: voices, stealing and release\n");
  PolyEngine e;
  e.init();
  e.setParam(PolyEngine::P_RELEASE, 200);
  e.setParam(PolyEngine::P_SUSTAIN, 80);

  for (int n = 0; n < 8; ++n) e.noteOn(static_cast<uint8_t>(48 + n * 2), 100);
  renderPoly(e, kBlock * 4);
  check(e.activeVoices() == 8, "8 held notes light up 8 voices");

  /* Four more notes on a full engine must steal, not explode. */
  for (int n = 0; n < 4; ++n) e.noteOn(static_cast<uint8_t>(72 + n), 100);
  auto stolen = renderPoly(e, kSR / 4);
  check(analyse(stolen).finite, "over-allocating stays finite");
  check(e.activeVoices() == 8, "voice count stays capped at 8");

  e.allNoteOff();
  auto tail = renderPoly(e, kSR);  // 1 s, release is 200 ms
  check(e.activeVoices() == 0, "all voices idle one second after note off");
  check(analyse(tail).peak > 0.f, "release tail is audible, not a hard cut");

  /* Retrigger the same note: must not stack two voices on one key. */
  PolyEngine r;
  r.init();
  r.noteOn(60, 100);
  renderPoly(r, kBlock);
  r.noteOn(60, 100);
  renderPoly(r, kBlock);
  check(r.activeVoices() == 1, "repeated note-on reuses its voice");
}

/*
 * Two quirks of this runtime, both cheap to get wrong:
 *  - firmware < 1.2 reports every velocity as 0 (logue-sdk #105)
 *  - sequencer/arp gate events arrive with note 0xFF and no pitch
 */
void testRuntimeQuirks() {
  printf("\npoly8: runtime quirks\n");

  PolyEngine e;
  e.init();
  e.noteOn(60, 0);  // velocity 0 on an idle key must still sound
  renderPoly(e, kBlock);
  check(e.activeVoices() == 1, "velocity 0 on a new note plays instead of muting");

  e.noteOn(60, 0);  // velocity 0 on a sounding key is a note off
  auto after = renderPoly(e, kSR);
  check(e.activeVoices() == 0, "velocity 0 on a held note releases it");
  check(analyse(after).finite, "still finite");

  PolyEngine g;
  g.init();
  g.setParam(PolyEngine::P_DETUNE, 0);
  g.setParam(PolyEngine::P_CUTOFF, 1023);
  g.setParam(PolyEngine::P_EGAMT, 0);
  g.setParam(PolyEngine::P_SUSTAIN, 100);
  g.setRuntimeNote(69);            // runtime sits on A4
  g.noteOn(PolyEngine::kGateNote, 100);  // gate on, no note number
  auto gated = renderPoly(g, kSR / 2);
  const float hz = estimateHz(gated, kSR / 8, kSR / 8);
  printf("  gate event with runtime note 69: %.2f Hz\n", hz);
  check(fabsf(hz - 440.f) < 2.f, "0xFF gate event follows the runtime pitch");

  g.noteOff(PolyEngine::kGateNote);
  renderPoly(g, kSR);
  check(g.activeVoices() == 0, "0xFF gate off releases the voice");
}

void testHeadroom() {
  printf("\npoly8: level\n");
  PolyEngine e;
  e.init();
  e.setParam(PolyEngine::P_SHAPE, 0);
  e.setParam(PolyEngine::P_CUTOFF, 1023);
  e.setParam(PolyEngine::P_RESO, 0);
  e.setParam(PolyEngine::P_SUSTAIN, 100);
  e.setParam(PolyEngine::P_EGAMT, 0);
  for (int n = 0; n < 8; ++n) e.noteOn(static_cast<uint8_t>(40 + n * 3), 127);
  auto buf = renderPoly(e, kSR / 2);
  const Stats s = analyse(buf);
  printf("  8 voices at full velocity: peak %.3f  rms %.3f\n", s.peak, s.rms);
  check(s.finite, "output is finite");
  check(s.peak <= 1.0001f, "output never exceeds full scale");
  check(s.rms > 0.05f, "output is not anaemic");
}

void testSilence() {
  printf("\npoly8: silence\n");
  PolyEngine e;
  e.init();
  auto buf = renderPoly(e, kSR / 10);
  check(analyse(buf).peak == 0.f, "no notes held means digital silence");
}

// ---------------------------------------------------------------------------

/* The three preset instruments share an interface, so they share a harness. */
template <typename EngineT>
std::vector<float> renderVoice(EngineT &e, int frames) {
  std::vector<float> out(frames, 0.f);
  for (int i = 0; i < frames; i += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min<int>(kBlock, frames - i));
    e.process(&out[i], n);
  }
  return out;
}

/*
 * Every preset of every instrument must be in tune and in range.
 *
 * Not by autocorrelation: a stiff-string preset like the toy piano is
 * deliberately inharmonic and has no single period, and the piccolo sounds an
 * octave above the note it is given, on purpose. What actually matters is that
 * the energy lands on the note's harmonic grid rather than between the keys, so
 * compare energy on the grid against the same grid pushed 70 cents sharp.
 */
template <typename EngineT>
void checkPresetsInTune(const char *unit, const char *const *names, int count,
                        std::function<void(EngineT &)> setup = nullptr) {
  printf("\n%s: presets\n", unit);
  const double f0 = dsp::noteToHz(69);  // A4

  for (int preset = 0; preset < count; ++preset) {
    EngineT e;
    e.init();
    e.setParam(EngineT::P_PRESET, preset);
    if (setup) setup(e);  // e.g. turn LOFI off so quantisation cannot blur the bins
    e.noteOn(69, 100);
    auto buf = renderVoice(e, kSR / 2);

    const double detune = 1.0413;  // +70 cents: between the keys
    double onGrid = 0.0, offGrid = 0.0;
    /* Half-steps of the harmonic series, up to 6*f0: wide enough to cover a
     * preset that deliberately sounds an octave above the note it is given. */
    for (int n = 1; n <= 12; ++n) {
      const double hz = f0 * 0.5 * n;
      if (hz > 0.45 * kSR) break;
      onGrid += energyAt(buf, kSR / 16, 8192, hz);
      offGrid += energyAt(buf, kSR / 16, 8192, hz * detune);
    }

    const Stats s = analyse(buf);
    printf("  %s: on-grid %.5f vs off-grid %.5f, peak %.3f, rms %.3f\n", names[preset],
           onGrid, offGrid, s.peak, s.rms);
    check(onGrid > offGrid * 2.5,
          std::string(unit) + " " + names[preset] + " is in tune");
    check(s.finite && s.peak <= 1.0001f,
          std::string(unit) + " " + names[preset] + " stays in range");
    check(s.rms > 0.01f, std::string(unit) + " " + names[preset] + " actually sounds");
  }
}

/* Decays away under a held key, and hands the voice back. */
template <typename EngineT>
void checkDecays(const char *unit, int preset) {
  EngineT e;
  e.init();
  e.setParam(EngineT::P_PRESET, preset);
  e.noteOn(60, 110);
  auto early = renderVoice(e, kSR / 10);
  renderVoice(e, kSR * 5);
  auto late = renderVoice(e, kSR);
  printf("  %s held: rms %.4f early, %.4f after 5 s\n", unit, analyse(early).rms,
         analyse(late).rms);
  check(analyse(late).rms < analyse(early).rms * 0.1f,
        std::string(unit) + " decays under a held key");
  check(e.activeVoices() == 0, std::string(unit) + " returns the voice to the pool");
}


/*
 * Presets that all sound alike are the failure mode this catches. For each
 * preset we measure two things a listener would notice immediately —
 * brightness (zero-crossing rate) and how long the note lasts — and require the
 * set to actually spread out across them.
 */
struct Fingerprint {
  float centroidHz;  // spectral centre of mass: perceived brightness
  float lengthSec;   // time until it falls 20 dB below its peak
  float wobble;      // how much the level moves once the note is going
};

Fingerprint fingerprint(const std::vector<float> &x, float f0) {
  Fingerprint f{0.f, 0.f, 0.f};

  /*
   * Brightness as a spectral centroid measured at harmonics of the note being
   * played. Two dead ends got us here: zero-crossing rate just reports 2*f0
   * for anything with a strong fundamental, and fixed log-spaced bands sit
   * between the partials, so they measure leakage rather than the tone.
   *
   * Averaged over several windows, because a preset whose highs die in 100 ms
   * should not measure as bright as one that holds them.
   */
  double num = 0.0, den = 0.0;
  for (int w = 0; w < 6; ++w) {
    const size_t win = 8192;
    const size_t at = static_cast<size_t>(kSR) / 20 + static_cast<size_t>(w) * win;
    if (at + win > x.size()) break;
    /*
     * Half-steps of the harmonic series. Whole harmonics are not enough: a
     * drawbar organ's 16' sits an octave *below* the note, so a grid starting
     * at f0 cannot see it and reports dark registrations as bright ones.
     */
    for (int n = 1; n <= 32; ++n) {
      const double hz = static_cast<double>(f0) * 0.5 * n;
      if (hz > 0.45 * kSR) break;
      const double e = energyAt(x, at, win, hz);
      num += hz * e;
      den += e;
    }
  }
  f.centroidHz = static_cast<float>(den > 1e-12 ? num / den : 0.0);

  /* Envelope in 10 ms blocks, measured forward from wherever the peak lands. */
  const size_t block = kSR / 100;
  std::vector<float> env;
  for (size_t i = 0; i + block <= x.size(); i += block) {
    float m = 0.f;
    for (size_t j = 0; j < block; ++j) m = fmaxf(m, fabsf(x[i + j]));
    env.push_back(m);
  }
  float peak = 0.f;
  for (size_t i = 0; i < env.size(); ++i) peak = fmaxf(peak, env[i]);

  /*
   * Time from the note speaking until it first falls 20 dB under its peak, or
   * the whole buffer if it never does.
   *
   * Two things this must not do. Measuring from the peak block looks reasonable
   * and is not: on a dead-flat organ note the peak block is wherever the last
   * bit of numerical noise landed, often near the end, and the "length" then
   * reports whatever buffer was left after it. Measuring from block 1 is worse
   * for the opposite reason: anything with a slow attack is still under 10 % of
   * its eventual peak 10 ms in, and reports a length of one block. So find the
   * onset — the first block at half the peak — and scan forward from there.
   */
  size_t onset = 0;
  for (size_t i = 0; i < env.size(); ++i)
    if (env[i] >= peak * 0.5f) {
      onset = i;
      break;
    }
  f.lengthSec = static_cast<float>(env.size()) * 0.01f;
  for (size_t i = onset + 1; i < env.size(); ++i)
    if (env[i] < peak * 0.1f) {
      f.lengthSec = static_cast<float>(i) * 0.01f;
      break;
    }

  /*
   * Level movement across the middle of the note. Two organ registrations can
   * match in brightness and both sustain forever, and still be nothing alike
   * if one of them has a tremulant swinging it — that difference has to be
   * measurable or the duplicate check is blind to it.
   */
  const size_t from = env.size() / 5, to = env.size() * 4 / 5;
  if (to > from) {
    float lo = 1e9f, hi = 0.f;
    for (size_t i = from; i < to; ++i) {
      lo = fminf(lo, env[i]);
      hi = fmaxf(hi, env[i]);
    }
    f.wobble = hi > 1e-6f ? (hi - lo) / hi : 0.f;
  }
  return f;
}

template <typename EngineT>
void checkPresetsDiffer(const char *unit, const char *const *names, int count,
                        float minBrightSpread, float minLengthSpread) {
  printf("\n%s: presets are distinguishable\n", unit);
  std::vector<Fingerprint> prints;
  for (int preset = 0; preset < count; ++preset) {
    EngineT e;
    e.init();
    e.setParam(EngineT::P_PRESET, preset);
    e.noteOn(60, 100);
    auto buf = renderVoice(e, kSR * 3);
    const Fingerprint f = fingerprint(buf, dsp::noteToHz(60));
    printf("  %s: centroid %5.0f Hz, %.2f s long, wobble %.2f\n", names[preset],
           f.centroidHz, f.lengthSec, f.wobble);
    prints.push_back(f);
  }

  float minB = 1e9f, maxB = 0.f, minL = 1e9f, maxL = 0.f;
  for (const Fingerprint &f : prints) {
    minB = fminf(minB, f.centroidHz);
    maxB = fmaxf(maxB, f.centroidHz);
    minL = fminf(minL, f.lengthSec);
    maxL = fmaxf(maxL, f.lengthSec);
  }
  printf("  spread: centroid x%.2f, length x%.2f\n", maxB / fmaxf(minB, 1.f),
         maxL / fmaxf(minL, 0.01f));
  check(maxB / fmaxf(minB, 1.f) > minBrightSpread,
        std::string(unit) + " presets span a range of brightness");
  check(maxL / fmaxf(minL, 0.01f) > minLengthSpread,
        std::string(unit) + " presets span a range of note length");

  /* And no two presets may be near-identical on both axes. */
  int tooClose = 0;
  for (size_t i = 0; i < prints.size(); ++i)
    for (size_t j = i + 1; j < prints.size(); ++j) {
      const float db = fabsf(prints[i].centroidHz - prints[j].centroidHz) /
                       fmaxf(prints[i].centroidHz, 1.f);
      const float dl = fabsf(prints[i].lengthSec - prints[j].lengthSec) /
                       fmaxf(prints[i].lengthSec, 0.01f);
      const float dw = fabsf(prints[i].wobble - prints[j].wobble);
      if (db < 0.12f && dl < 0.20f && dw < 0.15f) {
        printf("  %s and %s are nearly identical\n", names[i], names[j]);
        ++tooClose;
      }
    }
  check(tooClose == 0, std::string(unit) + " has no duplicate-sounding presets");
}

/*
 * ATK and REL stretch the preset rather than replacing it. Both are measured
 * against the same preset with the knobs at zero, which must be unchanged from
 * the preset's own timing.
 */
template <typename EngineT>
void checkEnvelopeStretch(const char *unit, int preset) {
  printf("\n%s: attack and release stretch\n", unit);

  auto onsetMs = [](const std::vector<float> &x) {
    float peak = 0.f;
    for (float v : x) peak = fmaxf(peak, fabsf(v));
    for (size_t i = 0; i < x.size(); ++i)
      if (fabsf(x[i]) > peak * 0.5f)
        return static_cast<float>(i) * 1000.f / kSR;
    return 0.f;
  };

  /* Attack: how long until the note reaches half its peak. */
  EngineT fast, slow;
  fast.init();
  slow.init();
  fast.setParam(EngineT::P_PRESET, preset);
  slow.setParam(EngineT::P_PRESET, preset);
  fast.setParam(EngineT::P_ATTACK, 0);
  slow.setParam(EngineT::P_ATTACK, 1023);
  fast.noteOn(60, 100);
  slow.noteOn(60, 100);
  const float fastMs = onsetMs(renderVoice(fast, kSR * 2));
  const float slowMs = onsetMs(renderVoice(slow, kSR * 2));
  printf("  onset: %.1f ms at ATK 0, %.1f ms at ATK max\n", fastMs, slowMs);
  /*
   * Relative, not absolute. A struck or plucked preset decays while the
   * envelope is still opening, so a two-second attack on a note that is over
   * in 700 ms can only ever produce a soft swell — the onset lengthens by two
   * orders of magnitude and still lands well under a second.
   */
  check(slowMs > fastMs * 8.f + 10.f, std::string(unit) + " ATK stretches the attack");

  /* Release: how much tail is left a second after the key comes up. */
  auto tailRms = [&](int32_t rel) {
    EngineT e;
    e.init();
    e.setParam(EngineT::P_PRESET, preset);
    e.setParam(EngineT::P_RELEASE, rel);
    e.noteOn(60, 110);
    renderVoice(e, kSR / 3);
    e.noteOff(60);
    renderVoice(e, kSR);  // one second after the key comes up
    return analyse(renderVoice(e, kSR / 4)).rms;
  };
  const float shortTail = tailRms(0);
  const float longTail = tailRms(1023);
  printf("  tail 1 s after note off: %.5f at REL 0, %.5f at REL max\n", shortTail,
         longTail);
  check(longTail > shortTail * 5.f, std::string(unit) + " REL stretches the release");

  /* Zero must mean "exactly the preset", not "a bit of stretch". */
  EngineT plain, zeroed;
  plain.init();
  zeroed.init();
  plain.setParam(EngineT::P_PRESET, preset);
  zeroed.setParam(EngineT::P_PRESET, preset);
  zeroed.setParam(EngineT::P_ATTACK, 0);
  zeroed.setParam(EngineT::P_RELEASE, 0);
  plain.noteOn(60, 100);
  zeroed.noteOn(60, 100);
  auto a = renderVoice(plain, kSR / 2);
  auto b = renderVoice(zeroed, kSR / 2);
  float maxDiff = 0.f;
  for (size_t i = 0; i < a.size(); ++i) maxDiff = fmaxf(maxDiff, fabsf(a[i] - b[i]));
  check(maxDiff < 1e-6f, std::string(unit) + " leaves the preset alone at zero");
}

void testPresetInstruments() {
  const char *pianoNames[5] = {"GRND", "BRIT", "MELO", "HONK", "TOY"};
  const char *fluteNames[5] = {"FLUT", "OCAR", "PANF", "RECD", "PICC"};
  const char *pluckNames[6] = {"BANJ", "GTAR", "UKUL", "MAND", "HARP", "KOTO"};
  const char *epianoNames[4] = {"RHDS", "WURL", "DYNO", "BARK"};
  const char *organNames[6] = {"JAZZ", "ROCK", "VOX", "FARF", "PIPE", "THTR"};

  /* Units with a lo-fi stage get it turned off; quantisation noise would blur
   * the harmonic bins these checks measure. */
  auto clean = [](auto &e) {
    e.setParam(std::decay_t<decltype(e)>::P_LOFI, 0);
    e.setParam(std::decay_t<decltype(e)>::P_VELO, 0);
  };

  checkPresetsInTune<PianoEngine>("piano", pianoNames, 5, clean);
  checkPresetsInTune<FluteEngine>("flute", fluteNames, 5, clean);
  checkPresetsInTune<PluckHarness>("pluck", pluckNames, 6, clean);
  checkPresetsInTune<EPianoEngine>("epiano", epianoNames, 4);
  checkPresetsInTune<OrganEngine>("organ", organNames, 6);

  checkPresetsDiffer<PianoEngine>("piano", pianoNames, 5, 1.5f, 2.0f);
  /* Wind instruments hold for as long as you blow, so — as with the organ —
   * brightness and modulation have to carry the separation. */
  checkPresetsDiffer<FluteEngine>("flute", fluteNames, 5, 2.0f, 0.f);
  checkPresetsDiffer<PluckHarness>("pluck", pluckNames, 6, 1.8f, 3.0f);
  checkPresetsDiffer<EPianoEngine>("epiano", epianoNames, 4, 1.3f, 1.3f);
  /* Organs hold flat by definition, so there is no note-length axis to spread
   * across — brightness has to carry the whole separation. */
  checkPresetsDiffer<OrganEngine>("organ", organNames, 6, 2.5f, 0.f);

  printf("\npreset instruments: articulation\n");
  checkDecays<PianoEngine>("piano", PianoEngine::kGrand);
  checkDecays<PluckHarness>("pluck", PluckHarness::kGuitar);
  checkDecays<EPianoEngine>("epiano", EPianoEngine::kRhodes);

  checkEnvelopeStretch<PianoEngine>("piano", PianoEngine::kGrand);
  checkEnvelopeStretch<FluteEngine>("flute", FluteEngine::kFlute);
  checkEnvelopeStretch<PluckHarness>("pluck", PluckHarness::kGuitar);
  checkEnvelopeStretch<EPianoEngine>("epiano", EPianoEngine::kRhodes);
  checkEnvelopeStretch<OrganEngine>("organ", OrganEngine::kJazz);

  {
    /* Wind instruments hold instead. */
    FluteEngine e;
    e.init();
    e.noteOn(72, 100);
    auto early = renderVoice(e, kSR / 4);
    auto late = renderVoice(e, kSR * 2);
    printf("  flute held: rms %.4f early, %.4f after 2 s\n", analyse(early).rms,
           analyse(late).rms);
    check(analyse(late).rms > analyse(early).rms * 0.7f, "flute sustains while held");
    e.noteOff(72);
    renderVoice(e, kSR);
    check(e.activeVoices() == 0, "flute releases");
  }

  printf("\npreset instruments: velocity and polyphony\n");
  {
    PianoEngine soft, hard;
    soft.init();
    hard.init();
    soft.setParam(PianoEngine::P_VELO, 100);
    hard.setParam(PianoEngine::P_VELO, 100);
    soft.noteOn(60, 25);
    hard.noteOn(60, 127);
    const Stats s = analyse(renderVoice(soft, kSR / 4));
    const Stats h = analyse(renderVoice(hard, kSR / 4));
    printf("  piano soft rms %.4f, hard rms %.4f\n", s.rms, h.rms);
    check(h.rms > s.rms * 2.f, "piano velocity changes level");

    PluckHarness chord;
    chord.init();
    chord.setParam(PluckHarness::P_PRESET, PluckHarness::kUkulele);
    const uint8_t notes[6] = {55, 59, 62, 67, 71, 74};
    for (int i = 0; i < 6; ++i) chord.noteOn(notes[i], 110);
    auto strum = renderVoice(chord, kSR / 4);
    printf("  pluck voices on a six-note chord: %d\n", chord.activeVoices());
    check(chord.activeVoices() == 6, "pluck plays six strings");
    check(analyse(strum).finite && analyse(strum).peak <= 1.0001f,
          "pluck chord stays in range");
  }

  printf("\npreset instruments: lo-fi stage\n");
  {
    auto distinct = [](const std::vector<float> &x) {
      std::vector<float> v(x);
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
      return v.size();
    };
    PianoEngine clean, crunch;
    clean.init();
    crunch.init();
    clean.setParam(PianoEngine::P_LOFI, 0);
    crunch.setParam(PianoEngine::P_LOFI, 100);
    clean.noteOn(60, 120);
    crunch.noteOn(60, 120);
    const size_t c = distinct(renderVoice(clean, kSR / 4));
    const size_t d = distinct(renderVoice(crunch, kSR / 4));
    printf("  distinct sample values: %zu clean, %zu crunched\n", c, d);
    check(d < c / 10, "LOFI quantises the output");
    check(d > 4, "LOFI leaves a signal, not a square");
  }
}



void testOrgan() {
  printf("\norgan: behaviour\n");

  {
    /* An organ holds flat for as long as the key is down. */
    OrganEngine e;
    e.init();
    e.setParam(OrganEngine::P_PRESET, OrganEngine::kPipe);
    e.noteOn(60, 100);
    auto early = renderVoice(e, kSR / 4);
    auto late = renderVoice(e, kSR * 3);
    printf("  PIPE held: rms %.4f early, %.4f after 3 s\n", analyse(early).rms,
           analyse(late).rms);
    check(analyse(late).rms > analyse(early).rms * 0.85f, "organ sustains flat");
    e.noteOff(60);
    renderVoice(e, kSR / 2);
    check(e.activeVoices() == 0, "organ releases quickly");
  }

  {
    /*
     * Hammond percussion is single-trigger: it fires on the first key of a
     * phrase and not on notes added while something is already held.
     */
    OrganEngine e;
    e.init();
    e.setParam(OrganEngine::P_PRESET, OrganEngine::kJazz);
    e.noteOn(60, 100);
    auto first = renderVoice(e, kSR / 12);
    e.noteOn(67, 100);  // added under a held key: no ping
    auto second = renderVoice(e, kSR / 12);

    /* The percussion sits on the 3rd harmonic of the note that triggered it. */
    const double perc1 = energyAt(first, 0, first.size(), dsp::noteToHz(60) * 3.0);
    const double perc2 = energyAt(second, 0, second.size(), dsp::noteToHz(67) * 3.0);
    printf("  percussion: %.5f on the first key, %.5f on the second\n", perc1, perc2);
    check(perc2 < perc1 * 0.5, "percussion is single-trigger");

    e.allNoteOff();
    renderVoice(e, kSR / 2);
    e.noteOn(64, 100);  // keyboard was clear again: ping returns
    auto third = renderVoice(e, kSR / 12);
    const double perc3 = energyAt(third, 0, third.size(), dsp::noteToHz(64) * 3.0);
    printf("  percussion after release: %.5f\n", perc3);
    check(perc3 > perc2 * 1.5, "percussion re-arms once every key is up");
  }

  {
    /* Nine drawbars across eight voices must stay in range. */
    OrganEngine e;
    e.init();
    e.setParam(OrganEngine::P_PRESET, OrganEngine::kRock);
    e.setParam(OrganEngine::P_TONE, 1023);
    e.setParam(OrganEngine::P_DRIVE, 100);
    const uint8_t chord[8] = {36, 43, 48, 52, 55, 60, 64, 67};
    for (int i = 0; i < 8; ++i) e.noteOn(chord[i], 120);
    auto full = renderVoice(e, kSR / 2);
    const Stats s = analyse(full);
    printf("  ROCK, all drawbars, 8 notes, full drive: peak %.3f rms %.3f\n", s.peak,
           s.rms);
    check(e.activeVoices() == 8, "organ plays eight voices");
    check(s.finite && s.peak <= 1.0001f, "full organ stays in range");
  }
}

/* Demos: every preset of each instrument, one phrase each. */
void renderPresetDemos() {
  printf("\npreset instrument demos\n");

  const uint8_t chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};

  {
    std::vector<float> mono;
    for (int preset = 0; preset < PianoEngine::kNumPresets; ++preset) {
      PianoEngine e;
      e.init();
      e.setParam(PianoEngine::P_PRESET, preset);
      for (int c = 0; c < 2; ++c) {
        for (int n = 0; n < 3; ++n)
          e.noteOn(chords[c][n], static_cast<uint8_t>(100 - n * 12));
        auto held = renderVoice(e, kSR * 3 / 4);
        mono.insert(mono.end(), held.begin(), held.end());
        for (int n = 0; n < 3; ++n) e.noteOff(chords[c][n]);
        auto gap = renderVoice(e, kSR / 4);
        mono.insert(mono.end(), gap.begin(), gap.end());
      }
    }
    const Stats s = analyse(mono);
    printf("  piano demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
    check(s.finite && s.peak <= 1.0001f, "piano demo stays in range");
    writeWav("dist/test/piano.wav", mono, 1);
  }


  {
    std::vector<float> mono;
    for (int preset = 0; preset < EPianoEngine::kNumPresets; ++preset) {
      EPianoEngine e;
      e.init();
      e.setParam(EPianoEngine::P_PRESET, preset);
      for (int c = 0; c < 2; ++c) {
        for (int n = 0; n < 3; ++n)
          e.noteOn(chords[c][n], static_cast<uint8_t>(105 - n * 14));
        auto held = renderVoice(e, kSR * 3 / 4);
        mono.insert(mono.end(), held.begin(), held.end());
        for (int n = 0; n < 3; ++n) e.noteOff(chords[c][n]);
        auto gap = renderVoice(e, kSR / 4);
        mono.insert(mono.end(), gap.begin(), gap.end());
      }
    }
    const Stats s = analyse(mono);
    printf("  epiano demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
    check(s.finite && s.peak <= 1.0001f, "epiano demo stays in range");
    writeWav("dist/test/epiano.wav", mono, 1);
  }

  {
    std::vector<float> mono;
    for (int preset = 0; preset < OrganEngine::kNumPresets; ++preset) {
      OrganEngine e;
      e.init();
      e.setParam(OrganEngine::P_PRESET, preset);
      for (int c = 0; c < 2; ++c) {
        /* Release fully between chords so the percussion re-arms. */
        for (int n = 0; n < 3; ++n) e.noteOn(chords[c][n], 100);
        auto held = renderVoice(e, kSR * 2 / 3);
        mono.insert(mono.end(), held.begin(), held.end());
        e.allNoteOff();
        auto gap = renderVoice(e, kSR / 4);
        mono.insert(mono.end(), gap.begin(), gap.end());
      }
    }
    const Stats s = analyse(mono);
    printf("  organ demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
    check(s.finite && s.peak <= 1.0001f, "organ demo stays in range");
    writeWav("dist/test/organ.wav", mono, 1);
  }
  {
    std::vector<float> mono;
    for (int preset = 0; preset < FluteEngine::kNumPresets; ++preset) {
      FluteEngine e;
      e.init();
      e.setParam(FluteEngine::P_PRESET, preset);
      const uint8_t line[6] = {72, 74, 76, 79, 76, 72};
      for (int i = 0; i < 6; ++i) {
        e.noteOn(line[i], static_cast<uint8_t>(90 + (i % 3) * 12));
        auto held = renderVoice(e, kSR * 2 / 5);
        mono.insert(mono.end(), held.begin(), held.end());
        e.noteOff(line[i]);
        auto gap = renderVoice(e, kSR / 12);
        mono.insert(mono.end(), gap.begin(), gap.end());
      }
    }
    const Stats s = analyse(mono);
    printf("  flute demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
    check(s.finite && s.peak <= 1.0001f, "flute demo stays in range");
    writeWav("dist/test/flute.wav", mono, 1);
  }

  {
    std::vector<float> mono;
    for (int preset = 0; preset < PluckHarness::kNumPresets; ++preset) {
      PluckHarness e;
      e.init();
      e.setParam(PluckHarness::P_PRESET, preset);
      /* Roll the chord like a strum, then let it ring. */
      for (int c = 0; c < 2; ++c) {
        for (int n = 0; n < 3; ++n) {
          e.noteOn(chords[c][n], static_cast<uint8_t>(105 - n * 8));
          auto step = renderVoice(e, kSR / 24);
          mono.insert(mono.end(), step.begin(), step.end());
        }
        auto ring = renderVoice(e, kSR * 2 / 3);
        mono.insert(mono.end(), ring.begin(), ring.end());
        for (int n = 0; n < 3; ++n) e.noteOff(chords[c][n]);
        auto gap = renderVoice(e, kSR / 6);
        mono.insert(mono.end(), gap.begin(), gap.end());
      }
    }
    const Stats s = analyse(mono);
    printf("  pluck demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
    check(s.finite && s.peak <= 1.0001f, "pluck demo stays in range");
    writeWav("dist/test/pluck.wav", mono, 1);
  }
}

// ---------------------------------------------------------------------------

std::vector<float> toStereo(const std::vector<float> &mono) {
  std::vector<float> s(mono.size() * 2);
  for (size_t i = 0; i < mono.size(); ++i) {
    s[i * 2] = mono[i];
    s[i * 2 + 1] = mono[i];
  }
  return s;
}

std::vector<float> sineBurst(double hz, int frames, int fadeOut) {
  std::vector<float> x(frames);
  for (int i = 0; i < frames; ++i) {
    float a = 0.7f;
    if (i > frames - fadeOut) a *= static_cast<float>(frames - i) / fadeOut;
    x[i] = a * static_cast<float>(sin(2.0 * M_PI * hz * i / kSR));
  }
  return x;
}

template <typename FxT>
std::vector<float> runFx(FxT &fx, const std::vector<float> &stereoIn) {
  std::vector<float> out(stereoIn.size(), 0.f);
  const size_t frames = stereoIn.size() / 2;
  for (size_t i = 0; i < frames; i += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(kBlock, frames - i));
    fx.process(&stereoIn[i * 2], &out[i * 2], n);
  }
  return out;
}

void testShimmer() {
  printf("\nshimmer: tail behaviour\n");
  std::vector<float> ram(ShimmerEngine::kBufferSize, 0.f);

  /* A 220 Hz burst, then two seconds of silence to listen to the tail. */
  auto burst = sineBurst(220.0, kSR / 2, 2000);
  burst.resize(kSR * 3, 0.f);
  auto stereoIn = toStereo(burst);

  ShimmerEngine fx;
  fx.init(ram.data());
  fx.setParam(ShimmerEngine::P_TIME, 800);
  fx.setParam(ShimmerEngine::P_DEPTH, 700);
  fx.setParam(ShimmerEngine::P_MIX, 1023);  // fully wet, so we measure the tank
  fx.setParam(ShimmerEngine::P_PITCH, ShimmerEngine::kOctaveUp);
  auto wet = runFx(fx, stereoIn);

  /* The octave above the source must appear in the tail after the note stops. */
  const size_t tailStart = static_cast<size_t>(kSR) * 2 * 3 / 2;  // 1.5 s in, stereo
  const double at220 = energyAt(wet, tailStart, kSR / 2, 220.0, 2);
  const double at440 = energyAt(wet, tailStart, kSR / 2, 440.0, 2);
  printf("  tail energy: 220 Hz %.5f, 440 Hz %.5f\n", at220, at440);
  check(at440 > at220 * 0.5, "the tail grows an octave above the source");

  const Stats s = analyse(wet);
  check(s.finite && s.peak <= 1.0001f, "shimmer stays finite and in range");

  /* Worst case for stability: longest tank, deepest regeneration, no input. */
  ShimmerEngine runaway;
  runaway.init(ram.data());
  runaway.setParam(ShimmerEngine::P_TIME, 1023);
  runaway.setParam(ShimmerEngine::P_DEPTH, 1023);
  runaway.setParam(ShimmerEngine::P_MIX, 1023);
  auto hit = sineBurst(330.0, kSR / 4, 1000);
  hit.resize(kSR * 8, 0.f);
  auto tail = runFx(runaway, toStereo(hit));

  auto windowRms = [&](size_t startSec) {
    std::vector<float> w(tail.begin() + startSec * kSR * 2,
                         tail.begin() + (startSec + 1) * kSR * 2);
    return analyse(w).rms;
  };
  const float early = windowRms(1), late = windowRms(6);
  printf("  max settings: rms %.4f at 1 s, %.4f at 6 s\n", early, late);
  check(analyse(tail).finite, "max settings stay finite");
  check(late < early * 0.4f, "the tail decays instead of running away");

  /*
   * The shifted tail has to land *on* the note, and hold still. A grain-based
   * shifter fails this two ways: an inexact ratio detunes it, and the grain
   * boundaries scatter sidebands around it. Both read as "out of tune" long
   * before they read as "wrong pitch".
   */
  printf("\nshimmer: pitch accuracy\n");
  for (int p = 0; p < ShimmerEngine::kNumPitches; ++p) {
    const double kRatio[ShimmerEngine::kNumPitches] = {2.0, 1.4983071, 4.0, 0.5};
    const char *kName[ShimmerEngine::kNumPitches] = {"+12", "+7", "+24", "-12"};

    std::vector<float> ram2(ShimmerEngine::kBufferSize, 0.f);
    ShimmerEngine fx;
    fx.init(ram2.data());
    fx.setParam(ShimmerEngine::P_TIME, 300);   // short tank: mostly first-generation
    fx.setParam(ShimmerEngine::P_DEPTH, 900);
    fx.setParam(ShimmerEngine::P_MIX, 1023);
    fx.setParam(ShimmerEngine::P_DAMP, 20);
    fx.setParam(ShimmerEngine::P_LOWCUT, 0);
    fx.setParam(ShimmerEngine::P_PITCH, p);

    auto tone = toStereo(sineBurst(220.0, kSR * 4, 0));
    auto wet = runFx(fx, tone);

    const double want = 220.0 * kRatio[p];
    const size_t at = static_cast<size_t>(kSR) * 2;  // settled, stereo indexing
    /*
     * A long analysis window on purpose: a quarter tone at the octave *below*
     * the source is 1.6 Hz, and a short window cannot tell that from the note
     * itself — the shifter would pass by being unmeasurable.
     */
    const size_t win = 49152;  // ~1 s, ~1 Hz resolution
    const double onPitch = energyAt(wet, at, win, want, 2);
    /*
     * Off-pitch probes a quarter tone either side, but never closer than 8 Hz:
     * a quarter tone at the octave below the source is 1.6 Hz, and energy that
     * close to the carrier is a slow beat, not mistuning. Probing there would
     * fail a shifter that sounds perfectly in tune.
     */
    const double off = fmax(want * 0.0147, 8.0);
    const double flat = energyAt(wet, at, win, want - off, 2);
    const double sharp = energyAt(wet, at, win, want + off, 2);
    /* And where the old design's grain rate used to throw its sidebands. */
    const double sideband = energyAt(wet, at, win, want + 20.0, 2);
    (void)kRatio;

    printf("  %s: on pitch %.5f, quarter-tone off %.5f/%.5f, sideband %.5f\n", kName[p],
           onPitch, flat, sharp, sideband);
    check(onPitch > fmax(flat, sharp) * 8.0,
          std::string("shimmer ") + kName[p] + " lands on pitch");
    check(onPitch > sideband * 8.0,
          std::string("shimmer ") + kName[p] + " does not scatter sidebands");
  }

  /* Dry path must be untouched at zero mix. */
  ShimmerEngine dry;
  dry.init(ram.data());
  dry.setParam(ShimmerEngine::P_MIX, 0);
  auto through = runFx(dry, stereoIn);
  double maxDiff = 0.0;
  for (size_t i = 0; i < through.size(); ++i)
    maxDiff = fmax(maxDiff, fabs(through[i] - stereoIn[i]));
  printf("  dry-path max deviation at MIX 0: %.6f\n", maxDiff);
  check(maxDiff < 0.02, "MIX 0 passes the input through");
}

void testDrive() {
  printf("\ndrive: shaping\n");
  auto tone = sineBurst(220.0, kSR / 2, 0);
  auto stereoIn = toStereo(tone);

  const double fundamental = energyAt(stereoIn, 0, kSR / 4, 220.0, 2);
  const double cleanThird = energyAt(stereoIn, 0, kSR / 4, 660.0, 2);

  const char *names[4] = {"SOFT", "FUZZ", "FOLD", "CRSH"};
  for (int mode = 0; mode < 4; ++mode) {
    DriveEngine fx;
    fx.init(nullptr);
    fx.setParam(DriveEngine::P_MODE, mode);
    fx.setParam(DriveEngine::P_DRIVE, 800);
    fx.setParam(DriveEngine::P_MIX, 100);
    auto out = runFx(fx, stereoIn);

    const double third = energyAt(out, kSR / 8, kSR / 4, 660.0, 2);
    const Stats s = analyse(out);
    printf("  %s: third harmonic %.5f (clean %.5f), peak %.3f\n", names[mode], third,
           cleanThird, s.peak);
    check(third > cleanThird * 20.0, std::string(names[mode]) + " adds harmonics");
    check(s.finite && s.peak <= 1.0001f, std::string(names[mode]) + " stays in range");
  }
  (void)fundamental;

  printf("\ndrive: aliasing and mix\n");
  {
    /*
     * A 7 kHz tone driven hard: its 7th harmonic lands at 49 kHz, which without
     * oversampling folds back to ~1 kHz — an inharmonic whistle. With 2x
     * oversampling it should stay well down.
     */
    auto high = toStereo(sineBurst(7000.0, kSR / 2, 0));
    DriveEngine fx;
    fx.init(nullptr);
    fx.setParam(DriveEngine::P_MODE, DriveEngine::kSoft);
    fx.setParam(DriveEngine::P_DRIVE, 950);
    fx.setParam(DriveEngine::P_MIX, 100);
    fx.setParam(DriveEngine::P_TONE, 1023);
    auto out = runFx(fx, high);

    const double atFund = energyAt(out, kSR / 8, kSR / 4, 7000.0, 2);
    const double atAlias = energyAt(out, kSR / 8, kSR / 4, 1000.0, 2);
    const double ratioDb = 20.0 * log10(fmax(atAlias, 1e-9) / fmax(atFund, 1e-9));
    printf("  7 kHz driven hard: alias at 1 kHz is %.1f dB below the fundamental\n",
           -ratioDb);
    check(ratioDb < -25.0, "oversampling keeps fold-back down");

    DriveEngine dryFx;
    dryFx.init(nullptr);
    dryFx.setParam(DriveEngine::P_MIX, 0);
    dryFx.setParam(DriveEngine::P_DRIVE, 900);
    auto through = runFx(dryFx, stereoIn);
    double maxDiff = 0.0;
    for (size_t i = 0; i < through.size(); ++i)
      maxDiff = fmax(maxDiff, fabs(through[i] - stereoIn[i]));
    printf("  dry-path max deviation at MIX 0: %.6f\n", maxDiff);
    check(maxDiff < 0.02, "MIX 0 passes the input through");
  }
}

/* Demos: rhodes into the shimmer, poly8 into the drive. */
void renderFxDemos() {
  printf("\nfx demo renders\n");

  {
    EPianoEngine e;
    e.init();  // RHDS preset by default
    const uint8_t chords[3][4] = {{48, 55, 60, 64}, {45, 52, 57, 64}, {41, 48, 55, 60}};
    std::vector<float> mono;
    for (int c = 0; c < 3; ++c) {
      for (int n = 0; n < 4; ++n) e.noteOn(chords[c][n], static_cast<uint8_t>(80 + n * 9));
      auto held = renderVoice(e, kSR);
      mono.insert(mono.end(), held.begin(), held.end());
      for (int n = 0; n < 4; ++n) e.noteOff(chords[c][n]);
      auto gap = renderVoice(e, kSR / 3);
      mono.insert(mono.end(), gap.begin(), gap.end());
    }
    mono.resize(mono.size() + kSR * 4, 0.f);  // room for the tail

    std::vector<float> ram(ShimmerEngine::kBufferSize, 0.f);
    ShimmerEngine fx;
    fx.init(ram.data());
    fx.setParam(ShimmerEngine::P_TIME, 780);
    fx.setParam(ShimmerEngine::P_DEPTH, 560);
    fx.setParam(ShimmerEngine::P_MIX, 430);
    fx.setParam(ShimmerEngine::P_DAMP, 45);
    fx.setParam(ShimmerEngine::P_PREDELAY, 30);
    fx.setParam(ShimmerEngine::P_LOWCUT, 30);
    auto out = runFx(fx, toStereo(mono));
    const Stats s = analyse(out);
    printf("  epiano -> shimmer: peak %.3f  rms %.3f\n", s.peak, s.rms);
    check(s.finite && s.peak <= 1.0001f, "epiano into shimmer stays in range");
    writeWav("dist/test/epiano_shimmer.wav", out, 2);
  }

  {
    PolyEngine e;
    e.init();
    e.setParam(PolyEngine::P_SHAPE, 120);
    e.setParam(PolyEngine::P_CUTOFF, 700);
    e.setParam(PolyEngine::P_DETUNE, 14);
    e.setParam(PolyEngine::P_SUSTAIN, 85);
    e.setParam(PolyEngine::P_RELEASE, 300);
    const uint8_t riff[6] = {40, 40, 47, 45, 43, 40};
    std::vector<float> mono;
    for (int i = 0; i < 6; ++i) {
      e.noteOn(riff[i], 120);
      e.noteOn(static_cast<uint8_t>(riff[i] + 12), 110);
      auto held = renderPoly(e, kSR / 3);
      mono.insert(mono.end(), held.begin(), held.end());
      e.noteOff(riff[i]);
      e.noteOff(static_cast<uint8_t>(riff[i] + 12));
      auto gap = renderPoly(e, kSR / 12);
      mono.insert(mono.end(), gap.begin(), gap.end());
    }

    std::vector<float> out;
    const int modes[3] = {DriveEngine::kSoft, DriveEngine::kFuzz, DriveEngine::kFold};
    for (int m = 0; m < 3; ++m) {
      DriveEngine fx;
      fx.init(nullptr);
      fx.setParam(DriveEngine::P_MODE, modes[m]);
      fx.setParam(DriveEngine::P_DRIVE, 640);
      fx.setParam(DriveEngine::P_TONE, 700);
      fx.setParam(DriveEngine::P_MIX, 100);
      fx.setParam(DriveEngine::P_LEVEL, 75);
      auto seg = runFx(fx, toStereo(mono));
      out.insert(out.end(), seg.begin(), seg.end());
    }
    const Stats s = analyse(out);
    printf("  poly8 -> drive (soft/fuzz/fold): peak %.3f  rms %.3f\n", s.peak, s.rms);
    check(s.finite && s.peak <= 1.0001f, "poly8 into drive stays in range");
    writeWav("dist/test/poly8_drive.wav", out, 2);
  }
}

/* A chord progression through poly8, then through the ensemble. */
void renderDemo() {
  printf("\ndemo render\n");
  PolyEngine e;
  e.init();
  e.setParam(PolyEngine::P_SHAPE, 300);
  e.setParam(PolyEngine::P_CUTOFF, 620);
  e.setParam(PolyEngine::P_DETUNE, 12);
  e.setParam(PolyEngine::P_ATTACK, 60);
  e.setParam(PolyEngine::P_DECAY, 900);
  e.setParam(PolyEngine::P_SUSTAIN, 60);
  e.setParam(PolyEngine::P_RELEASE, 700);
  e.setParam(PolyEngine::P_RESO, 35);
  e.setParam(PolyEngine::P_EGAMT, 45);

  const uint8_t chords[4][4] = {
      {48, 55, 60, 64},  // Cmaj
      {45, 52, 57, 64},  // Amin
      {41, 48, 53, 60},  // Fmaj
      {43, 50, 55, 62},  // Gmaj
  };

  std::vector<float> mono;
  for (int c = 0; c < 4; ++c) {
    for (int n = 0; n < 4; ++n) e.noteOn(chords[c][n], static_cast<uint8_t>(90 + n * 8));
    auto held = renderPoly(e, kSR * 3 / 4);
    mono.insert(mono.end(), held.begin(), held.end());
    for (int n = 0; n < 4; ++n) e.noteOff(chords[c][n]);
    auto gap = renderPoly(e, kSR / 4);
    mono.insert(mono.end(), gap.begin(), gap.end());
  }

  const Stats s = analyse(mono);
  printf("  poly8 demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
  check(s.finite && s.peak <= 1.0001f, "demo stays in range");
  writeWav("dist/test/poly8.wav", mono, 1);

  /* Same audio through the ensemble, as the hardware would chain them. */
  EnsembleEngine fx;
  std::vector<float> ram(EnsembleEngine::kBufferSize, 0.f);
  fx.init(ram.data());
  fx.setParam(EnsembleEngine::P_RATE, 420);
  fx.setParam(EnsembleEngine::P_DEPTH, 620);
  fx.setParam(EnsembleEngine::P_MIX, 60);
  fx.setParam(EnsembleEngine::P_MODE, EnsembleEngine::kModeII);
  fx.setParam(EnsembleEngine::P_SPREAD, 85);
  fx.setParam(EnsembleEngine::P_TONE, 75);

  std::vector<float> stereoIn(mono.size() * 2), stereoOut(mono.size() * 2);
  for (size_t i = 0; i < mono.size(); ++i) {
    stereoIn[i * 2] = mono[i];
    stereoIn[i * 2 + 1] = mono[i];
  }
  for (size_t i = 0; i < mono.size(); i += kBlock) {
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(kBlock, mono.size() - i));
    fx.process(&stereoIn[i * 2], &stereoOut[i * 2], n);
  }

  const Stats fs = analyse(stereoOut);
  printf("  ensemble out: peak %.3f  rms %.3f\n", fs.peak, fs.rms);
  check(fs.finite && fs.peak <= 1.0001f, "ensemble stays in range");

  /* The two channels must differ, or the stereo spread is not working. */
  double diff = 0.0;
  for (size_t i = 0; i < mono.size(); ++i)
    diff += fabs(stereoOut[i * 2] - stereoOut[i * 2 + 1]);
  printf("  L/R mean difference: %.4f\n", diff / mono.size());
  check(diff / mono.size() > 0.001, "ensemble produces a stereo image");

  writeWav("dist/test/poly8_ensemble.wav", stereoOut, 2);
}

}  // namespace


// ---------------------------------------------------------------------------
// Casio PT-20 tones. One engine, seven voice specs.

static const char *const kCasioNames[Pt20Engine::kNumTones] = {
    "PIANO", "ORGAN", "VIOLIN", "FLUTE", "HORN", "FANTASY", "MELLOW"};

/* The tests want a bare divider tone: LOFI quantisation smears the bins. */
static Pt20Engine makeCasio(int tone, bool clean = true) {
  Pt20Engine e;
  e.init(tone);
  if (clean) e.setParam(Pt20Engine::P_LOFI, 0);
  return e;
}

void testCasioTones() {
  printf("\ncasio: tones\n");
  const double f0 = dsp::noteToHz(69);  // A4

  for (int tone = 0; tone < Pt20Engine::kNumTones; ++tone) {
    Pt20Engine e = makeCasio(tone);
    e.noteOn(69, 100);
    auto buf = renderVoice(e, kSR / 2);

    /*
     * FANTASY is deliberately inharmonic — its partials sit a little off the
     * series so they beat — so test the same way the preset instruments do:
     * energy on the note's harmonic grid against the grid pushed 70 cents
     * sharp, rather than a single-period pitch estimate.
     */
    const double detune = 1.0413;
    double onGrid = 0.0, offGrid = 0.0;
    for (int n = 1; n <= 12; ++n) {
      const double hz = f0 * 0.5 * n;
      if (hz > 0.45 * kSR) break;
      onGrid += energyAt(buf, kSR / 16, 8192, hz);
      offGrid += energyAt(buf, kSR / 16, 8192, hz * detune);
    }

    const Stats st = analyse(buf);
    printf("  %-7s on-grid %.5f vs off-grid %.5f, peak %.3f, rms %.3f\n",
           kCasioNames[tone], onGrid, offGrid, st.peak, st.rms);
    check(onGrid > offGrid * 2.5, std::string("casio ") + kCasioNames[tone] + " is in tune");
    check(st.finite && st.peak <= 1.0001f,
          std::string("casio ") + kCasioNames[tone] + " stays in range");
    check(st.rms > 0.01f, std::string("casio ") + kCasioNames[tone] + " actually sounds");
  }
}

void testCasioTonesDiffer() {
  printf("\ncasio: tones are distinguishable\n");
  std::vector<Fingerprint> prints;
  for (int tone = 0; tone < Pt20Engine::kNumTones; ++tone) {
    Pt20Engine e = makeCasio(tone);
    e.noteOn(60, 100);
    auto buf = renderVoice(e, kSR * 3);
    const Fingerprint f = fingerprint(buf, dsp::noteToHz(60));
    printf("  %-7s centroid %5.0f Hz, %.2f s long, wobble %.2f\n", kCasioNames[tone],
           f.centroidHz, f.lengthSec, f.wobble);
    prints.push_back(f);
  }

  float minB = 1e9f, maxB = 0.f, minL = 1e9f, maxL = 0.f;
  for (const Fingerprint &f : prints) {
    minB = fminf(minB, f.centroidHz);
    maxB = fmaxf(maxB, f.centroidHz);
    minL = fminf(minL, f.lengthSec);
    maxL = fmaxf(maxL, f.lengthSec);
  }
  printf("  spread: centroid x%.2f, length x%.2f\n", maxB / fmaxf(minB, 1.f),
         maxL / fmaxf(minL, 0.01f));
  check(maxB / fmaxf(minB, 1.f) > 1.8f, "casio tones span a range of brightness");
  check(maxL / fmaxf(minL, 0.01f) > 3.0f, "casio tones span a range of note length");

  /* Two units that sound the same are two wasted oscillator slots. */
  bool duplicate = false;
  for (size_t i = 0; i < prints.size(); ++i)
    for (size_t j = i + 1; j < prints.size(); ++j) {
      const float db = fabsf(prints[i].centroidHz - prints[j].centroidHz) /
                       fmaxf(prints[i].centroidHz, 1.f);
      const float dl = fabsf(prints[i].lengthSec - prints[j].lengthSec) /
                       fmaxf(prints[i].lengthSec, 0.01f);
      const float dw = fabsf(prints[i].wobble - prints[j].wobble);
      if (db < 0.06f && dl < 0.12f && dw < 0.06f) {
        printf("  %s and %s are too alike\n", kCasioNames[i], kCasioNames[j]);
        duplicate = true;
      }
    }
  check(!duplicate, "casio has no duplicate-sounding tones");
}

void testCasioArticulation() {
  printf("\ncasio: articulation\n");

  /* ATK and REL add on top of the voice; zero must mean exactly the voice. */
  for (int tone = 0; tone < Pt20Engine::kNumTones; ++tone) {
    Pt20Engine plain = makeCasio(tone);
    Pt20Engine zeroed = makeCasio(tone);
    zeroed.setParam(Pt20Engine::P_ATTACK, 0);
    zeroed.setParam(Pt20Engine::P_RELEASE, 0);
    plain.noteOn(60, 100);
    zeroed.noteOn(60, 100);
    auto a = renderVoice(plain, kSR / 2);
    auto b = renderVoice(zeroed, kSR / 2);
    float maxDiff = 0.f;
    for (size_t i = 0; i < a.size(); ++i) maxDiff = fmaxf(maxDiff, fabsf(a[i] - b[i]));
    check(maxDiff < 1e-6f,
          std::string("casio ") + kCasioNames[tone] + " leaves the voice alone at zero");
  }

  /* Stretching the attack has to actually delay the onset. */
  auto onsetMs = [](int32_t atk) {
    Pt20Engine e = makeCasio(Pt20Engine::kOrgan);
    e.setParam(Pt20Engine::P_ATTACK, atk);
    e.noteOn(60, 100);
    auto buf = renderVoice(e, kSR);
    float peak = 0.f;
    for (float v : buf) peak = fmaxf(peak, fabsf(v));
    for (size_t i = 0; i < buf.size(); ++i)
      if (fabsf(buf[i]) > peak * 0.5f) return static_cast<float>(i) * 1000.f / kSR;
    return 1000.f;
  };
  const float fast = onsetMs(0), slow = onsetMs(1023);
  printf("  organ onset: %.1f ms at ATK 0, %.1f ms at ATK max\n", fast, slow);
  check(slow > fast * 5.f, "casio ATK stretches the attack");

  /* And stretching the release has to leave a longer tail. */
  auto tailRms = [](int32_t rel) {
    Pt20Engine e = makeCasio(Pt20Engine::kOrgan);
    e.setParam(Pt20Engine::P_RELEASE, rel);
    e.noteOn(60, 100);
    renderVoice(e, kSR / 3);
    e.noteOff(60);
    renderVoice(e, kSR / 2);
    return analyse(renderVoice(e, kSR / 4)).rms;
  };
  const float shortTail = tailRms(0), longTail = tailRms(1023);
  printf("  tail after note off: %.5f at REL 0, %.5f at REL max\n", shortTail, longTail);
  check(longTail > shortTail * 5.f, "casio REL stretches the release");
}

void testCasioBehaviour() {
  printf("\ncasio: behaviour\n");

  /* The PT-20 keyboard has no touch sensitivity, so VSEN defaults to 0. */
  {
    Pt20Engine soft = makeCasio(Pt20Engine::kOrgan);
    Pt20Engine hard = makeCasio(Pt20Engine::kOrgan);
    soft.noteOn(60, 20);
    hard.noteOn(60, 127);
    const float sr = analyse(renderVoice(soft, kSR / 4)).rms;
    const float hr = analyse(renderVoice(hard, kSR / 4)).rms;
    printf("  velocity 20 vs 127 at VSEN 0: rms %.4f vs %.4f\n", sr, hr);
    check(fabsf(sr - hr) < 1e-6f, "casio ignores velocity by default, as the PT-20 did");

    Pt20Engine s2 = makeCasio(Pt20Engine::kOrgan);
    Pt20Engine h2 = makeCasio(Pt20Engine::kOrgan);
    s2.setParam(Pt20Engine::P_VELO, 100);
    h2.setParam(Pt20Engine::P_VELO, 100);
    s2.noteOn(60, 20);
    h2.noteOn(60, 127);
    const float sr2 = analyse(renderVoice(s2, kSR / 4)).rms;
    const float hr2 = analyse(renderVoice(h2, kSR / 4)).rms;
    printf("  the same at VSEN 100: rms %.4f vs %.4f\n", sr2, hr2);
    check(hr2 > sr2 * 3.f, "casio VSEN turns velocity back on");
  }

  /* OCT must transpose by whole octaves, not something close to one. */
  {
    auto gridEnergy = [](int32_t oct, double hz) {
      Pt20Engine e = makeCasio(Pt20Engine::kFlute);
      e.setParam(Pt20Engine::P_OCTAVE, oct);
      e.noteOn(69, 100);  // A4 = 440
      auto buf = renderVoice(e, kSR / 2);
      return energyAt(buf, kSR / 16, 8192, hz);
    };
    const double up = gridEnergy(1, 880.0), upWrong = gridEnergy(1, 440.0);
    const double dn = gridEnergy(-1, 220.0), dnWrong = gridEnergy(-1, 440.0);
    printf("  OCT +1: 880 Hz %.5f vs 440 Hz %.5f\n", up, upWrong);
    printf("  OCT -1: 220 Hz %.5f vs 440 Hz %.5f\n", dn, dnWrong);
    check(up > upWrong * 3.0, "casio OCT +1 sounds an octave up");
    check(dn > dnWrong * 3.0, "casio OCT -1 sounds an octave down");
  }

  /* Voice allocation, stealing, and handing voices back. */
  {
    Pt20Engine e = makeCasio(Pt20Engine::kOrgan);
    for (int n = 0; n < 6; ++n) e.noteOn(static_cast<uint8_t>(48 + n * 2), 100);
    renderVoice(e, kBlock * 4);
    check(e.activeVoices() == 6, "casio 6 held notes light up 6 voices");

    for (int n = 0; n < 4; ++n) e.noteOn(static_cast<uint8_t>(72 + n), 100);
    auto stolen = renderVoice(e, kSR / 4);
    check(analyse(stolen).finite, "casio over-allocating stays finite");
    check(e.activeVoices() == 6, "casio voice count stays capped at 6");

    e.allNoteOff();
    renderVoice(e, kSR);
    check(e.activeVoices() == 0, "casio returns every voice to the pool");
  }

  /* PIANO has no sustain: a held key must die and free its voice. */
  {
    Pt20Engine e = makeCasio(Pt20Engine::kPiano);
    e.noteOn(60, 100);
    const float early = analyse(renderVoice(e, kSR / 4)).rms;
    renderVoice(e, kSR * 4);
    const float late = analyse(renderVoice(e, kSR / 4)).rms;
    printf("  piano held: rms %.4f early, %.4f after 4 s\n", early, late);
    check(late < early * 0.05f, "casio PIANO decays under a held key");
    check(e.activeVoices() == 0, "casio PIANO returns the voice to the pool");
  }

  /* Nothing held means digital silence, not a drifting DC offset. */
  {
    Pt20Engine e = makeCasio(Pt20Engine::kFantasy, false);
    auto buf = renderVoice(e, kSR / 10);
    check(analyse(buf).peak == 0.f, "casio is silent with no notes held");
  }

  /* The lo-fi stage has to be audible — it is the whole point of the LOFI knob. */
  {
    Pt20Engine clean = makeCasio(Pt20Engine::kOrgan);
    Pt20Engine crushed = makeCasio(Pt20Engine::kOrgan, false);
    crushed.setParam(Pt20Engine::P_LOFI, 100);
    clean.noteOn(60, 100);
    crushed.noteOn(60, 100);
    auto a = renderVoice(clean, kSR / 2);
    auto b = renderVoice(crushed, kSR / 2);
    float diff = 0.f;
    for (size_t i = 0; i < a.size(); ++i) diff = fmaxf(diff, fabsf(a[i] - b[i]));
    printf("  LOFI 0 vs 100 max sample difference: %.4f\n", diff);
    check(diff > 0.02f, "casio LOFI changes the sound");
    check(analyse(b).finite, "casio LOFI stays finite");
  }
}

void renderCasioDemo() {
  printf("\ncasio demo\n");
  const uint8_t chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};

  std::vector<float> mono;
  for (int tone = 0; tone < Pt20Engine::kNumTones; ++tone) {
    Pt20Engine e;
    e.init(tone);
    for (int c = 0; c < 2; ++c) {
      for (int n = 0; n < 3; ++n) e.noteOn(chords[c][n], 100);
      auto held = renderVoice(e, kSR * 3 / 4);
      mono.insert(mono.end(), held.begin(), held.end());
      for (int n = 0; n < 3; ++n) e.noteOff(chords[c][n]);
      auto gap = renderVoice(e, kSR / 2);
      mono.insert(mono.end(), gap.begin(), gap.end());
    }
  }
  const Stats s = analyse(mono);
  printf("  casio demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
  check(s.finite && s.peak <= 1.0001f, "casio demo stays in range");
  writeWav("dist/test/casio.wav", mono, 1);
}


// ---------------------------------------------------------------------------
// dx — six-operator FM.

static const char *const kDxNames[DxEngine::kNumPresets] = {
    "BELL", "MRMB", "VIBE", "BASS", "BRAS", "STRG", "HPSI", "CLAV"};

void testDx() {
  checkPresetsInTune<DxEngine>("dx", kDxNames, DxEngine::kNumPresets);
  checkPresetsDiffer<DxEngine>("dx", kDxNames, DxEngine::kNumPresets, 1.8f, 2.5f);
  checkDecays<DxEngine>("dx", DxEngine::kMarimba);
  checkEnvelopeStretch<DxEngine>("dx", DxEngine::kBell);

  printf("\ndx: operators\n");

  /* TONE scales every modulator, which on an FM voice is the brightness. */
  {
    auto centroid = [](int32_t tone) {
      DxEngine e;
      e.init();
      e.setParam(DxEngine::P_PRESET, DxEngine::kHarpsi);
      e.setParam(DxEngine::P_TONE, tone);
      e.noteOn(60, 100);
      auto buf = renderVoice(e, kSR / 2);
      return fingerprint(buf, dsp::noteToHz(60)).centroidHz;
    };
    const float dark = centroid(0), bright = centroid(1023);
    printf("  HPSI centroid: %.0f Hz at TONE 0, %.0f Hz at TONE max\n", dark, bright);
    check(bright > dark * 1.5f, "dx TONE opens the modulators");
  }

  /* TONE at zero must leave sines: no modulator, no sidebands. */
  {
    DxEngine e;
    e.init();
    e.setParam(DxEngine::P_PRESET, DxEngine::kBrass);
    e.setParam(DxEngine::P_TONE, 0);
    e.setParam(DxEngine::P_FEEDBACK, 0);
    e.noteOn(69, 100);
    auto buf = renderVoice(e, kSR / 2);
    const double f0 = dsp::noteToHz(69);
    const double fund = energyAt(buf, kSR / 16, 8192, f0);
    double upper = 0.0;
    for (int n = 3; n <= 8; ++n) upper += energyAt(buf, kSR / 16, 8192, f0 * n);
    printf("  BRAS at TONE 0: fundamental %.5f, harmonics 3-8 %.5f\n", fund, upper);
    check(fund > upper * 8.0, "dx collapses to sines at TONE 0");
  }

  /* Feedback is what makes the bass and clav reedy rather than hollow. */
  {
    auto render = [](int32_t fb) {
      DxEngine e;
      e.init();
      e.setParam(DxEngine::P_PRESET, DxEngine::kBass);
      e.setParam(DxEngine::P_FEEDBACK, fb);
      e.noteOn(40, 100);
      return renderVoice(e, kSR / 2);
    };
    auto none = render(0);
    auto lots = render(100);
    float diff = 0.f;
    for (size_t i = 0; i < none.size(); ++i) diff = fmaxf(diff, fabsf(none[i] - lots[i]));
    const float b0 = fingerprint(none, dsp::noteToHz(40)).centroidHz;
    const float b1 = fingerprint(lots, dsp::noteToHz(40)).centroidHz;
    printf("  BASS centroid %.0f Hz at FDBK 0, %.0f Hz at FDBK 100 (max diff %.3f)\n",
           b0, b1, diff);
    check(diff > 0.02f, "dx FDBK changes the sound");
    check(b1 > b0, "dx FDBK adds harmonics");
    check(analyse(lots).finite, "dx FDBK at maximum stays finite");
  }

  /*
   * The top of the keyboard. FM sidebands are unbounded, so a bright patch up
   * high folds them back — the modulator cap and the per-operator key scaling
   * exist to stop that. Measured as spectral spread: the same patch must be
   * rich low and much tamer high, and never louder up there.
   */
  {
    auto spread = [](uint8_t note) {
      DxEngine e;
      e.init();
      e.setParam(DxEngine::P_PRESET, DxEngine::kBell);
      e.setParam(DxEngine::P_TONE, 1023);
      e.noteOn(note, 127);
      auto buf = renderVoice(e, kSR / 2);
      const double f0 = dsp::noteToHz(note);
      const double fund = energyAt(buf, kSR / 16, 8192, f0);
      double away = 0.0;
      /* Deliberately off the harmonic grid: only folding puts energy here. */
      for (int n = 1; n <= 10; ++n) away += energyAt(buf, kSR / 16, 8192, f0 * (n + 0.37));
      return std::make_pair(static_cast<float>(away / fmax(fund, 1e-9)),
                            analyse(buf).peak);
    };
    const auto low = spread(48);
    const auto high = spread(108);
    printf("  BELL off-grid energy: %.3f at C3, %.3f at C8 (peaks %.3f / %.3f)\n",
           low.first, high.first, low.second, high.second);
    check(high.first < low.first, "dx tames the sidebands at the top of the keyboard");
    check(high.second <= low.second * 1.5f, "dx does not gain level up the keyboard");
    check(high.second <= 1.0001f, "dx stays in range at the top of the keyboard");
  }

  printf("\ndx: voices\n");
  {
    DxEngine e;
    e.init();
    e.setParam(DxEngine::P_PRESET, DxEngine::kStrings);
    for (int n = 0; n < 6; ++n) e.noteOn(static_cast<uint8_t>(48 + n * 2), 100);
    renderVoice(e, kBlock * 8);
    check(e.activeVoices() == 6, "dx 6 held notes light up 6 voices");

    for (int n = 0; n < 4; ++n) e.noteOn(static_cast<uint8_t>(72 + n), 100);
    auto stolen = renderVoice(e, kSR / 4);
    check(analyse(stolen).finite, "dx over-allocating stays finite");
    check(e.activeVoices() == 6, "dx voice count stays capped at 6");

    e.allNoteOff();
    renderVoice(e, kSR * 2);
    check(e.activeVoices() == 0, "dx returns every voice to the pool");
  }

  /* The percussive patches end on their own with the key still down. */
  {
    for (int preset : {DxEngine::kMarimba, DxEngine::kHarpsi, DxEngine::kBell}) {
      DxEngine e;
      e.init();
      e.setParam(DxEngine::P_PRESET, preset);
      e.noteOn(60, 110);
      const float early = analyse(renderVoice(e, kSR / 8)).rms;
      renderVoice(e, kSR * 6);
      const float late = analyse(renderVoice(e, kSR / 8)).rms;
      printf("  %s held: rms %.4f early, %.4f after 6 s\n", kDxNames[preset], early, late);
      check(late < early * 0.05f,
            std::string("dx ") + kDxNames[preset] + " decays under a held key");
      check(e.activeVoices() == 0,
            std::string("dx ") + kDxNames[preset] + " returns the voice to the pool");
    }
  }

  {
    DxEngine e;
    e.init();
    auto buf = renderVoice(e, kSR / 10);
    check(analyse(buf).peak == 0.f, "dx is silent with no notes held");
  }
}

void renderDxDemo() {
  printf("\ndx demo\n");
  const uint8_t chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};

  std::vector<float> mono;
  for (int preset = 0; preset < DxEngine::kNumPresets; ++preset) {
    DxEngine e;
    e.init();
    e.setParam(DxEngine::P_PRESET, preset);
    for (int c = 0; c < 2; ++c) {
      for (int n = 0; n < 3; ++n)
        e.noteOn(chords[c][n], static_cast<uint8_t>(105 - n * 10));
      auto held = renderVoice(e, kSR * 3 / 4);
      mono.insert(mono.end(), held.begin(), held.end());
      for (int n = 0; n < 3; ++n) e.noteOff(chords[c][n]);
      auto gap = renderVoice(e, kSR / 2);
      mono.insert(mono.end(), gap.begin(), gap.end());
    }
  }
  const Stats s = analyse(mono);
  printf("  dx demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
  check(s.finite && s.peak <= 1.0001f, "dx demo stays in range");
  writeWav("dist/test/dx.wav", mono, 1);
}


// ---------------------------------------------------------------------------
// cz — phase distortion.

static const char *const kCzNames[CzEngine::kNumPresets] = {
    "RESO", "BRAS", "EPNO", "BASS", "BELL", "STRG", "PIPE", "VOX"};

void testCz() {
  checkPresetsInTune<CzEngine>("cz", kCzNames, CzEngine::kNumPresets);
  checkPresetsDiffer<CzEngine>("cz", kCzNames, CzEngine::kNumPresets, 1.8f, 2.0f);
  checkDecays<CzEngine>("cz", CzEngine::kEPiano);
  checkEnvelopeStretch<CzEngine>("cz", CzEngine::kEPiano);

  printf("\ncz: distortion\n");

  /* DCW is the filter knob on a synth with no filter. */
  {
    auto centroid = [](int32_t dcw) {
      CzEngine e;
      e.init();
      e.setParam(CzEngine::P_PRESET, CzEngine::kReso);
      e.setParam(CzEngine::P_DCW, dcw);
      e.noteOn(60, 100);
      auto buf = renderVoice(e, kSR / 2);
      return fingerprint(buf, dsp::noteToHz(60)).centroidHz;
    };
    const float shut = centroid(0), open = centroid(1023);
    printf("  RESO centroid: %.0f Hz at DCW 0, %.0f Hz at DCW max\n", shut, open);
    check(open > shut * 2.f, "cz DCW opens the spectrum");
  }

  /* At DCW 0 the morphing shapes are undistorted cosines, so a preset built
   * from them has to collapse to something close to a sine. */
  {
    CzEngine e;
    e.init();
    e.setParam(CzEngine::P_PRESET, CzEngine::kEPiano);
    e.setParam(CzEngine::P_DCW, 0);
    e.noteOn(69, 100);
    auto buf = renderVoice(e, kSR / 4);
    const double f0 = dsp::noteToHz(69);
    const double fund = energyAt(buf, kSR / 32, 8192, f0);
    double upper = 0.0;
    for (int n = 3; n <= 9; ++n) upper += energyAt(buf, kSR / 32, 8192, f0 * n);
    printf("  EPNO at DCW 0: fundamental %.5f, harmonics 3-9 %.5f\n", fund, upper);
    check(fund > upper * 6.0, "cz collapses toward cosines at DCW 0");
  }

  /*
   * Every shape has to be a plain cosine when the sweep is shut, and none of
   * them may contain a step: a discontinuity inside the cycle is a click at
   * the fundamental on every period, which is the classic way to get a phase
   * distortion oscillator sounding harsh for no musical reason.
   */
  {
    CzEngine probe;
    probe.init();
    float worstDev = 0.f, worstStep = 0.f;
    for (int w = 0; w < CzEngine::kNumWaves; ++w) {
      /* The resonant three are a windowed harmonic, not a warped cosine, so
       * only the five morphing shapes are held to the cosine. */
      float prev = probe.probeShape(w, 0.f, 0.f, 1000.f);
      for (int i = 1; i <= 4000; ++i) {
        const float ph = i / 4000.f;
        const float got = probe.probeShape(w, ph, 0.f, 1000.f);
        if (w < CzEngine::kResoSaw)
          worstDev = fmaxf(worstDev, fabsf(got - cosf(2.f * dsp::kPi * ph)));
        worstStep = fmaxf(worstStep, fabsf(got - prev));
        prev = got;
      }
    }
    printf("  at DCW 0: %.5f from a cosine, largest step %.5f\n", worstDev, worstStep);
    check(worstDev < 0.005f, "cz morphing shapes are plain cosines at DCW 0");
    check(worstStep < 0.02f, "cz shapes have no discontinuity inside the cycle");
  }

  /*
   * The resonant peak climbs with the sweep and must stop before it runs off
   * the top of the band. Folding is what to look for, and folded products land
   * off the note's harmonic grid — so measure on-grid against the same grid
   * pushed 70 cents sharp, the way the preset tuning check does. Detune and
   * LFO off, or the patch's own movement shows up as off-grid energy.
   */
  {
    auto offGrid = [](uint8_t note) {
      CzEngine e;
      e.init();
      e.setParam(CzEngine::P_PRESET, CzEngine::kReso);
      e.setParam(CzEngine::P_DCW, 1023);
      e.setParam(CzEngine::P_DETUNE, 0);
      e.setParam(CzEngine::P_LFO, 0);
      e.noteOn(note, 127);
      auto buf = renderVoice(e, kSR / 2);
      const double f0 = dsp::noteToHz(note);
      double on = 0.0, off = 0.0;
      for (int n = 1; n <= 40; ++n) {
        const double hz = f0 * n;
        if (hz > 0.45 * kSR) break;
        on += energyAt(buf, kSR / 16, 8192, hz);
        off += energyAt(buf, kSR / 16, 8192, hz * 1.0413);
      }
      return std::make_pair(static_cast<float>(off / fmax(on, 1e-9)), analyse(buf).peak);
    };
    const auto low = offGrid(36);
    const auto high = offGrid(103);
    printf("  RESO off-grid share: %.4f at C2, %.4f at G7 (peaks %.3f / %.3f)\n",
           low.first, high.first, low.second, high.second);
    check(low.first < 0.30f, "cz sweep stays on the harmonic grid low down");
    check(high.first < 0.30f, "cz caps the resonant peak at the top of the keyboard");
    check(high.second <= 1.0001f, "cz stays in range at the top of the keyboard");
  }

  /* Every one of the eight shapes has to be finite and bounded across a full
   * sweep, including the corners of the distortion functions. */
  {
    bool ok = true;
    float worst = 0.f;
    for (int w = 0; w < CzEngine::kNumWaves; ++w) {
      for (int step = 0; step <= 20; ++step) {
        CzEngine e;
        e.init();
        /* Drive every preset's sweep to this point and check the output. The
         * shapes are exercised through the presets that use them. */
        e.setParam(CzEngine::P_PRESET, w % CzEngine::kNumPresets);
        e.setParam(CzEngine::P_DCW, step * 1023 / 20);
        e.noteOn(static_cast<uint8_t>(48 + w * 4), 120);
        auto buf = renderVoice(e, kSR / 20);
        const Stats st = analyse(buf);
        worst = fmaxf(worst, st.peak);
        if (!st.finite || st.peak > 1.0001f) ok = false;
      }
    }
    printf("  worst peak across every shape and sweep position: %.3f\n", worst);
    check(ok, "cz stays finite and in range across the whole sweep");
  }

  printf("\ncz: voices\n");
  {
    CzEngine e;
    e.init();
    e.setParam(CzEngine::P_PRESET, CzEngine::kStrings);
    for (int n = 0; n < 8; ++n) e.noteOn(static_cast<uint8_t>(48 + n * 2), 100);
    renderVoice(e, kBlock * 8);
    check(e.activeVoices() == 8, "cz 8 held notes light up 8 voices");

    for (int n = 0; n < 4; ++n) e.noteOn(static_cast<uint8_t>(72 + n), 100);
    auto stolen = renderVoice(e, kSR / 4);
    check(analyse(stolen).finite, "cz over-allocating stays finite");
    check(e.activeVoices() == 8, "cz voice count stays capped at 8");

    e.allNoteOff();
    renderVoice(e, kSR * 2);
    check(e.activeVoices() == 0, "cz returns every voice to the pool");
  }

  /*
   * OCT has to move by whole octaves. Measured by period rather than by
   * comparing two bins: several of these patches carry a line an octave above
   * the note, so "more energy at f0 than at 2*f0" is not true even when the
   * transpose is perfect.
   */
  {
    /*
     * Against the octave below, which for a signal periodic at f0 has to be
     * empty. Not autocorrelation: PIPE is dead flat, so every multiple of the
     * true period correlates equally well and the estimate lands on whichever
     * one float noise favours. Not a bin ratio against 2*f0 either, since
     * several patches carry a line up there on purpose.
     */
    auto fundamentalVsOctaveBelow = [](int32_t oct, double expected) {
      CzEngine e;
      e.init();
      e.setParam(CzEngine::P_PRESET, CzEngine::kReso);
      e.setParam(CzEngine::P_OCTAVE, oct);
      e.setParam(CzEngine::P_DETUNE, 0);
      e.setParam(CzEngine::P_LFO, 0);
      e.noteOn(69, 100);  // A4 = 440
      auto buf = renderVoice(e, kSR / 3);
      const double at = energyAt(buf, kSR / 16, 8192, expected);
      const double below = energyAt(buf, kSR / 16, 8192, expected * 0.5);
      return static_cast<float>(at / fmax(below, 1e-9));
    };
    const float mid = fundamentalVsOctaveBelow(0, 440.0);
    const float down = fundamentalVsOctaveBelow(-1, 220.0);
    const float up = fundamentalVsOctaveBelow(1, 880.0);
    printf("  fundamental vs the octave below it: %.1fx at OCT 0, %.1fx down, %.1fx up\n",
           mid, down, up);
    /*
     * The ceiling here is the analysis, not the signal: the subharmonic bin
     * only ever holds leakage from the fundamental, and a fixed 8192-sample
     * window holds fewer cycles the lower the note, so the ratio climbs with
     * pitch even though all three are equally clean. A transpose that did
     * nothing would score below 1, so a threshold of 5 discriminates with
     * room to spare.
     */
    check(mid > 5.f, "cz plays A4 at 440 Hz");
    check(down > 5.f, "cz OCT -1 sounds an octave down");
    check(up > 5.f, "cz OCT +1 sounds an octave up");
  }

  {
    CzEngine e;
    e.init();
    auto buf = renderVoice(e, kSR / 10);
    check(analyse(buf).peak == 0.f, "cz is silent with no notes held");
  }
}

void renderCzDemo() {
  printf("\ncz demo\n");
  const uint8_t chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};

  std::vector<float> mono;
  for (int preset = 0; preset < CzEngine::kNumPresets; ++preset) {
    CzEngine e;
    e.init();
    e.setParam(CzEngine::P_PRESET, preset);
    for (int c = 0; c < 2; ++c) {
      for (int n = 0; n < 3; ++n)
        e.noteOn(chords[c][n], static_cast<uint8_t>(105 - n * 10));
      auto held = renderVoice(e, kSR * 3 / 4);
      mono.insert(mono.end(), held.begin(), held.end());
      for (int n = 0; n < 3; ++n) e.noteOff(chords[c][n]);
      auto gap = renderVoice(e, kSR / 2);
      mono.insert(mono.end(), gap.begin(), gap.end());
    }
  }
  const Stats s = analyse(mono);
  printf("  cz demo: peak %.3f  rms %.3f\n", s.peak, s.rms);
  check(s.finite && s.peak <= 1.0001f, "cz demo stays in range");
  writeWav("dist/test/cz.wav", mono, 1);
}


// ---------------------------------------------------------------------------
// wt — wavetable scanning.

void testWt() {
  printf("\nwt: the wave bank\n");
  {
    static int8_t table[WtEngine::kBufferSize];
    WtEngine::buildTable(table);

    /* Every wave, at every band-limited level, must be present and normalised.
     * A level quieter than its neighbour puts a step in loudness exactly where
     * the note crosses from one to the other. */
    int silent = 0, unnormalised = 0;
    for (int w = 0; w < WtEngine::kWaves; ++w)
      for (int l = 0; l < WtEngine::kLevels; ++l) {
        int peak = 0;
        for (int i = 0; i < WtEngine::kLen; ++i) {
          const int v = table[(w * WtEngine::kLevels + l) * WtEngine::kLen + i];
          peak = std::max(peak, v < 0 ? -v : v);
        }
        if (peak < 8) ++silent;
        if (peak < 120) ++unnormalised;
      }
    printf("  %d waves x %d levels: %d silent, %d under-normalised\n", WtEngine::kWaves,
           WtEngine::kLevels, silent, unnormalised);
    check(silent == 0, "wt every wave and level has signal in it");
    check(unnormalised == 0, "wt every wave and level is normalised");

    /* The bank has to be a progression, not a list: neighbours differ a
     * little, the ends differ a lot. Scanning a table of near-duplicates is
     * the failure mode that still passes every other check. */
    float maxNeighbour = 0.f;
    for (int w = 0; w + 1 < WtEngine::kWaves; ++w) {
      float d = 0.f;
      for (int i = 0; i < WtEngine::kLen; ++i) {
        const int a = table[(w * WtEngine::kLevels) * WtEngine::kLen + i];
        const int b = table[((w + 1) * WtEngine::kLevels) * WtEngine::kLen + i];
        d = fmaxf(d, fabsf(static_cast<float>(a - b)));
      }
      maxNeighbour = fmaxf(maxNeighbour, d);
    }
    float ends = 0.f;
    for (int i = 0; i < WtEngine::kLen; ++i) {
      const int a = table[i];
      const int b = table[((WtEngine::kWaves - 1) * WtEngine::kLevels) * WtEngine::kLen + i];
      ends = fmaxf(ends, fabsf(static_cast<float>(a - b)));
    }
    printf("  largest step between neighbours %.0f, first vs last %.0f (of 254)\n",
           maxNeighbour, ends);
    check(ends > 60.f, "wt the ends of the bank are far apart");
    check(maxNeighbour < 200.f, "wt the bank is a progression, not a list of jumps");
  }

  printf("\nwt: tuning\n");
  {
    auto pitch = [](uint8_t note) {
      WtHarness e;
      e.init();
      e.setParam(WtEngine::P_DETUNE, 0);
      e.setParam(WtEngine::P_SWEEP, 0);
      e.setParam(WtEngine::P_WAVE, 400);
      e.setParam(WtEngine::P_CUTOFF, 1023);
      e.setParam(WtEngine::P_SUSTAIN, 100);
      e.setParam(WtEngine::P_EGFILTER, 0);
      e.noteOn(note, 100);
      auto buf = renderVoice(e, kSR / 2);
      return estimateHz(buf, kSR / 8, kSR / 8);
    };
    const float a4 = pitch(69), a1 = pitch(33);
    printf("  A4 %.2f Hz, A1 %.2f Hz\n", a4, a1);
    check(fabsf(a4 - 440.f) < 2.f, "wt A4 renders at 440 Hz +/- 2");
    check(fabsf(a1 - 55.f) < 1.f, "wt A1 renders at 55 Hz +/- 1");
  }

  printf("\nwt: scanning\n");
  {
    /* Moving the pointer has to change the timbre, and the far end of the bank
     * has to be brighter than the near end. */
    auto centroid = [](int32_t wave) {
      WtHarness e;
      e.init();
      e.setParam(WtEngine::P_WAVE, wave);
      e.setParam(WtEngine::P_SWEEP, 0);
      e.setParam(WtEngine::P_CUTOFF, 1023);
      e.setParam(WtEngine::P_EGFILTER, 0);
      e.setParam(WtEngine::P_SUSTAIN, 100);
      e.noteOn(60, 100);
      auto buf = renderVoice(e, kSR / 2);
      return fingerprint(buf, dsp::noteToHz(60)).centroidHz;
    };
    const float low = centroid(0), mid = centroid(512), high = centroid(1023);
    printf("  centroid across the bank: %.0f / %.0f / %.0f Hz\n", low, mid, high);
    check(high > low * 2.f, "wt scanning the bank changes the timbre");
    check(mid > low, "wt the bank gets brighter as it is scanned");

    /*
     * SWEP walks the pointer while the note sounds. Driven by the envelope, so
     * it needs one that actually moves: a fast attack into a long decay to
     * zero sustain sweeps the pointer up and walks it back down. Both windows
     * are taken after the attack has landed — measuring one inside a slow
     * attack compares against near-silence, whose centroid is zero, and the
     * test passes without having established anything.
     */
    WtHarness e;
    e.init();
    e.setParam(WtEngine::P_WAVE, 0);
    e.setParam(WtEngine::P_SWEEP, 100);
    e.setParam(WtEngine::P_CUTOFF, 1023);
    e.setParam(WtEngine::P_EGFILTER, 0);
    e.setParam(WtEngine::P_ATTACK, 8);
    e.setParam(WtEngine::P_DECAY, 2500);
    e.setParam(WtEngine::P_SUSTAIN, 0);
    e.noteOn(60, 100);
    renderVoice(e, kSR / 40);  // let the attack land
    /* A quarter second each: the centroid is averaged over 8192-sample windows
     * starting a twentieth of a second in, so anything shorter measures
     * nothing and silently reports zero. */
    auto head = renderVoice(e, kSR / 4);
    renderVoice(e, kSR);
    auto tail = renderVoice(e, kSR / 4);
    const float c0 = fingerprint(head, dsp::noteToHz(60)).centroidHz;
    const float c1 = fingerprint(tail, dsp::noteToHz(60)).centroidHz;
    printf("  one note with SWEP 100: %.0f Hz early, %.0f Hz late\n", c0, c1);
    check(c0 > 50.f, "wt the swept note has signal in both windows");
    check(c0 > c1 * 1.3f, "wt SWEP walks the pointer back as the envelope falls");
  }

  printf("\nwt: band limiting\n");
  {
    /*
     * The reason each wave is stored four times. Every harmonic is already in
     * the table, so there is no depth to clamp the way FM or phase distortion
     * allows — the note has to pick a copy that fits. Folded partials land off
     * the harmonic grid, so measure on-grid against the grid 70 cents sharp.
     */
    auto offGrid = [](uint8_t note) {
      WtHarness e;
      e.init();
      e.setParam(WtEngine::P_WAVE, 1023);
      e.setParam(WtEngine::P_SWEEP, 0);
      e.setParam(WtEngine::P_CUTOFF, 1023);
      e.setParam(WtEngine::P_EGFILTER, 0);
      e.setParam(WtEngine::P_DETUNE, 0);
      e.setParam(WtEngine::P_SUSTAIN, 100);
      e.noteOn(note, 110);
      auto buf = renderVoice(e, kSR / 2);
      const double f0 = dsp::noteToHz(note);
      double on = 0.0, off = 0.0;
      for (int n = 1; n <= 40; ++n) {
        const double hz = f0 * n;
        if (hz > 0.45 * kSR) break;
        on += energyAt(buf, kSR / 16, 8192, hz);
        off += energyAt(buf, kSR / 16, 8192, hz * 1.0413);
      }
      return static_cast<float>(off / fmax(on, 1e-9));
    };
    const float low = offGrid(36), mid = offGrid(72), high = offGrid(103);
    printf("  off-grid share: %.4f at C2, %.4f at C5, %.4f at G7\n", low, mid, high);
    check(low < 0.25f, "wt stays on the harmonic grid low down");
    check(mid < 0.25f, "wt stays on the harmonic grid in the middle");
    check(high < 0.25f, "wt stays on the harmonic grid at the top of the keyboard");
  }

  printf("\nwt: voices\n");
  {
    WtHarness e;
    e.init();
    for (int n = 0; n < 6; ++n) e.noteOn(static_cast<uint8_t>(48 + n * 2), 100);
    renderVoice(e, kBlock * 8);
    check(e.activeVoices() == 6, "wt 6 held notes light up 6 voices");

    for (int n = 0; n < 4; ++n) e.noteOn(static_cast<uint8_t>(72 + n), 100);
    auto stolen = renderVoice(e, kSR / 4);
    check(analyse(stolen).finite, "wt over-allocating stays finite");
    check(e.activeVoices() == 6, "wt voice count stays capped at 6");

    e.allNoteOff();
    renderVoice(e, kSR * 2);
    check(e.activeVoices() == 0, "wt returns every voice to the pool");
  }

  {
    WtHarness e;
    e.init();
    auto buf = renderVoice(e, kSR / 10);
    check(analyse(buf).peak == 0.f, "wt is silent with no notes held");
  }
}

void renderWtDemo() {
  printf("\nwt demo\n");
  const uint8_t chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};
  const int32_t sweeps[4] = {0, 60, -80, 100};

  std::vector<float> mono;
  for (int s = 0; s < 4; ++s) {
    WtHarness e;
    e.init();
    e.setParam(WtEngine::P_WAVE, s == 2 ? 900 : 120);
    e.setParam(WtEngine::P_SWEEP, sweeps[s]);
    e.setParam(WtEngine::P_ATTACK, s == 3 ? 300 : 8);
    e.setParam(WtEngine::P_DECAY, 1200);
    e.setParam(WtEngine::P_SUSTAIN, 60);
    for (int c = 0; c < 2; ++c) {
      for (int n = 0; n < 3; ++n) e.noteOn(chords[c][n], static_cast<uint8_t>(105 - n * 10));
      auto held = renderVoice(e, kSR * 3 / 4);
      mono.insert(mono.end(), held.begin(), held.end());
      for (int n = 0; n < 3; ++n) e.noteOff(chords[c][n]);
      auto gap = renderVoice(e, kSR / 2);
      mono.insert(mono.end(), gap.begin(), gap.end());
    }
  }
  const Stats st = analyse(mono);
  printf("  wt demo: peak %.3f  rms %.3f\n", st.peak, st.rms);
  check(st.finite && st.peak <= 1.0001f, "wt demo stays in range");
  writeWav("dist/test/wt.wav", mono, 1);
}


// ---------------------------------------------------------------------------
// bbd — bucket-brigade delay.

/* Left channel only, deinterleaved. */
static std::vector<float> leftOf(const std::vector<float> &stereo) {
  std::vector<float> l(stereo.size() / 2);
  for (size_t i = 0; i < l.size(); ++i) l[i] = stereo[i * 2];
  return l;
}

/* Where the loudest thing after the input lands, in milliseconds. */
static float firstEchoMs(const std::vector<float> &mono, size_t skip) {
  float peak = 0.f;
  size_t at = 0;
  for (size_t i = skip; i < mono.size(); ++i)
    if (fabsf(mono[i]) > peak) {
      peak = fabsf(mono[i]);
      at = i;
    }
  return static_cast<float>(at) * 1000.f / kSR;
}

void testBbd() {
  printf("\nbbd: the line\n");
  std::vector<float> ram(BbdEngine::kBufferSize, 0.f);

  /* MIX 0 has to be a bypass. */
  {
    BbdEngine fx;
    fx.init(ram.data());
    fx.setParam(BbdEngine::P_MIX, 0);
    auto in = toStereo(sineBurst(440.0, kSR / 4, 1000));
    auto out = runFx(fx, in);
    float dev = 0.f;
    for (size_t i = 0; i < in.size(); ++i) dev = fmaxf(dev, fabsf(in[i] - out[i]));
    printf("  dry-path max deviation at MIX 0: %.6f\n", dev);
    check(dev < 0.01f, "bbd MIX 0 passes the input through");
  }

  /* An impulse has to come back when asked. */
  {
    auto impulseAt = [&ram](int32_t time) {
      BbdEngine fx;
      fx.init(ram.data());
      fx.setParam(BbdEngine::P_TIME, time);
      fx.setParam(BbdEngine::P_FEEDBACK, 0);
      fx.setParam(BbdEngine::P_MIX, 100);
      fx.setParam(BbdEngine::P_MOD, 0);
      fx.setParam(BbdEngine::P_AGE, 0);
      fx.setParam(BbdEngine::P_SPREAD, 0);
      /* The time control glides, so let it arrive before asking where the
       * echo is — otherwise the blip is smeared across the whole sweep. */
      std::vector<float> settle(kSR, 0.f);
      runFx(fx, toStereo(settle));
      /* Two seconds: the longest setting is 1.2 s and would not fit in one. */
      std::vector<float> mono(kSR * 2, 0.f);
      for (int i = 0; i < 64; ++i) mono[i] = 0.8f;  // a short blip, not a click
      auto out = runFx(fx, toStereo(mono));
      /* Skip only past the blip itself: the shortest setting puts its
       * echo at 20 ms, which is inside a longer skip. */
      return firstEchoMs(leftOf(out), 200);
    };
    const float shortMs = impulseAt(0), longMs = impulseAt(1023);
    printf("  echo lands at %.1f ms at TIME 0 and %.1f ms at TIME max\n", shortMs, longMs);
    check(shortMs > 15.f && shortMs < 30.f, "bbd short setting delays about 20 ms");
    check(longMs > 1000.f, "bbd long setting delays over a second");
  }

  /*
   * The signature. On a bucket-brigade chain delay time *is* clock rate, and
   * clock rate is bandwidth — so a long setting is darker than a short one
   * with nothing else touched. A digital delay does not do this, and it is the
   * single most identifying thing about the sound.
   */
  {
    auto brightness = [&ram](int32_t time) {
      BbdEngine fx;
      fx.init(ram.data());
      fx.setParam(BbdEngine::P_TIME, time);
      fx.setParam(BbdEngine::P_FEEDBACK, 0);
      fx.setParam(BbdEngine::P_MIX, 100);
      fx.setParam(BbdEngine::P_MOD, 0);
      fx.setParam(BbdEngine::P_AGE, 100);
      fx.setParam(BbdEngine::P_TONE, 100);
      /* Broadband input, so the measurement is of the line and not the source. */
      dsp::Noise ns;
      ns.seed(0x1234567U);
      /* Four seconds of noise, measured at three — well past the longest
       * setting, so both are reading a line that has filled. Kept quiet so
       * the output stage stays inside the linear part of its soft clip. */
      std::vector<float> mono(kSR * 4, 0.f);
      for (size_t i = 0; i < mono.size(); ++i) mono[i] = ns.next() * 0.15f;
      auto out = leftOf(runFx(fx, toStereo(mono)));

      /*
       * Subtract the dry path to leave the line on its own. Measuring the
       * mixed output instead compares two different comb filters — the dry
       * signal and its own delayed copy interfere, with notches every 1/delay
       * Hz — and that swamps the bandwidth difference being looked for.
       * At MIX 100 the dry gain is 0.5 by construction.
       */
      std::vector<float> wetOnly(out.size());
      for (size_t i = 0; i < out.size(); ++i) wetOnly[i] = out[i] - 0.5f * mono[i];

      /* Energy above 4 kHz against energy below it. */
      double hi = 0.0, lo = 0.0;
      for (int k = 0; k < 12; ++k) {
        lo += energyAt(wetOnly, kSR * 3, 8192, 300.0 + k * 300.0);
        hi += energyAt(wetOnly, kSR * 3, 8192, 4200.0 + k * 700.0);
      }
      return static_cast<float>(hi / fmax(lo, 1e-9));
    };
    const float shortB = brightness(120), longB = brightness(1023);
    printf("  high/low energy ratio: %.4f short, %.4f long\n", shortB, longB);
    check(shortB > longB * 2.f, "bbd the line gets darker as the delay gets longer");
  }

  /* AGE 0 is a clean digital delay; AGE 100 is the chain. */
  {
    auto render = [&ram](int32_t age) {
      BbdEngine fx;
      fx.init(ram.data());
      fx.setParam(BbdEngine::P_TIME, 500);
      fx.setParam(BbdEngine::P_FEEDBACK, 40);
      fx.setParam(BbdEngine::P_MIX, 100);
      fx.setParam(BbdEngine::P_MOD, 0);
      fx.setParam(BbdEngine::P_AGE, age);
      return leftOf(runFx(fx, toStereo(sineBurst(700.0, kSR, 2000))));
    };
    auto clean = render(0);
    auto worn = render(100);
    float diff = 0.f;
    for (size_t i = 0; i < clean.size(); ++i) diff = fmaxf(diff, fabsf(clean[i] - worn[i]));
    printf("  AGE 0 vs 100 max sample difference: %.4f\n", diff);
    check(diff > 0.02f, "bbd AGE changes the character of the line");
    check(analyse(worn).finite, "bbd AGE at maximum stays finite");
  }

  /* Repeats, and a runaway that saturates instead of exploding. */
  {
    BbdEngine fx;
    fx.init(ram.data());
    fx.setParam(BbdEngine::P_TIME, 300);
    fx.setParam(BbdEngine::P_FEEDBACK, 100);
    fx.setParam(BbdEngine::P_MIX, 100);
    auto in = toStereo(sineBurst(440.0, kSR / 4, 1000));
    in.resize(kSR * 12, 0.f);
    auto out = runFx(fx, in);
    const Stats st = analyse(out);
    /* Long after the input stopped there must still be something there. */
    std::vector<float> tail(out.end() - kSR * 2, out.end());
    printf("  runaway feedback: peak %.3f, tail rms %.4f\n", st.peak, analyse(tail).rms);
    check(st.finite, "bbd maximum feedback stays finite");
    check(st.peak <= 1.0001f, "bbd maximum feedback stays in range");
    check(analyse(tail).rms > 0.005f, "bbd maximum feedback keeps repeating");
  }

  /* Tempo sync: an eighth note at 120 bpm is 250 ms. */
  {
    BbdEngine fx;
    fx.init(ram.data());
    fx.setTempo(120.f);
    fx.setParam(BbdEngine::P_SYNC, BbdEngine::kSync8);
    fx.setParam(BbdEngine::P_FEEDBACK, 0);
    fx.setParam(BbdEngine::P_MIX, 100);
    fx.setParam(BbdEngine::P_MOD, 0);
    fx.setParam(BbdEngine::P_AGE, 0);
    fx.setParam(BbdEngine::P_SPREAD, 0);
    std::vector<float> settle(kSR, 0.f);
    runFx(fx, toStereo(settle));
    std::vector<float> mono(kSR, 0.f);
    for (int i = 0; i < 64; ++i) mono[i] = 0.8f;
    const float at = firstEchoMs(leftOf(runFx(fx, toStereo(mono))), 200);
    printf("  1/8 at 120 bpm lands at %.1f ms (250 expected)\n", at);
    check(fabsf(at - 250.f) < 15.f, "bbd tempo sync places the echo on the beat");
  }
}

// ---------------------------------------------------------------------------
// reso — tuned string resonators.

void testReso() {
  printf("\nreso: strings\n");
  std::vector<float> ram(ResoEngine::kBufferSize, 0.f);

  {
    ResoEngine fx;
    fx.init(ram.data());
    fx.setParam(ResoEngine::P_MIX, 0);
    auto in = toStereo(sineBurst(440.0, kSR / 4, 1000));
    auto out = runFx(fx, in);
    float dev = 0.f;
    for (size_t i = 0; i < in.size(); ++i) dev = fmaxf(dev, fabsf(in[i] - out[i]));
    printf("  dry-path max deviation at MIX 0: %.6f\n", dev);
    check(dev < 0.01f, "reso MIX 0 passes the input through");
  }

  /*
   * Excited with noise, the strings have to ring at the note they are tuned
   * to — that is the whole claim. Measured on the tail, after the input has
   * stopped, so what is left is the resonator and nothing else.
   */
  {
    auto ringPitch = [&ram](int32_t noteKnob, int32_t strings) {
      ResoEngine fx;
      fx.init(ram.data());
      fx.setParam(ResoEngine::P_NOTE, noteKnob);
      fx.setParam(ResoEngine::P_CHORD, ResoEngine::kUnison);
      fx.setParam(ResoEngine::P_STRINGS, strings);
      fx.setParam(ResoEngine::P_DECAY, 95);
      fx.setParam(ResoEngine::P_DAMP, 90);
      fx.setParam(ResoEngine::P_MIX, 100);
      fx.setParam(ResoEngine::P_SPREAD, 0);
      dsp::Noise ns;
      ns.seed(0x99AA33U);
      std::vector<float> mono(kSR * 3, 0.f);
      for (int i = 0; i < kSR / 8; ++i) mono[i] = ns.next() * 0.5f;
      auto out = leftOf(runFx(fx, toStereo(mono)));
      /* Well past the excitation: only the ring is left. */
      std::vector<float> tail(out.begin() + kSR, out.begin() + kSR * 2);
      return estimateHz(tail, 1000, kSR / 4);
    };
    /* The knob maps 0..1023 onto notes 24..72. */
    const float expectMid = dsp::noteToHz(24.f + 512.f / 1023.f * 48.f);
    const float midHz = ringPitch(512, 1);
    printf("  rings at %.1f Hz, expected %.1f\n", midHz, expectMid);
    check(fabsf(midHz - expectMid) / expectMid < 0.03f, "reso rings at its middle note");
  }

  /* A chord has to put energy on more than one note. */
  {
    ResoEngine fx;
    fx.init(ram.data());
    fx.setParam(ResoEngine::P_NOTE, 512);
    fx.setParam(ResoEngine::P_CHORD, ResoEngine::kMinor7);
    fx.setParam(ResoEngine::P_DECAY, 95);
    fx.setParam(ResoEngine::P_DAMP, 85);
    fx.setParam(ResoEngine::P_MIX, 100);
    dsp::Noise ns;
    ns.seed(0x5150U);
    std::vector<float> mono(kSR * 3, 0.f);
    for (int i = 0; i < kSR / 8; ++i) mono[i] = ns.next() * 0.5f;
    auto out = leftOf(runFx(fx, toStereo(mono)));
    const float root = 24.f + 512.f / 1023.f * 48.f;
    const double e0 = energyAt(out, kSR, 16384, dsp::noteToHz(root));
    const double e3 = energyAt(out, kSR, 16384, dsp::noteToHz(root + 3.f));
    const double e7 = energyAt(out, kSR, 16384, dsp::noteToHz(root + 7.f));
    const double off = energyAt(out, kSR, 16384, dsp::noteToHz(root + 5.f));
    printf("  MIN7 root %.5f, minor third %.5f, fifth %.5f, unplayed fourth %.5f\n", e0,
           e3, e7, off);
    check(e3 > off * 2.0 && e7 > off * 2.0, "reso voices the chord it is asked for");
  }

  /* DECY sets how long they ring. */
  {
    auto tailRms = [&ram](int32_t decay) {
      ResoEngine fx;
      fx.init(ram.data());
      fx.setParam(ResoEngine::P_DECAY, decay);
      fx.setParam(ResoEngine::P_MIX, 100);
      fx.setParam(ResoEngine::P_DAMP, 85);
      dsp::Noise ns;
      ns.seed(0x2468U);
      std::vector<float> mono(kSR * 3, 0.f);
      for (int i = 0; i < kSR / 8; ++i) mono[i] = ns.next() * 0.5f;
      auto out = leftOf(runFx(fx, toStereo(mono)));
      std::vector<float> tail(out.begin() + kSR * 2, out.end());
      return analyse(tail).rms;
    };
    const float shortR = tailRms(0), longR = tailRms(100);
    printf("  tail two seconds on: %.5f at DECY 0, %.5f at DECY 100\n", shortR, longR);
    check(longR > shortR * 5.f, "reso DECY sets how long the strings ring");
  }

  /*
   * A resonator will integrate any offset in its input straight into the loop
   * and then sit on it. Feed it something with a deliberate bias and check
   * nothing accumulates.
   */
  {
    ResoEngine fx;
    fx.init(ram.data());
    fx.setParam(ResoEngine::P_DECAY, 100);
    fx.setParam(ResoEngine::P_MIX, 100);
    std::vector<float> mono(kSR * 4, 0.f);
    for (size_t i = 0; i < mono.size(); ++i)
      mono[i] = 0.3f + 0.2f * sinf(2.f * dsp::kPi * 300.f * i / kSR);
    auto out = leftOf(runFx(fx, toStereo(mono)));
    /*
     * The dry path carries the input's own offset through by design, so
     * measure the strings alone. At MIX 100 the dry gain is 0.5.
     */
    double mean = 0.0;
    const size_t from = out.size() / 2;
    for (size_t i = from; i < out.size(); ++i) mean += out[i] - 0.5f * mono[i];
    mean /= static_cast<double>(out.size() - from);
    printf("  mean of the string path on a biased input: %.5f\n", mean);
    check(fabs(mean) < 0.02, "reso does not accumulate a DC offset");
    check(analyse(out).finite && analyse(out).peak <= 1.0001f,
          "reso stays in range on a biased input");
  }
}

void renderDelayDemos() {
  printf("\ndelay demo renders\n");

  /* poly8 through the bucket-brigade line. */
  {
    PolyEngine e;
    e.init();
    e.setParam(PolyEngine::P_SHAPE, 250);
    e.setParam(PolyEngine::P_CUTOFF, 640);
    e.setParam(PolyEngine::P_DECAY, 260);
    e.setParam(PolyEngine::P_SUSTAIN, 0);
    e.setParam(PolyEngine::P_RELEASE, 200);

    std::vector<float> mono;
    const uint8_t notes[8] = {60, 67, 72, 67, 64, 71, 76, 71};
    for (int i = 0; i < 8; ++i) {
      e.noteOn(notes[i], 100);
      auto seg = renderPoly(e, kSR / 4);
      mono.insert(mono.end(), seg.begin(), seg.end());
      e.noteOff(notes[i]);
    }
    mono.resize(mono.size() + kSR * 4, 0.f);

    std::vector<float> ram(BbdEngine::kBufferSize, 0.f);
    BbdEngine fx;
    fx.init(ram.data());
    fx.setParam(BbdEngine::P_TIME, 420);
    fx.setParam(BbdEngine::P_FEEDBACK, 62);
    fx.setParam(BbdEngine::P_MIX, 45);
    fx.setParam(BbdEngine::P_AGE, 80);
    fx.setParam(BbdEngine::P_MOD, 35);
    fx.setParam(BbdEngine::P_SPREAD, 60);
    auto out = runFx(fx, toStereo(mono));
    const Stats st = analyse(out);
    printf("  poly8 -> bbd: peak %.3f  rms %.3f\n", st.peak, st.rms);
    check(st.finite && st.peak <= 1.0001f, "poly8 into bbd stays in range");
    writeWav("dist/test/poly8_bbd.wav", out, 2);
  }

  /* pluck through the resonator: strings exciting strings. */
  {
    PluckHarness e;
    e.init();
    e.setParam(PluckHarness::P_PRESET, PluckHarness::kBanjo);

    std::vector<float> mono;
    const uint8_t notes[6] = {72, 76, 79, 76, 72, 67};
    for (int i = 0; i < 6; ++i) {
      e.noteOn(notes[i], 105);
      auto seg = renderVoice(e, kSR / 3);
      mono.insert(mono.end(), seg.begin(), seg.end());
      e.noteOff(notes[i]);
    }
    mono.resize(mono.size() + kSR * 4, 0.f);

    std::vector<float> ram(ResoEngine::kBufferSize, 0.f);
    ResoEngine fx;
    fx.init(ram.data());
    fx.setParam(ResoEngine::P_NOTE, 430);
    fx.setParam(ResoEngine::P_CHORD, ResoEngine::kMinor7);
    fx.setParam(ResoEngine::P_DECAY, 88);
    fx.setParam(ResoEngine::P_DAMP, 70);
    fx.setParam(ResoEngine::P_MIX, 60);
    fx.setParam(ResoEngine::P_SPREAD, 85);
    auto out = runFx(fx, toStereo(mono));
    const Stats st = analyse(out);
    printf("  pluck -> reso: peak %.3f  rms %.3f\n", st.peak, st.rms);
    check(st.finite && st.peak <= 1.0001f, "pluck into reso stays in range");
    writeWav("dist/test/pluck_reso.wav", out, 2);
  }
}


// ---------------------------------------------------------------------------
// micro — micro pitch shifting.

void testMicro() {
  printf("\nmicro: detune and width\n");
  std::vector<float> ram(MicroEngine::kBufferSize, 0.f);

  {
    MicroEngine fx;
    fx.init(ram.data());
    fx.setParam(MicroEngine::P_MIX, 0);
    auto in = toStereo(sineBurst(440.0, kSR / 4, 1000));
    auto out = runFx(fx, in);
    float dev = 0.f;
    for (size_t i = 0; i < in.size(); ++i) dev = fmaxf(dev, fabsf(in[i] - out[i]));
    printf("  dry-path max deviation at MIX 0: %.6f\n", dev);
    check(dev < 0.01f, "micro MIX 0 passes the input through");
  }

  /*
   * The claim is two different pitches, one per ear. Feed a steady tone and
   * look for energy either side of it — flat on the left, sharp on the right.
   * A chorus would put the energy back at the source frequency and move it
   * around instead, which is the difference being tested.
   */
  {
    MicroEngine fx;
    fx.init(ram.data());
    fx.setParam(MicroEngine::P_DETUNE, 1023);  // 25 cents in FINE
    fx.setParam(MicroEngine::P_MODE, MicroEngine::kFine);
    fx.setParam(MicroEngine::P_MIX, 100);
    fx.setParam(MicroEngine::P_SPREAD, 100);
    fx.setParam(MicroEngine::P_FEEDBACK, 0);

    std::vector<float> mono(kSR * 3, 0.f);
    for (size_t i = 0; i < mono.size(); ++i)
      mono[i] = 0.35f * sinf(2.f * dsp::kPi * 440.f * i / kSR);
    auto out = runFx(fx, toStereo(mono));

    std::vector<float> l(out.size() / 2), r(out.size() / 2);
    for (size_t i = 0; i < l.size(); ++i) {
      l[i] = out[i * 2];
      r[i] = out[i * 2 + 1];
    }
    /* 25 cents is a ratio of about 1.0145, so 440 becomes 433.7 and 446.4. */
    const double flat = 440.0 * dsp::centsToRatio(-25.f);
    const double sharp = 440.0 * dsp::centsToRatio(25.f);
    const double lFlat = energyAt(l, kSR, 16384, flat);
    const double lSharp = energyAt(l, kSR, 16384, sharp);
    const double rFlat = energyAt(r, kSR, 16384, flat);
    const double rSharp = energyAt(r, kSR, 16384, sharp);
    printf("  left: flat %.5f sharp %.5f   right: flat %.5f sharp %.5f\n", lFlat, lSharp,
           rFlat, rSharp);
    check(lFlat > lSharp * 3.0, "micro pitches the left side down");
    check(rSharp > rFlat * 3.0, "micro pitches the right side up");
  }

  /* SPREAD 0 has to be mono-compatible; SPREAD 100 must not be. */
  {
    auto sideEnergy = [&ram](int32_t spread) {
      MicroEngine fx;
      fx.init(ram.data());
      fx.setParam(MicroEngine::P_DETUNE, 700);
      fx.setParam(MicroEngine::P_MIX, 100);
      fx.setParam(MicroEngine::P_SPREAD, spread);
      std::vector<float> mono(kSR * 2, 0.f);
      for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.35f * sinf(2.f * dsp::kPi * 330.f * i / kSR);
      auto out = runFx(fx, toStereo(mono));
      double side = 0.0;
      const size_t from = out.size() / 4;
      for (size_t i = from; i < out.size() / 2; ++i) {
        const float d = out[i * 2] - out[i * 2 + 1];
        side += static_cast<double>(d) * d;
      }
      return sqrt(side / static_cast<double>(out.size() / 2 - from));
    };
    const double narrow = sideEnergy(0), wide = sideEnergy(100);
    printf("  side-channel rms: %.6f at SPRD 0, %.6f at SPRD 100\n", narrow, wide);
    check(narrow < 1e-5, "micro SPRD 0 collapses to mono");
    check(wide > narrow * 50.0, "micro SPRD 100 opens the image");
  }

  /* The modes have to differ, and none of them may run away. */
  {
    float peaks[MicroEngine::kNumModes];
    for (int m = 0; m < MicroEngine::kNumModes; ++m) {
      MicroEngine fx;
      fx.init(ram.data());
      fx.setParam(MicroEngine::P_MODE, m);
      fx.setParam(MicroEngine::P_MIX, 100);
      fx.setParam(MicroEngine::P_FEEDBACK, 100);
      fx.setParam(MicroEngine::P_DETUNE, 1023);
      auto in = toStereo(sineBurst(220.0, kSR, 2000));
      std::vector<float> pad(kSR * 4 * 2, 0.f);
      in.insert(in.end(), pad.begin(), pad.end());
      auto out = runFx(fx, in);
      const Stats st = analyse(out);
      peaks[m] = st.peak;
      check(st.finite, std::string("micro ") + MicroEngine::modeName(m) + " stays finite");
      check(st.peak <= 1.0001f,
            std::string("micro ") + MicroEngine::modeName(m) + " stays in range");
    }
    printf("  peak at full feedback: FINE %.3f  WIDE %.3f  SLAP %.3f\n", peaks[0],
           peaks[1], peaks[2]);
  }
}

int main() {
  printf("nts1-mkii-lab offline render tests\n");
  testTuning();
  testVoiceLifecycle();
  testRuntimeQuirks();
  testHeadroom();
  testSilence();
  testPresetInstruments();
  testOrgan();
  testShimmer();
  testDrive();
  testCasioTones();
  testCasioTonesDiffer();
  testCasioArticulation();
  testCasioBehaviour();
  testDx();
  testCz();
  testWt();
  testBbd();
  testReso();
  testMicro();
  renderDemo();
  renderPresetDemos();
  renderFxDemos();
  renderCasioDemo();
  renderDxDemo();
  renderCzDemo();
  renderWtDemo();
  renderDelayDemos();
  printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures,
         g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
