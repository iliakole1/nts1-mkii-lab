# DRIVE — four-flavour distortion

| Mode | What it is |
|---|---|
| `SOFT` | Smooth asymmetric saturation. Amp-like: compresses before it tears. |
| `FUZZ` | Hard clipping with a DC bias, so the two halves clip unevenly — gated and spitty. |
| `FOLD` | Wavefolder. Past the threshold the signal turns back on itself and keeps generating new harmonics instead of flattening out. |
| `CRSH` | Sample-and-hold plus bit quantisation. No oversampling here: the aliasing *is* the effect. |

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `DRIV` | 0–1023 | Gain into the shaper, up to about 40×. For `CRSH` it sets rate and bit reduction instead. |
| B knob | `TONE` | 0–1023 | Post-shaper lowpass. Distortion without a tone control is just noise. |
| | `MODE` | SOFT / FUZZ / FOLD / CRSH | |
| | `MIX` | 0–100 % | Parallel blend. Low settings give a clean signal with grit underneath. |
| | `LVL` | 0–100 % | Output level. |
| | `BIAS` | 0–100 % | DC offset into the shaper — pushes the waveform off-centre so even harmonics appear. Most audible on `FUZZ` and `FOLD`. |

## Oversampling

The three analogue-ish modes run at 2× internally. Clipping generates harmonics
far above Nyquist, and without oversampling those fold back as inharmonic
whistles that turn chords to mush — worst on high notes, where the 5th and 7th
harmonics are already past the limit.

Upsampling is linear interpolation; decimation is a 7-tap halfband FIR whose
odd taps are zero, so it costs five multiplies per output sample. Measured in
`make test`: a 7 kHz tone driven hard puts its fold-back product 51 dB below
the fundamental.

`CRSH` deliberately skips all of that.

## Starting points

- **Amp grit**: `SOFT`, DRIV 500, TONE 700, MIX 100, LVL 75
- **Fuzz box**: `FUZZ`, DRIV 850, TONE 550, BIAS 45
- **West coast**: `FOLD`, DRIV 640, TONE 800 — sweep DRIV and listen to the harmonics march
- **Parallel dirt**: any mode, MIX 35 — keeps the low end intact
- **Broken sampler**: `CRSH`, DRIV 700, TONE 900
