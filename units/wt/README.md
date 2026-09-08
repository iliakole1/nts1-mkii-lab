# WAVE — wavetable scanning

A bank of 32 single-cycle waves laid out as a progression, and a pointer that
moves through it while the note sounds. Sweeping that pointer is not a filter
opening and not a modulator arriving — it is the waveform itself being
replaced, continuously, which is why it sounds like nothing else here.

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `WAVE` | 0–1023 | Where in the bank to read. This is the instrument. |
| B knob | `CUTF` | 0–1023 | Filter cutoff, 20 Hz to ~18 kHz. |
| | `SWEP` | −100…100 % | Envelope onto the pointer. Positive walks it up the bank as the note swells and back down as it falls; negative goes the other way. The single most useful control after `WAVE`. |
| | `ATK` | 0–2000 ms | |
| | `DEC` | 1–4000 ms | |
| | `SUS` | 0–100 % | At 100 % the envelope stops moving once the attack lands, and so does the pointer. Drop it below 100 to hear `SWEP` travel. |
| | `REL` | 1–4000 ms | |
| | `EGFL` | −100…100 % | Envelope onto the filter, up to ±6 octaves. |
| | `DTUN` | 0–100 cents | Between the two oscillators of a voice. |
| | `VOIC` | 1–6 | |

## The bank

Four zones of eight waves, each morphing into the next, so travelling the bank
is a continuous change of timbre rather than a series of jumps:

| Waves | |
|---|---|
| 0–7 | Sine opening into a sawtooth — the fundamental stays, everything above it fades up |
| 8–15 | Sawtooth closing into a square — the even harmonics fade out |
| 16–23 | Square narrowing into a pulse |
| 24–31 | A resonant bump climbing the harmonic series over a thin sawtooth floor: the vocal end |

The bank is **generated at load, not shipped**. Oscillators here get no SDRAM
and about 48 KB for everything, so a stored bank is the one thing that will not
fit — but a bank is only a list of harmonic recipes, and those cost nothing to
write down.

## Aliasing, and why the table is 8-bit

Aliasing is the whole difficulty with wavetables. There is no depth to clamp
the way [FM](../dx) or [phase distortion](../cz) allows, because every harmonic
is already sitting in the table. The only move is to keep several copies of
each wave and let the note pick one.

So each wave is built four times — band-limited to 32, 16, 8 and 4 harmonics —
and every block picks the widest copy that still fits under Nyquist. Four
copies of 32 waves at 128 points is 16 KB, which is why they are stored as
8-bit: the machines this borrows from were too, and here it is the difference
between fitting in `.bss` and not fitting at all.

`make test` measures the result: the same brightest wave puts 0.16 of its
energy off the harmonic grid at C2, 0.02 at C5 and 0.004 at G7 — the higher the
note, the narrower the copy it is reading.

## Starting points

- **Classic sweep**: `WAVE` 0, `SWEP` 100, `SUS` 30, `DEC` 2000
- **Reverse sweep**: `WAVE` 900, `SWEP` −90, `SUS` 20
- **Vocal pad**: `WAVE` 850, `SWEP` 20, `ATK` 400, `REL` 900, `DTUN` 20
- **Digital bass**: `WAVE` 300, `SWEP` −60, `SUS` 0, `DEC` 400, `CUTF` 600
- **Static sawtooth**: `WAVE` 220, `SWEP` 0 — the bank read as an ordinary synth
- **Hard sync-ish**: `WAVE` 1023, `SWEP` −100, `DEC` 250, `SUS` 0

## Notes

16 KB of program in the oscillator slot's ~48 KB budget, plus 16 KB of `.bss`
for the bank — which costs nothing in the shipped file. The bank comes from the
caller rather than living inside the engine for exactly that reason; held as a
member it would be emitted as initialised `.data` and spend a third of the
budget on zeros. See [docs/platform.md](../../docs/platform.md) and
[`units/pluck`](../pluck), which learned it the hard way.
