# CASIO — the seven PT-20 tones

Seven oscillators, one per preset on the Casio PT-20: **PIANO**, **ORGAN**,
**VIOLIN**, **FLUTE**, **HORN**, **FANTASY** and **MELLOW**. They are separate
units rather than one unit with a `PRST` knob, so each sits in its own OSC slot
and is one turn of the encoder away instead of buried in the EDIT menu.

They share an engine. [`pt20.h`](pt20.h) holds it, along with all seven voice
specs in one table so they can be read against each other; each unit's `dsp.h`
is three lines that pin one tone.

## What the PT-20 actually is

A divider synth from 1983. One master oscillator is divided down into square
waves, a handful are mixed at fixed levels, and a simple envelope opens and
closes the result. No filter sweep, no resonance, and no velocity — the
keyboard has no touch sensitivity at all. The presets differ only in which
harmonics are in the mix, how the envelope is shaped, and how bright the output
stage is.

So that is the construction here: up to four pulse partials per voice, a fixed
key-tracked lowpass, an ADSR, one global vibrato, and a lo-fi stage standing in
for the original's cheap output.

It is not a sampled emulation, and it departs from the original in three ways
on purpose:

- **It plays chords.** Six voices. The PT-20's melody section is monophonic.
- **The oscillators are band-limited.** A real divider aliases hard above the
  top octave. polyBLEP keeps it usable across the whole keyboard; `LOFI` is
  there if you want the grit back.
- **The knobs move.** Zero on every control is the factory voice.

## The tones

| Unit | Character | Partials | Envelope |
|---|---|---|---|
| `casio-piano` | Struck square, dies under a held key | 1, 2, 3 × | fast attack, 900 ms decay, no sustain |
| `casio-organ` | 8'/4'/2'/1' octaves, instant on and off | 1, 2, 4, 8 × | 3 ms attack, full sustain |
| `casio-violin` | The buzziest — narrow pulses, deep vibrato | 1, 2, 3, 4 × narrow | 95 ms bowed attack |
| `casio-flute` | Nearly the bare fundamental | 1, 2 × | soft attack, dark |
| `casio-horn` | Hollow, midrange, brassy | 1 × narrow, 2, 3 × | 38 ms attack |
| `casio-fantasy` | The famous one — beating partials, long tail | 1, 2.01, 3.02, 4.98 × | 1.8 s decay, 950 ms release |
| `casio-mellow` | Dark and round, gently detuned | 1, 2, 3 × | 28 ms attack |

`FANTASY`'s partials sit deliberately off the harmonic series so they beat
against each other — that is what makes it a chime rather than a tone, and why
it is the preset everyone remembers.

## Parameters

The same nine on every unit. A and B match the other preset instruments in this
repo, and both *add* to the voice rather than replacing it, so zero is the
factory PT-20.

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `ATK` | 0–1023 | Stretches the attack, up to +2 s. |
| B knob | `REL` | 0–1023 | Stretches the release, up to +3 s. |
| | `TONE` | 0–1023 | Trims the voice's fixed cutoff ±2 octaves. 512 is the factory setting. |
| | `LOFI` | 0–100 % | Sample-and-hold plus bit reduction — the cheap converter. 30 is the default; 0 is clean, 100 is broken. |
| | `VIB` | 0–200 % | Scales the voice's own vibrato. 50 is the factory depth, 0 turns it off. |
| | `DTUN` | 0–200 % | Scales the beating between partials. Most audible on `FANTASY` and `MELLOW`. |
| | `VSEN` | 0–100 % | Velocity sensitivity. **Defaults to 0**, because the PT-20 had none. |
| | `VOIC` | 1–6 | Voices in the pool. |
| | `OCT` | −2…+2 | Octave transpose — the PT-20 is a mini keyboard. |

Being poly units, these want the same two device settings `poly8` does: global
`EG Legato` = 0 and amp EG type `Open`. See
[docs/polyphony.md](../../docs/polyphony.md).

## Starting points

- **Straight PT-20**: everything at default. That is the point.
- **Broken thrift-store PT**: `LOFI` 100, `TONE` 300 — on any of the seven
- **Fantasy pad**: `casio-fantasy`, `REL` 700, `DTUN` 140, `VIB` 90
- **Cheap string section**: `casio-violin`, `VIB` 120, `DTUN` 150, `ATK` 260
- **Bass**: `casio-mellow`, `OCT` −2, `TONE` 250, `LOFI` 55
- **Toy chime**: `casio-fantasy`, `OCT` +2, `LOFI` 70, `ATK` 0
- **Clean modern take**: `LOFI` 0, `TONE` 700 — band-limited and nothing like
  the original, which is occasionally what you want

## Notes

Each unit is about 15 KB of the oscillator slot's ~48 KB budget. Seven of them
plus the six other oscillators here is 13 of the mkII's 16 OSC slots, so load
the ones you want rather than all of them.

`make lint` checks that each directory pins the tone it is named after — the
one mistake in this layout that would compile, link, load and silently ship a
second flute.
