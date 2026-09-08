# ORGAN — six drawbar presets

| Preset | Registration and character |
|---|---|
| `JAZZ` | 16', 5 1/3', 8' — the classic three-drawbar jazz setting, with percussion on the 3rd and a touch of vibrato. |
| `ROCK` | Fat and full: everything low pulled out, a firm hand of upper drawbars, and enough amp dirt to bite. |
| `VOX` | Continental. Thin at the bottom, strong 8' and 4', reedy edge. |
| `FARF` | Farfisa — the buzziest thing in the box. Almost no fundamental, upper drawbars wide open, heavy waveshaping. |
| `PIPE` | Church: principal chorus with mixtures, slow attack and release, dead clean, no vibrato. |
| `THTR` | Theatre tibia. Nearly pure 16' + 8', wrapped in a deep tremulant. |

## How it works

Nine drawbars — 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' — summed as
sines, tonewheel style.

The trick that makes it cheap: **the phasor runs at half the note frequency**.
Against that sub-octave every drawbar is an integer multiple — 1, 2, 3, 4, 6, 8,
10, 12, 16 — so all nine come from one accumulator by multiplying its phase. Run
the phasor at the note frequency instead and the 16' and 5 1/3' drawbars need
ratios of 0.5 and 1.5, which click audibly every time the phase wraps.

Voices skip drawbars set to zero, so the three-drawbar jazz registration costs
about a third of a full one.

**Percussion is single-trigger**, like a Hammond's: it fires on the first key of
a phrase and stays silent for notes added while something is still held. Lift
everything and it re-arms. That behaviour is half of why organ players phrase
the way they do, and it is tested.

The key click is scaled against the preamp gain — it is a mechanical noise
*ahead* of the amp, and left unscaled a driven preset turns the click into the
loudest thing in the instrument.

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `ATK` | 0–1023 | Adds to the preset's attack, up to +2 s. At zero the preset is untouched. |
| B knob | `REL` | 0–1023 | Adds to the preset's release, up to +3 s. At zero the preset is untouched. |
| | `PRST` | preset names | The organ, in the EDIT menu where the display renders names. |
| | `TONE` | 0–1023 | Pulls the upper drawbars in and out — the gesture an organist makes constantly. |
| | `VIB` | 0–100 % | Scales the preset's vibrato and tremulant. |
| | `PERC` | 0–100 % | Scales the percussion. |
| | `DRIV` | 0–100 % | Amp dirt on top of whatever edge the preset already has. |
| | `VOIC` | 1–8 | Voice count. |

## On the device

Global `EG Legato` = **0** and amp EG type = `Open` — see
[../../docs/polyphony.md](../../docs/polyphony.md). Worth trying through
[`ensemble`](../ensemble) for a rotary-ish wobble, or [`drive`](../drive) for a
cranked amp.
