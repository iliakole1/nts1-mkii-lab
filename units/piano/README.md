# PIANO — five struck-string presets

| Preset | What it is |
|---|---|
| `GRND` | Straight grand. |
| `BRIT` | Bright grand: strong upper partials that also *last* — they decay nearly as slowly as the fundamental. |
| `MELO` | Mellow. Steep roll-off and highs that vanish quickly, leaving a round, woody note. |
| `HONK` | Honky-tonk. Heavy unison beating and a stiffer string, short and jangly. |
| `TOY` | Toy piano — inharmonicity cranked until the partials spread into a clang, over in a fifth of a second. |

Additive, not FM. FM makes a decent electric piano (see [rhodes](../rhodes)) and
a poor acoustic one: its partials sit at integer ratios and all decay together,
which is exactly what an acoustic piano does not do. Three properties do the
work here:

**Inharmonicity.** Real strings are stiff, so partial *n* sits at
`n·f0·sqrt(1 + B·n²)`, not `n·f0`. B is small for a long grand string and large
for a short one — push it far enough and you get the toy piano. The formula is
normalised so the *fundamental* stays exactly on pitch and only the partials
above it stretch; without that the whole instrument goes sharp.

**Per-partial decay.** High partials die away much faster than the fundamental.
This is most of what makes a note sound *struck*: bright for a moment, then a
settling hum. A single decay rate for everything sounds like an organ with an
envelope on it. The gap between `BRIT` and `MELO` is almost entirely this.

**Unison beat.** Every note on a real piano has two or three strings, never
quite in tune. A slow LFO pushing partial groups against each other stands in
for that; turned up, it is the honky-tonk.

Higher notes decay faster and are stiffer, the way shorter strings are.

## Parameters

The knobs are the envelope — `ATK` and `REL`, mirroring the hardware EG layout
that `Open` mode disables — and everything else lives in the EDIT menu (hold
**OSC**, turn the encoder), where the display can render preset names.

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `ATK` | 0–1023 | Adds to the preset's attack, up to +2 s. At zero the preset is untouched. |
| B knob | `REL` | 0–1023 | Adds to the preset's release, up to +3 s. At zero the preset is untouched. |
| | `PRST` | preset names | The piano, in the EDIT menu where the display renders names. |
| | `TONE` | 0–1023 | Tilts the partial balance: dark and woody, or open and bright. |
| | `LOFI` | 0–100 % | Converter crunch: rate reduction plus bit quantisation. |
| | `VSEN` | 0–100 % | Velocity sensitivity — level and brightness together. |
| | `VOIC` | 1–6 | Voice count. |

**Why the knobs are ATK and REL:** the hardware's own Attack and Release live on
the A and B knobs of the EG section — and do nothing while the amp EG type is
`Open`, which is what a poly unit needs. These put them back. `PRST` sits in the
EDIT menu (hold **OSC**, turn the encoder) because that is where the mkII's
display renders string names; on the A/B knobs it would show a bare number.

## On the device

Global `EG Legato` = **0** and amp EG type = `Open`, same as any poly unit here
— see [../../docs/polyphony.md](../../docs/polyphony.md).
