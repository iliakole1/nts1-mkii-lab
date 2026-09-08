# PLUCK — six string presets, Karplus-Strong

| Preset | What it is |
|---|---|
| `BANJ` | Banjo: bright pick near the bridge, short ring, resonant small body. |
| `GTAR` | Guitar. Softer pick further along the string, long decay, low body around 100 Hz. |
| `UKUL` | Ukulele: nylon, so a soft dark pick, over a small body resonating up at 450 Hz. |
| `MAND` | Mandolin — the brightest pick here, picked hard by the bridge, tight little body, over in 400 ms. |
| `HARP` | Harp. Bright but soft-picked, 7 s ring, long release. |
| `KOTO` | Koto: picked right at the bridge, nasal, with a strong body. |

Each note fills a delay line with a burst of filtered noise, then circulates it
through a one-pole lowpass. That filter *is* the string: how much high end it
keeps sets both the tone and how long the note rings, because the loop loses a
little more energy on every pass.

Three things separate a banjo from a guitar here:

- **Pick brightness** — how much high end is in the burst. A fingernail on steel
  against a thumb on nylon. Playing harder brightens it, on every preset.
- **Pick position** — the burst has a delayed copy of itself subtracted, which
  notches out the harmonic whose wavelength matches the delay. Picking near the
  bridge is thin and nasal; over the sound hole is round.
- **Body** — two resonant bandpasses across the voice mix. Instrument size lives
  here: a ukulele body resonates near 400 Hz, a guitar near 100.

The loop filter's own delay is subtracted from the string length. Without that
correction strings ring flat, increasingly so as you play up the keyboard.

**Range**: the delay lines are 1024 samples, so tuning is exact down to about
47 Hz — below a guitar's low E and below every other preset. Notes under that
clamp.

## Parameters

Preset-first: the A knob picks the instrument, the B knob is the one continuous
character control, and the EDIT menu holds three things you set once.

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `ATK` | 0–1023 | Adds to the preset's attack, up to +2 s. At zero the preset is untouched. |
| B knob | `REL` | 0–1023 | Adds to the preset's release, up to +3 s. At zero the preset is untouched. |
| | `PRST` | preset names | The instrument, in the EDIT menu where the display renders names. |
| | `TONE` | 0–1023 | String brightness. Opens the loop filter, which makes notes brighter *and* longer. |
| | `LOFI` | 0–100 % | The converter: sample-and-hold rate reduction plus bit-depth quantisation, from ~15 bit at full rate down to ~5 bit at an eighth. The crunchy DAC of a cheap PCM keyboard. |
| | `VSEN` | 0–100 % | Velocity sensitivity — level and brightness together. |
| | `VOIC` | 1–6 | Voice count. |

The NTS-1's shape LFO is wired to `TONE`, so the LFO section sweeps brightness.

## On the device

Global `EG Legato` = **0** and amp EG type = `Open`, same as any poly unit here
— see [../../docs/polyphony.md](../../../docs/polyphony.md).
