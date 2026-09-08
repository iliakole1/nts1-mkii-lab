# RESONATOR — tuned strings in the delay slot

A delay line short enough to be a pitch rather than an echo. Feed anything into
a loop 1/f seconds long: the components that fit the loop survive, the ones
that don't cancel themselves out, and what comes back is your input played on a
string.

That is [`pluck`](../../osc/pluck) with the excitation taken from the audio input
instead of a burst of noise — which is the older idea of the two, and the more
useful one in an effects slot.

Four strings, tuned to a chord. A drum loop through a minor seventh comes back
as a chord voiced by its own transients. A pad through a unison becomes a
resonant peak that rings.

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `NOTE` | 0–1023 | Root pitch, two octaves below middle C up to an octave above. |
| B knob | `DECY` | 0–100 % | How long the strings ring. |
| | `MIX` | 0–100 % | |
| | `CHRD` | UNIS / OCT / 5TH / MAJ / MIN / MAJ7 / MIN7 / SUS4 | What the four strings are tuned to. |
| | `DAMP` | 0–100 % | How much top the loop filter keeps. |
| | `STRG` | 1–4 | How many strings. One is a single resonant peak. |
| | `SPRD` | 0–100 % | Fans the strings across the stereo image, low on the left. |
| | `TONE` | 0–100 % | Rolls the top off the excitation, so the strings aren't driven by hiss they'd then ring on forever. |
| | `FINE` | −50…50 cents | Spreads the strings apart for beating. |

## Notes

`DAMP` is the string. The one-pole filter in each loop sets brightness **and**
ring time together, because the loop loses whatever the filter takes on every
pass — that is one control, not two, and pretending otherwise is how these end
up sounding synthetic. `DECY` sets the floor, `DAMP` the slope.

The loop filter also adds a delay of its own, which is subtracted from the line
length before it's used. Skip that and every string rings flat, with the error
growing as the note goes up.

DC is blocked **before** the strings, not after. A resonator fed a biased signal
integrates that bias straight into its loop and then sits on it: the ring is
still there but riding on an offset that eats the headroom the strings needed.
Cleaning it up at the output leaves the loop just as full. `make test` feeds it
a deliberately biased input and checks.

64 KB of the 3 MB available — four lines of 4096 samples, which reaches down to
about 12 Hz, well below the lowest root on offer.

## Starting points

- **Drums into a chord**: `CHRD` MIN7, `NOTE` 430, `DECY` 85, `MIX` 55, `SPRD` 90
- **Single resonant peak**: `STRG` 1, `CHRD` UNIS, `DECY` 95, `DAMP` 95
- **Sympathetic strings**: `CHRD` 5TH, `DECY` 70, `MIX` 35, `FINE` 12
- **Gamelan**: `CHRD` SUS4, `NOTE` 700, `DAMP` 40, `DECY` 60
- **Drone**: `DECY` 100, `MIX` 80, `TONE` 30 — it will sit there
- **Bowed**: `CHRD` OCT, `DECY` 98, `DAMP` 80, feed it something sustained
