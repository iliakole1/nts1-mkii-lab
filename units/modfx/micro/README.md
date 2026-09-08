# MICRO — micro pitch shifting

Two pitch shifters a few cents apart, one per side, with a short delay on each.
That is all it is, and it is the widest-sounding effect there is that doesn't
move.

A chorus makes width by modulating a delay, which you hear as movement — the
sweep is the effect. A micro pitch shifter makes width by putting genuinely
different pitches in the two ears, and the brain reads that as *size* rather
than as motion. Nothing cycles, so there's nothing to get tired of.

Pairs with the mono oscillators here the way [`ensemble`](../ensemble) does,
but without the movement — useful on the ones that already carry their own
vibrato and don't want any more.

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `DTUN` | 0–1023 | How far apart the two sides are pitched, up to 25 cents before the mode scales it. **Five to fifteen is the useful range**; past twenty-five it stops sounding like one instrument and starts sounding like two. |
| B knob | `MIX` | 0–100 % | The dry path only drops to 50 % at full wet. |
| | `DLAY` | 0–1023 | Adds up to 40 ms on top of whatever the mode uses. |
| | `MODE` | FINE / WIDE / SLAP | |
| | `SPRD` | 0–100 % | At 0 both ears get the same sum — a thickener rather than a widener, and mono-compatible, which the fully spread setting is not. |
| | `FDBK` | 0–100 % | Cascades the detune: each pass shifts the already-shifted signal again. A little turns two voices into a smeared cluster. |
| | `TONE` | 0–100 % | Lowpass on the wet path. |

## Modes

| Mode | Interval | Delay | Reads as |
|---|---|---|---|
| `FINE` | as set | 6 / 9 ms | One wide instrument |
| `WIDE` | ×2.4 | 18 / 27 ms | Two players on the same part |
| `SLAP` | ×1.6 | 42 / 62 ms | A double-tracked take |

## Notes

The shifter is [`common/shifter.h`](../../../common/shifter.h), shared with
[`shimmer`](../../revfx/shimmer) — one head resampling, plus a second that appears only
in the last 12 % of each grain to cover the wrap. The header explains why the
obvious two-head crossfade sounds detuned on sustained material.

Because the interval here is tiny, the shifter barely splices at all. Grain
rate is `|ratio − 1| / window`, so at 7 cents and a 43 ms window a splice
happens roughly every ten seconds. That's why this can use a short window and
keep transients intact where `shimmer`, shifting a whole octave, needs a long
one.

64 KB of the 256 KB a mod effect gets.

## Starting points

- **Wide, invisible**: `FINE`, `DTUN` 140, `MIX` 45, `SPRD` 100 — the default
- **Double-tracked**: `SLAP`, `DTUN` 300, `MIX` 50
- **Mono thickener**: `FINE`, `SPRD` 0, `MIX` 60 — survives a mono sum
- **Two players**: `WIDE`, `DTUN` 500, `DLAY` 600
- **Smear**: any mode, `FDBK` 70, `TONE` 55
- **Detune the whole synth**: `MIX` 100, `DTUN` 900
