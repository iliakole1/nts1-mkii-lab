# POLY8 — 8-voice polyphonic synth

The NTS-1 mkII's oscillator slot is monophonic. Its *note callbacks* are not:
`unit_note_on` fires for every key, including simultaneous ones. So a unit can
keep its own voices, run its own envelopes and filters, and sum a chord into
the single output channel the runtime expects.

That is all `poly8` is. Eight voices, each two detuned polyBLEP oscillators
through a state-variable lowpass and an ADSR, mixed down and soft-clipped.

**It needs two device settings to work at all** — `EG Legato = 0` and amp EG
type `Open`. Without them you get one note, or a chord that swells as one lump.
See [docs/polyphony.md](../../../docs/polyphony.md); the short version is in the
main [README](../../../README.md#two-device-settings-poly8-needs).

## Parameters

| | Name | Range | What it does |
|---|---|---|---|
| A knob | `SHPE` | 0–1023 | Waveshape morph, continuous: saw at 0, square at the midpoint, 5 % pulse at full. The hardware shape LFO adds to this. |
| B knob | `CUTF` | 0–1023 | Filter cutoff, 20 Hz to ~18 kHz exponentially. |
| | `DTUN` | 0–100 cents | Spread between a voice's two oscillators. 0 is dead clean; 8 is the default drift; past 30 it turns into a chorus. |
| | `VOIC` | 1–8 | Voices in the pool. Lower it to force stealing on purpose. |
| | `ATK` | 0–2000 ms | Envelope attack. |
| | `DEC` | 1–4000 ms | Envelope decay. |
| | `SUS` | 0–100 % | Sustain level. |
| | `REL` | 1–4000 ms | Envelope release. |
| | `RESO` | 0–100 % | Filter resonance. |
| | `EGFL` | −100…100 % | How much the envelope moves the filter, up to ±6 octaves. Negative closes the filter as the note swells, which is how you get a reverse-ish sweep. |

Unlike the preset instruments, `poly8` has no `PRST` — the ten controls *are*
the instrument.

## How the voices work

`dsp::VoicePool` in `common/` does the bookkeeping. A note-on looks for a voice
already holding that key, and reuses it if it finds one — so a repeated key
does not stack. Otherwise it allocates: an idle voice first, then the quietest,
then the oldest. Stealing is by envelope level rather than age alone, so the
voice that disappears is the one you were least likely to hear.

Two details that matter more than they look:

- **Voice phases are staggered** (`index * 0.11`). Eight voices starting at
  phase zero produce an audible click on the first chord and an unnaturally
  coherent attack.
- **The two oscillators in a voice are offset by 0.13, not 0.5.** Two antiphase
  saws at the same frequency cancel their fundamental. At `DTUN = 0` that guts
  the sound — the detune is what normally hides it, and a half-cycle offset
  makes it worst exactly where the unit should be cleanest.

Filter cutoff tracks the key (0.35 octaves per octave) so high notes do not
disappear, and the envelope modulates it per voice — the thing the hardware
filter cannot do, because it sits after the slot and sees only the sum.

## Starting points

- **Poly brass**: SHPE 0, CUTF 500, RESO 40, EGFL 60, ATK 60, DEC 800, SUS 60
- **String pad**: SHPE 180, CUTF 620, DTUN 22, ATK 400, REL 900, EGFL 15
- **Clav / plucky**: SHPE 900, CUTF 380, RESO 55, ATK 0, DEC 180, SUS 0, EGFL 75
- **Hollow square lead**: SHPE 512, VOIC 1, CUTF 700, RESO 20, DTUN 6
- **Reverse sweep**: EGFL −80, ATK 300, CUTF 850, RESO 60

`poly8` is mono by construction — the slot has one output channel. Put
[`ensemble`](../../modfx/ensemble) after it for a stereo image.
