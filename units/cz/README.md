# PD8 — phase distortion

Eight presets built the way the Casio CZ line worked in 1984: make a resonant
filter sweep on a synth that has no filter in it.

A phase accumulator runs at the note frequency, a distortion function warps it,
and the warped phase reads a cosine. Warp it so the cycle rushes through its
first half and crawls through the second and the cosine comes out shaped like a
sawtooth. Hold it flat in two places and you get a square. Then sweep the amount
of warping with an envelope — the **DCW** — and the spectrum opens and closes
exactly the way a lowpass with an envelope on cutoff would.

It appears on the device as **PD8**. It's a phase-distortion oscillator in that
tradition, not a clone of a product.

## Presets

| | What it is |
|---|---|
| `RESO` | The one the machine is remembered for. A resonant sawtooth whose peak climbs on the attack and falls back. |
| `BRAS` | Resonant trapezoid — more body under the peak than the saw — plus the swell: the sweep arrives after the level. |
| `EPNO` | The glassy one. A narrow pulse whose distortion collapses fast, so the strike is bright and the tail is nearly a sine. |
| `BASS` | The rubbery one. A resonant saw kept low in its sweep, with a square an octave down for weight. |
| `BELL` | Two lines at an inharmonic interval, the distortion dying faster than the level so the strike is the bright part. |
| `STRG` | Resonant triangle held low and wide, slow at both ends, the two lines detuned far enough to shimmer without beating into a null. |
| `PIPE` | Squares an octave apart, distortion nearly static. Flat as a drawbar: on and off, no movement. |
| `VOX` | A resonant trapezoid parked high with almost no sweep, which puts a fixed peak in the formant range and reads as a vowel. |

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `ATK` | 0–1023 | Stretches the attack, up to +2 s. Zero is the preset. |
| B knob | `REL` | 0–1023 | Stretches the release, up to +3 s. |
| | `PRST` | 8 presets | |
| | `DCW` | 0–1023 | Depth of the distortion sweep. **This is the filter knob on a synth with no filter**: 512 is the patch as written, 0 leaves plain cosines. |
| | `DTUN` | 0–200 % | Scales the detuning between the two lines. |
| | `LFO` | 0–200 % | Scales the patch's LFO, which moves pitch and sweep depth. |
| | `VSEN` | 0–100 % | Velocity sensitivity. It reaches the *sweep*, so digging in gets brighter rather than only louder. |
| | `VOIC` | 1–8 | Voices in the pool. |
| | `OCT` | −2…+2 | Octave transpose. |

## The eight shapes

Five morph out of a plain cosine as the sweep opens:

- **Saw** — the half-cycle point slides down, so the first half of the cosine
  happens in a shrinking slice and the rest stretches out
- **Square** — two flat regions with edges that sharpen
- **Pulse** — the same, with the transitions pulled together so the high part
  narrows and even harmonics come in
- **Double sine** — the phase runs faster than the period, growing a second
  cycle inside the first
- **Saw-pulse** — a saw with its tail windowed away

Three are the signature, and are not warped cosines at all but a window times a
harmonic:

```
out = window(p) * cos(2*pi * n * p)
```

with `n` swept by the DCW envelope, which reads as a resonant peak climbing the
spectrum. The **resonant saw**, **triangle** and **trapezoid** differ only in
their window. All three windows fall to zero at the end of the cycle, so a
fractional `n` never leaves a step at the wrap — the buzz is harmonics, not a
discontinuity.

`n` is capped so the peak stays under Nyquist. Without that the sweep runs off
the top of the band at the top of the keyboard and folds back down through the
note it came from. `make test` measures it: the same `RESO` patch puts 0.19 of
its energy off the harmonic grid at C2 and 0.009 at G7.

## Why it fits this hardware

It is the cheapest oscillator in the repo. A warp is a couple of compares and a
multiply, then one table read — less work than a single polyBLEP pulse, and
nothing like the cost of six FM operators. There is no filter to run and no
wavetable to store, which matters because oscillators here get no SDRAM at all.

That is what buys 8 voices with two lines each.

## Starting points

- **Straight preset**: everything default.
- **Cosine organ**: any preset, `DCW` 0 — every shape collapses to a cosine
- **Screaming sweep**: `RESO`, `DCW` 1023, `ATK` 400, `VSEN` 100
- **Rubber bass**: `BASS`, `OCT` −1, `DCW` 700
- **Choir**: `VOX`, `ATK` 350, `REL` 500, `LFO` 120
- **Music box**: `BELL`, `OCT` +1, `DCW` 850, `DTUN` 160
- **Cheap string machine**: `STRG`, `DTUN` 200, `LFO` 140

## Notes

22.5 KB of the oscillator slot's ~48 KB budget, at 8 voices × 2 lines. The
four-stage envelopes come from [`common/opeg.h`](../../common/opeg.h), shared
with [`dx`](../dx) — every line has two of them, one for level and one for the
sweep.
