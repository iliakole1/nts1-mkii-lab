# FM6 — six-operator FM

Eight presets built the way the 1983 six-operator FM synths were: every
operator is a sine with its own four-stage envelope, and what separates one
sound from another is which operators modulate which, and what their envelopes
do on the way.

The unit appears on the device as **FM6**. It is a six-operator FM oscillator
in that tradition, not a clone of any product, and it carries no patch data
from one.

## Presets

| | Name | What it is |
|---|---|---|
| `BELL` | Tubular bells | Three carrier/modulator pairs at ratios pulled off the harmonic series, so the partials never resolve into a pitch you could hum. The strike is a modulator that is gone in 60 ms. |
| `MRMB` | Marimba | One carrier with a hard 4:1 knock on the attack and a quiet partial a twelfth up. Rosewood: everything is over in half a second. |
| `VIBE` | Vibraphone | The same mallet shape with the knock pulled back, an aluminium-length decay, and the motor — an amplitude LFO, which is what actually makes a vibraphone sound like one. |
| `BASS` | Solid bass | A 1:1 modulator with feedback, which is how you get a reedy saw out of two sines, plus a fast 2:1 click. |
| `BRAS` | Brass | The swell. The carrier is up in 60 ms but its modulator takes 190 ms to arrive, so the note opens *after* it speaks. |
| `STRG` | Strings | Thin on purpose. A carrier and an octave above it, detuned a few cents, with the modulators kept quiet so it never turns brassy. |
| `HPSI` | Harpsichord | Odd modulator ratios put energy up where a quill belongs. Over inside a second. |
| `CLAV` | Clavinet | The grit is the feedback operator, not a high ratio. Shorter and punchier than the harpsichord. |

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `ATK` | 0–1023 | Stretches every operator's attack, up to +2 s. Zero is the preset. |
| B knob | `REL` | 0–1023 | Stretches every operator's release, up to +3 s. |
| | `PRST` | 8 presets | |
| | `TONE` | 0–1023 | Scales every modulator. The single most useful control on an FM voice: 512 is the patch as written, 0 collapses the whole thing to plain sines. |
| | `FDBK` | 0–200 % | Scales the patch's feedback. 50 is what the preset asks for. Most audible on `BASS` and `CLAV`; the other presets have little or none. |
| | `LFO` | 0–200 % | Scales the patch's LFO — the vibraphone motor, the brass shimmer, the string movement. |
| | `VSEN` | 0–100 % | Velocity sensitivity. It reaches the modulators hardest, so digging in gets brighter rather than just louder — the bark. |
| | `VOIC` | 1–6 | Voices in the pool. |
| | `DTUN` | 0–200 % | Scales the per-operator detuning. Beating and shimmer. |

## How the routing works

Rather than a fixed list of 32 algorithms, each patch carries its own topology:
a modulator bitmask per operator, a carrier mask, and one feedback operator.
That is a superset of the classic algorithm set and costs a byte per operator.

The one rule inherited from those machines is that a modulator is always a
higher-numbered operator than what it feeds. That is what lets the render loop
evaluate operators 6 down to 1 in a single pass, with every source already
computed by the time it is read.

## Two things worth knowing

**Modulator envelopes are brightness, not loudness.** A modulator never reaches
the output. Its envelope decides how much the carrier's spectrum opens, so a
bell is a carrier whose modulator dies faster than it does, and brass is a
carrier whose modulator arrives later than it does. Almost every preset here is
that one relationship, set differently.

**The sidebands are capped.** FM produces unbounded harmonics, so a bright
patch high up the keyboard folds them back at 48 kHz. The originals did that
too, and people came to like it. This does not: at control rate each modulator's
depth is clamped so the topmost sideband stays under Nyquist, alongside
per-operator key scaling. It engages only near the top of the keyboard, where
the alternative is not period charm but an audibly wrong note. `make test`
measures it — the same `BELL` patch has 0.65 of its energy off the harmonic
grid at C3 and 0.007 at C8.

## Starting points

- **Straight preset**: everything default.
- **Sine organ**: any preset, `TONE` 0 — every operator collapses to a sine
- **Glass bells**: `BELL`, `TONE` 800, `REL` 600, `DTUN` 140
- **Fretless-ish**: `BASS`, `FDBK` 20, `ATK` 180, `VSEN` 90
- **Fanfare**: `BRAS`, `TONE` 700, `ATK` 300, `LFO` 120
- **Broken music box**: `VIBE`, `TONE` 950, `DTUN` 200
- **Funk clav**: `CLAV`, `FDBK` 120, `VSEN` 100

## Notes

`23.5 KB` of the oscillator slot's ~48 KB budget, at 6 voices × 6 operators.
That is cheaper per voice than the polyBLEP units here: a sine lookup and an
envelope step beat two BLEP evaluations with branches.

The four-stage envelope lives in [`common/opeg.h`](../../common/opeg.h) rather
than in this unit, because it is generally useful and nothing like an ADSR.
