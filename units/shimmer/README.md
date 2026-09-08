# SHIMMER — pitch-shifted reverb

A Schroeder/Moorer tank — four damped combs into two allpasses per channel —
with a pitch shifter wrapped around its own output and fed back in. Whatever
enters the tank comes back an octave up, then *that* comes back an octave up,
and the tail climbs away from the source instead of just fading.

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `TIME` | 0–1023 | Tank length, from a short room to a long hall. |
| B knob | `SHMR` | 0–1023 | How much of the shifted tail is fed back. |
| MIX | `MIX` | 0–1023 | Dry/wet. |
| | `DAMP` | 0–100 % | High-frequency damping inside the combs. |
| | `PDLY` | 0–200 ms | Predelay. |
| | `PTCH` | +12 / +7 / +24 / −12 | Shift interval. `+7` gives a stacked-fifths drone; `−12` builds a sub-octave fog. |
| | `LOCT` | 0–100 % | Low cut on the tail, so the shimmer does not turn to mud. |

## Two things that make it work

**One head resamples; the second only hides the splice.** The obvious design —
two heads locked half a window apart, both audible all the time, crossfaded
between — sounds detuned on anything sustained, and it is worth knowing why.
Both heads resample the same source at the same ratio, so they emit the *same*
frequency, and their delays differ by a constant, so their phase difference is
constant too. That is a fixed comb across the whole spectrum, with a notch
every `2·SR/window` Hz. Sustained notes come back shredded into a detuned
scatter.

Here head A sweeps its window alone, and only in the last 12% does head B — one
whole window behind, sitting exactly where A is about to jump back to — fade in
to cover the wrap. Interference exists only during that splice.

Ratios are exact, because the shifted signal is fed back and shifted again:
five cents of error on the first pass is ten on the second, and by the third
the tail is audibly sour.

The signal entering the shifter is band-limited first. Reading a delay line at
2x speed doubles every frequency in it, so anything above a quarter of the
sample rate returns as aliasing — inharmonic, and exactly the kind of scatter a
shimmer must not have.

**Regeneration is capped, and falls as `TIME` rises.** A comb tank amplifies
anything injected into it by roughly 1/(1−feedback), so shimmer feedback and
comb feedback multiply. Left unchecked, a long tank at full depth stops being
a reverb and becomes a drone that never ends. The soft clip in the loop is a
backstop, not the plan.

Even at maximum `TIME` and `SHMR` the tail is long but finite — measured in
`make test`, which also checks each interval lands on pitch: the shifted tail
must hold at least 8x more energy on the note than a quarter tone either side
of it.

## Starting points

- **Vocal-ish halo**: TIME 780, SHMR 560, MIX 430, DAMP 45, PDLY 30, LOCT 30
- **Ambient wash**: TIME 950, SHMR 800, MIX 700, LOCT 45
- **Just a reverb**: SHMR 0 — the pitch path drops out and the tank stands alone
- **Sub fog**: `PTCH` −12, SHMR 600, DAMP 70
