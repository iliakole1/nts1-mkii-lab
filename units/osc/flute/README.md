# FLUTE — five breath-blown presets

| Preset | What it is |
|---|---|
| `FLUT` | Concert flute. Balanced harmonics, moderate air, gentle vibrato. |
| `OCAR` | Ocarina: nearly a pure sine with a fat second harmonic, almost no air, slow shallow vibrato. |
| `PANF` | Pan flute — hollow third harmonic, lots of breath, a strong chiff. |
| `RECD` | Recorder. Odd-harmonic lean and a quick attack. |
| `PICC` | Piccolo — **sounds an octave above the note you play**, which is what a piccolo is. Brightest, fastest and deepest vibrato. |

A flute is close to a sine with a little harmonic colour and a lot of air. What
sells it is not the waveform — it is the chiff at the start of the note, the
steady breath underneath, and vibrato that **arrives after the attack**. Players
do not vibrate a note until it has landed, so every note here re-earns its
vibrato over a preset-defined onset (320 ms on the piccolo, 550 on the ocarina).

The breath is filtered noise tracking the note, so it stays in the right
register as you play up the keyboard.

All presets sustain while the key is held.

## Parameters

Preset-first: the A knob picks the instrument, the B knob is the one continuous
character control, and the EDIT menu holds three things you set once.

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `ATK` | 0–1023 | Adds to the preset's attack, up to +2 s. At zero the preset is untouched. |
| B knob | `REL` | 0–1023 | Adds to the preset's release, up to +3 s. At zero the preset is untouched. |
| | `PRST` | preset names | The instrument, in the EDIT menu where the display renders names. |
| | `TONE` | 0–1023 | Body against air — down is pure and round, up is bright and windy. |
| | `LOFI` | 0–100 % | The converter: sample-and-hold rate reduction plus bit-depth quantisation, from ~15 bit at full rate down to ~5 bit at an eighth. The crunchy DAC of a cheap PCM keyboard. |
| | `VSEN` | 0–100 % | Velocity sensitivity — level and brightness together. |
| | `VOIC` | 1–6 | Voice count. |

The NTS-1's shape LFO is wired to `TONE`, so the LFO section sweeps brightness.

## On the device

Global `EG Legato` = **0** and amp EG type = `Open`, same as any poly unit here
— see [../../docs/polyphony.md](../../../docs/polyphony.md).
