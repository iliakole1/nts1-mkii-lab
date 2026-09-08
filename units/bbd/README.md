# BBD — bucket-brigade delay

A bucket-brigade chip is not a memory. It is a few thousand capacitors in a
row and a two-phase clock that tips the charge from each one into the next.
The signal is never digitised — it is sampled in time but not in amplitude,
and it arrives at the far end having been handed along four thousand times.

Three consequences, and they are the entire sound.

**Delay time is clock rate.** There is no other control: the charge takes
`stages / (2 × clock)` to cross. So asking for a longer delay slows the clock,
and the clock is the sample rate of the line — **the bandwidth falls as the
delay lengthens**. A long setting is dark because it cannot be anything else.
Nothing else in this repo behaves that way, and it is why a digital delay set
dark still doesn't sound like one of these. `make test` measures it: the same
input comes back with five times more high-frequency energy at the short
setting than at the long one, with nothing else touched.

**Every pass loses more.** The anti-alias filter going in and the
reconstruction filter coming out are both gentle and both inside the feedback
path, so each repeat is darker and softer than the last until the tail is a
murmur with no top left in it.

**The compander breathes.** These chains were noisy enough that they were
always run with a compressor in front and an expander behind. Quiet passages
get pushed up into the line and pulled back down on the way out, which drags
the hiss up and down with the music.

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `TIME` | 0–1023 | 20 ms to 1.2 s. Also the bandwidth control, because on this kind of delay those are the same knob. |
| B knob | `FDBK` | 0–100 % | Past about 95 it runs away — gently, into the soft clip, rather than exploding. |
| | `MIX` | 0–100 % | The dry path only drops to 50 % at full wet, so the signal never disappears. |
| | `TONE` | 0–100 % | Darkens on top of whatever the clock already took away. |
| | `MOD` | 0–100 % | Wobbles the clock. Small values thicken; large ones warble. |
| | `AGE` | 0–100 % | How much of the chain to keep. **At 0 this is a clean digital delay** — full bandwidth at any time setting, no compander, no hiss. At 100 it is the chip. |
| | `SYNC` | OFF / 1/4 / 1/8. / 1/8 / 1/8T / 1/16 | Locks `TIME` to the tempo. |
| | `SPRD` | 0–100 % | Offsets the right tap from the left. |
| | `RATE` | 0–100 % | Modulation speed, 0.05 Hz to 6 Hz. |

## Notes

Modelled rather than sampled: the line runs at 48 kHz and the clock rate
appears as the cutoff of the filters around it. What that buys is a delay whose
character tracks its time control the way the real thing does — which is the
part worth having.

The feedback is taken **inside** the compander, still in the compressed domain,
so the loop gain is the feedback control and nothing else. Expanding first and
feeding that back puts the expander's gain inside the loop, and since it sits
well under unity on quiet material the repeats then die out however high the
feedback is set. That bug is why `make test` checks that maximum feedback keeps
repeating.

1.2 s a side is far longer than any real chain managed — those ran out around
400 ms before the noise swallowed everything — but the slot is called DEL FX
and people want a long one. 450 KB of the 3 MB available.

## Starting points

- **Slapback**: `TIME` 180, `FDBK` 15, `MIX` 30, `AGE` 70
- **Dub**: `TIME` 520, `FDBK` 85, `MIX` 45, `AGE` 100, `TONE` 45
- **Clean digital**: `AGE` 0 — full bandwidth at any length
- **Chorus**: `TIME` 0, `FDBK` 20, `MOD` 70, `RATE` 30, `MIX` 50
- **Runaway**: `FDBK` 100 and ride `TIME`
- **On the beat**: `SYNC` 1/8, `FDBK` 60, `SPRD` 80
