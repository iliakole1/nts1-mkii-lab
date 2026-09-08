# ENSEMBLE — stereo chorus

Three LFO-modulated fractional delay taps, mixed with opposite polarity across
the stereo field. That counter-phase trick is what string machines and the
Juno chorus did: the two channels sweep against each other, so the image opens
up instead of just wobbling in the middle.

It exists mostly because [`poly8`](../../osc/poly8) is mono by construction — the
oscillator slot has one output channel. A chord needs width from somewhere.

## Modes

| Mode | Taps | Base delay | What it is |
|---|---|---|---|
| `I` | 1 | 9 ms | One tap. The plain, obvious chorus — closest to a single BBD line. |
| `II` | 2 | 9 ms | Two taps at unrelated LFO rates. Thicker, less periodic. The default. |
| `DUAL` | 3 | 16 ms | Three taps and a longer base delay. Drifts toward the blurry string-machine end. |

The three LFOs run at ratios of 1, 0.87 and 1.31 — deliberately not simple
fractions, so the taps take a long time to line up and the movement never
settles into an obvious pulse.

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `RATE` | 0–1023 | LFO speed, 0.05 Hz to 6 Hz exponentially. Slow settings are the useful ones. |
| B knob | `DPTH` | 0–1023 | Sweep depth — up to 3.5 ms of delay modulation (5 ms in `DUAL`). |
| | `MIX` | 0–100 % | Wet amount. The dry path only drops to 50 % at full wet, so the signal never disappears. |
| | `MODE` | I / II / DUAL | |
| | `SPRD` | 0–100 % | Stereo spread. At 0 both channels sweep together, which is a mono chorus; at 100 they are fully counter-phase. |
| | `TONE` | 0–100 % | Lowpass on the wet path only. Rolling it back keeps the chorus from adding hiss and sizzle on top of a bright source. |

## Notes

The delay lines are 4096 samples each (85 ms at 48 kHz), taken from SDRAM via
`sdram_alloc()` in `unit_init()` — mod effects get 256 KB, so there is room to
spare. Reads are linearly interpolated; the taps move slowly enough that the
interpolation error stays well below the modulation itself.

Both channels write the same mono sum. The stereo image is made entirely on
the read side, by where the taps are, which is why `SPRD` at 0 collapses it
back to mono cleanly rather than leaving a phasey residue.

## Starting points

- **Juno-ish**: `II`, RATE 300, DPTH 520, MIX 60, SPRD 100
- **Wide pad**: `DUAL`, RATE 150, DPTH 700, MIX 75, SPRD 100, TONE 55
- **Subtle thickening**: `I`, RATE 420, DPTH 260, MIX 35, SPRD 70
- **Seasick**: `DUAL`, RATE 700, DPTH 1023, MIX 100
- **Mono doubler**: `II`, SPRD 0, DPTH 400, MIX 50
