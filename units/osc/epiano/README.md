# EPIANO — four electric pianos

| Preset | What it is |
|---|---|
| `RHDS` | Ballad Rhodes: soft tine, long decay, gentle tremolo. The unit boots on this. |
| `WURL` | Wurlitzer — hollow reed, faster tremolo, more preamp. |
| `DYNO` | The bright, hi-fi tine sound: hard attack ping, tight tremolo, driven. |
| `BARK` | A reed pushed hard. Highest velocity sensitivity here, so it barks when you dig in and stays sweet when you do not. |

2-op FM, which is the right tool here for exactly the reason it is the wrong one
for an acoustic piano (see [piano](../piano)): a tine or a reed really does
produce a near-harmonic spectrum whose brightness collapses in the first
fraction of a second.

- **Tine-type** (`RHDS`, `DYNO`) — modulator at 1x for a full body, plus a short
  12x ping on the attack: the metallic strike of tine against pickup.
- **Reed-type** (`WURL`, `BARK`) — modulator at 2x, which puts the sidebands on
  odd harmonics: the hollow, square-ish Wurlitzer bark.

The modulation index has its own fast envelope *and* scales with velocity. That
is the bark when you dig in, and it is most of what makes the instrument feel
played rather than triggered. Higher notes decay faster, as shorter tines do.

Tremolo and preamp drive are global, because on the real instruments they are:
one amp, after all the notes.

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `ATK` | 0–1023 | Adds to the preset's attack, up to +2 s. At zero the preset is untouched. |
| B knob | `REL` | 0–1023 | Adds to the preset's release, up to +3 s. At zero the preset is untouched. |
| | `PRST` | preset names | The instrument, in the EDIT menu where the display renders names. |
| | `TONE` | 0–1023 | Bark: how hard the strike reads, from felt to bright. |
| | `TREM` | 0–100 % | Scales the preset's tremolo depth. |
| | `DRIV` | 0–100 % | Preamp dirt on top of the preset's own. |
| | `VOIC` | 1–8 | Voice count. |

## On the device

Global `EG Legato` = **0** and amp EG type = `Open` — see
[../../docs/polyphony.md](../../../docs/polyphony.md). `TINE` through
[`ensemble`](../../modfx/ensemble) is the Rhodes cliché for good reason.
