# Polyphony on a monophonic runtime

The NTS-1 mkII gives an oscillator unit one mono output and one pitch value.
It also gives it `unit_note_on(note, velo)` and `unit_note_off(note)` for
*every* note event — and the mkII's 18-key multitouch strip plus MIDI in send
those polyphonically. That gap is the whole trick: **the runtime is
monophonic, the note stream is not.** A poly unit ignores the runtime's pitch,
builds its own voices from note events, and sums them into the mono output.

## What you have to bring yourself

| Normally provided by the synth | In a poly unit |
|---|---|
| Pitch, via `context->pitch` | Ignore it. It follows the last note only. Track notes yourself. |
| Amp envelope (hardware EG) | Per-voice envelope inside your unit. The hardware EG is applied to the *sum*. |
| Filter | Per-voice filter inside your unit, if you want per-note movement. |
| Voice allocation | Yours: free voice → quietest released voice → oldest. |

## Two device settings decide whether any of this works

### 1. Global `EG Legato` must be **0 / Off** — this is the big one

With `EG Legato = On`, which is the **factory default**, the runtime treats a
held phrase as one legato note and calls `unit_note_on` **once**. Your unit
never hears the second and third keys, so a poly unit plays monophonically no
matter how correct its voice allocator is. With `EG Legato = Off` you get one
`unit_note_on` per key.

Korg's own paraphonic example states the same requirement — *"Required: Global
Option Legato=0"* — and the behaviour is tracked in logue-sdk issues
[#111](https://github.com/korginc/logue-sdk/issues/111) and
[#102](https://github.com/korginc/logue-sdk/issues/102).

To change it on the device:

1. Power on while holding the **REVERB** button.
2. Turn the **TYPE** knob to `EG Legato`, and the **B** knob to `0`.
3. Press **ARP** to save. The synth restarts.

Note-offs are delivered per key in both modes (fixed in firmware for the mkII),
so a unit can safely receive a note-off for a note it never got a note-on for —
`poly8` ignores those.

### 2. Set the hardware EG to `Open`

This is the one setting that matters on the device. The NTS-1's amp EG types
are `ADSR`, `AHR`, `AR`, `AR Loop` and `Open`. Any type other than `Open`
re-gates the entire chord on each new note — you will hear every held voice
duck and swell together. `Open` holds the VCA wide and lets your per-voice
envelopes through untouched.

Likewise, set the hardware filter wide open (or use it as a global tone
control only). It is after the sum, so it cannot articulate individual notes.

### Also worth knowing

- **Firmware ≥ 1.2** for velocity. Earlier firmware reported every note-on
  velocity as 0 ([#105](https://github.com/korginc/logue-sdk/issues/105)).
  `poly8` therefore treats velocity 0 on an *idle* key as a normal note-on at
  default velocity, and only as a note-off when that key is already sounding.
- **Note `0xFF`** arrives for internal sequencer/arpeggiator gate events when a
  unit provides no explicit gate handler. It carries no pitch — `poly8` falls
  back to the runtime's current note, which `unit.cc` feeds it each block.

## Voice allocation, concretely

`units/poly8/dsp.h` is a working reference. The shape of it:

```cpp
void noteOn(uint8_t note, uint8_t velo) {
  if (velo == 0) { noteOff(note); return; }      // running-status note off
  Voice *v = findVoiceForNote(note);             // same key already down?
  const bool retrigger = (v == nullptr);         // no -> a fresh voice
  if (!v) v = allocate();
  v->note = note;
  v->velocity = ...;
  v->held = true;
  v->age = ++ageCounter_;
  v->start(retrigger);                           // reset phase/filter only if new
}
```

`allocate()` prefers an idle voice, then the quietest voice already in its
release stage, then the oldest voice outright. Stealing the quietest release
tail is what makes fast passages sound clean rather than clicky.

Two details worth copying:

- **Re-pressing a held key must reuse its voice.** Otherwise a repeated note
  eats the whole pool and the sound doubles in level.
- **Note-off matches by note number, not by voice index** — a key that was
  stolen must not release someone else's voice.

## Pitch bend, glide, and the arpeggiator

Because you ignore `context->pitch`, you also opt out of anything the runtime
folds into it: glide, the arpeggiator's transposition, and master tune. Bend
still arrives cleanly through `unit_pitch_bend(uint16_t)` (14-bit, 8192 =
centre), and `poly8` applies it as ±2 semitones across all voices.

If you *want* whatever the runtime is doing to the pitch, derive it as an
offset from the last note-on rather than using it as an absolute:

```cpp
const float runtimePitch = (ctx->pitch >> 8) + (ctx->pitch & 0xFF) * (1.f / 256.f);
const float offsetSemis  = runtimePitch - lastNoteOnNote_;   // glide/arp/bend
```

Add `offsetSemis` to every voice. It is a good effect for arpeggiated chords
and a bad surprise if you forget it is there — hence it is off in `poly8`.

## CPU: how many voices fit

The M7 at 48 kHz is generous. `poly8` runs 8 voices × 2 polyBLEP oscillators,
plus a per-voice TPT state variable filter and ADSR, and updates filter
coefficients once per block rather than per sample. That is the pattern to
follow:

- per **sample**: oscillator, filter, envelope
- per **block**: `tanf` for filter coefficients, `pow2` for pitch, parameter smoothing
- per **event**: anything else

If you need more headroom, drop the second oscillator per voice before you
drop voices — detune is cheaper as a chorus effect after the sum (that is what
`units/ensemble` is for).

The real ceiling is the 48 KB unit size, not the CPU. Wavetables, long
lookup tables, and per-voice delay lines are what will actually stop you, and
oscillator units get no SDRAM.

## Paraphonic as a cheaper option

If per-voice filters are too expensive for what you are building, a paraphonic
design — N oscillators, one shared filter and envelope — costs a fraction and
still plays chords. It is what several existing NTS-1 mkII units do. The
give-away is that new notes join the current envelope stage instead of
starting their own.
