# nts1-mkii-lab

Twenty-two custom oscillators and effects for the **Korg NTS-1 digital kit
mkII**, built on Korg's [logue SDK](https://github.com/korginc/logue-sdk) v2.

Sixteen polyphonic instruments — a subtractive synth, electric pianos, drawbar
organs, struck strings, blown flutes, plucked strings, six-operator FM, phase
distortion, wavetable scanning and the seven Casio PT-20 presets — plus a
chorus, a micro pitch shifter, a distortion, a bucket-brigade delay, a string
resonator and a pitch-shifted reverb. All free, all GPLv3.

The bet this repo makes: the NTS-1 mkII's oscillator runtime is monophonic, but
its note callbacks are not — so a unit can implement its own voices, envelopes
and filters and play chords. `poly8` does exactly that.

## Units

| Unit | Slot | What it is |
|---|---|---|
| [`poly8`](units/poly8) | OSC | 8-voice polyphonic synth. Two detuned polyBLEP oscillators per voice (saw → square → narrow pulse), per-voice state variable lowpass and ADSR, envelope-to-filter amount, voice stealing. |
| [`epiano`](units/epiano) | OSC | Four electric pianos — ballad Rhodes, Wurlitzer, bright tine, barking reed — from 2-op FM with a velocity-scaled strike. Global tremolo and preamp. |
| [`organ`](units/organ) | OSC | Six drawbar organs: jazz, rock, Vox, Farfisa, church pipe, theatre tibia. Nine drawbars off one phasor, single-trigger percussion, key click, tremulant. |
| [`piano`](units/piano) | OSC | Five struck-string presets — grand, bright, mellow, honky-tonk, toy. Additive, with stiff-string inharmonicity, per-partial decay and unison beating. |
| [`flute`](units/flute) | OSC | Five breath-blown presets: flute, ocarina, pan flute, recorder, piccolo. Chiff, breath noise, and vibrato that arrives after the attack. |
| [`pluck`](units/pluck) | OSC | Six Karplus-Strong string presets: banjo, guitar, ukulele, mandolin, harp, koto. Pick brightness, pick position and body resonance separate them. |
| [`wt`](units/wt) | OSC | Wavetable scanning, shown as `WAVE`. 32 waves generated at load, band-limited four ways so the top of the keyboard does not tear, with the envelope walking the pointer through the bank. |
| [`cz`](units/cz) | OSC | Phase distortion, shown as `PD8`. Eight presets — a resonant filter sweep on a synth with no filter, the way the Casio CZ line did it. Cheapest oscillator here. |
| [`dx`](units/dx) | OSC | Six-operator FM, shown as `FM6`. Eight presets — bells, marimba, vibes, bass, brass, strings, harpsichord, clavinet. Per-patch routing, operator feedback, and sidebands capped so the top of the keyboard does not fold. |
| [`ensemble`](units/ensemble) | MOD FX | Stereo chorus/ensemble. Three LFO-modulated delay taps with counter-phase stereo spread, three modes, wet-path tone control. Pairs with `poly8`, which is mono by construction. |
| [`micro`](units/micro) | MOD FX | Micro pitch shifting. Two shifters a few cents apart, one per side — width with nothing moving, so there is no sweep to get tired of. |
| [`drive`](units/drive) | MOD FX | Distortion: soft saturation, biased fuzz, wavefolder, bitcrusher. The analogue-ish modes run at 2x oversampling with a halfband decimator. |
| [`bbd`](units/bbd) | DEL FX | Bucket-brigade delay. Time is clock rate and clock rate is bandwidth, so a long setting is dark because it cannot be anything else. Compander, tape-ish wobble, tempo sync. |
| [`reso`](units/reso) | DEL FX | Four tuned string resonators — a delay line short enough to be a pitch. Play a drum loop through a minor seventh. |
| [`shimmer`](units/shimmer) | REVERB | Pitch-shifted reverb. A comb/allpass tank with an octave-up shifter in its feedback path, so the tail climbs away from the source. |

### [`casio`](units/casio) — the seven PT-20 tones

The Casio PT-20 (1983) is a divider synth: square waves mixed at fixed levels
with a simple envelope, no filter sweep and no velocity. Its seven presets are
here as seven separate oscillators, so each one is a slot of its own rather
than a menu dive. They share [one engine](units/casio/pt20.h); each unit's
`dsp.h` is three lines pinning a tone.

| Unit | Slot | What it is |
|---|---|---|
| [`casio-piano`](units/casio) | OSC | Struck square that dies under a held key. |
| [`casio-organ`](units/casio) | OSC | 8'/4'/2'/1' octaves off one divider, instant on and off. |
| [`casio-violin`](units/casio) | OSC | The buzziest — narrow pulses and a deep vibrato. |
| [`casio-flute`](units/casio) | OSC | Nearly the bare fundamental, rolled off hard. |
| [`casio-horn`](units/casio) | OSC | Hollow, midrange, brassy. |
| [`casio-fantasy`](units/casio) | OSC | The famous one — partials beating off the harmonic series. |
| [`casio-mellow`](units/casio) | OSC | Dark and round, gently detuned. |

Band-limited and polyphonic, which the original is not; `LOFI` puts the grit
back and `VSEN` defaults to 0 because the PT-20's keyboard has no touch
sensitivity. Full parameter list and starting points in
[units/casio/README.md](units/casio).

## Install

You do not need to build anything. Grab the units from the
[latest release](../../releases/latest), then:

1. Connect the NTS-1 mkII over USB and leave it powered on.
2. Open [KORG KONTROL Editor](https://www.korg.com/us/products/synthesizers/nts_1_mk2/editor.php)
   (free, Mac/Windows).
3. Drag each `.nts1mkiiunit` onto the matching user-unit list — oscillators to
   OSC, `ensemble` and `drive` to MOD FX, `shimmer` to REVERB.

The unit then appears at the end of that list on the device. Don't unplug while
the editor is transferring. Longer version, including what to do when something
refuses to load, in [docs/workflow.md](docs/workflow.md).

If you play chords, read [the two settings `poly8` needs](#two-device-settings-poly8-needs)
first — the factory defaults will make any polyphonic unit sound broken.

## Playing them

### Shared layout on the preset instruments

`epiano`, `organ`, `piano`, `flute` and `pluck` all use the same controls: **A =
`ATK`**, **B = `REL`** — both adding on top of the preset, zero meaning "leave
it alone" — with `PRST` and `TONE` in the EDIT menu (hold **OSC**, turn the
encoder). The knobs mirror the hardware EG's own Attack/Release, which is dead
while the amp EG type is `Open`.

### Two device settings `poly8` needs

1. **Global `EG Legato` = 0 (Off).** The factory default is On, and in that
   mode the runtime sends a *single* note-on for a held phrase — the unit plays
   monophonically however you wrote it. Power on holding **REVERB**, turn
   **TYPE** to `EG Legato`, **B** to `0`, press **ARP** to save.
2. **Amp EG type = `Open`.** The hardware envelope is after the oscillator slot
   and monophonic; anything else re-gates the whole chord on every new note.

Both are explained in [docs/polyphony.md](docs/polyphony.md).

## Build it yourself

```bash
./scripts/setup.sh    # SDK submodule, CMSIS, ARM toolchain
make test             # host-side DSP tests + demo WAVs in dist/test
make                  # cross-build every unit into dist/
```

`setup.sh` pulls about 1.3 GB of toolchain and CMSIS headers. `make test` needs
none of it — it builds the DSP with your system compiler in a couple of
seconds, which is where most of the work happens.

## Layout

```
common/          portable DSP building blocks (BLEP osc, SVF, ADSR, operator EG,
                 pitch shifter, LFO, helpers)
units/<name>/
  dsp.h          the engine — portable C++, zero SDK includes
  osc.h|modfx.h|revfx.h  logue-sdk Processor adapter
  unit.cc        SDK callback surface
  header.c       unit metadata + the 10 parameter descriptors
  config.mk      sources for the build
units/casio/     a family: shared pt20.h engine, one subdirectory per tone
tests/render.cc  offline harness: checks + WAV renders, builds with plain clang
mk/nts1mkii.mk   points the stock SDK build at this repo's layout
logue-sdk/       submodule, kept pristine
docs/            platform deep dive, polyphony notes, workflow
```

A unit is any directory under `units/` with a `config.mk`, at either depth. The
build target flattens the path, so `units/casio/piano` is `make casio-piano`
and cannot collide with the top-level `make piano`.

The split between `dsp.h` (portable) and the SDK glue is the important part: it
is what lets `make test` run the exact code that ships to the synth, on your
laptop, in two seconds.

## Docs

- [docs/platform.md](docs/platform.md) — the hardware, the runtime, memory and
  parameter budgets, and the gotchas that cost time
- [docs/polyphony.md](docs/polyphony.md) — how polyphony works on a
  monophonic runtime, voice allocation, what the hardware still does to you
- [docs/workflow.md](docs/workflow.md) — build, test, simulate, install,
  troubleshoot

## Adding a unit

Copy an existing unit directory, then:

1. `config.mk` — set `PROJECT` and `PROJECT_TYPE` (`osc`/`modfx`/`delfx`/`revfx`)
2. `header.c` — set `name`, a unique `unit_id`, the module in `.target`, and the parameters
3. `dsp.h` — write the engine
4. `osc.h` / `modfx.h` / `revfx.h` — the adapter class, named `Osc`, `ModFx` or
   `RevFx`; `wasm.cc` refers to it by that name
5. Add a case to `tests/render.cc` so it is covered before it ever reaches hardware

Four rules the hardware enforces and the compiler does not: no logue-sdk
includes in `dsp.h`, single precision only, no heap, and `header.c` parameter
order matching the `ParamId` enum. [docs/platform.md](docs/platform.md) explains
why each one bites. `make lint` catches the parameter-descriptor mistakes that
otherwise show up as *"wrong unit min max or center"* at load time.

## Contributing

Issues and pull requests welcome — especially reports from actual hardware,
since the test harness cannot tell you how something sounds. If you send a
patch:

- `make test` must pass, and new behaviour needs a check in `tests/render.cc`
- `make` must cross-build clean, and the unit must stay inside its size budget
- keep `dsp.h` free of SDK includes

Bug reports are more useful with your firmware version and the settings you
were running (amp EG type, `EG Legato`, filter position).

## License

[GPLv3](LICENSE). The units are yours to use, study, modify and share; if you
distribute a modified version, it has to stay under the same terms.

The `logue-sdk` submodule is Korg's, under its own
[BSD 3-Clause license](https://github.com/korginc/logue-sdk/blob/master/LICENSE),
and is left untouched.

## Acknowledgements

Built on [Korg's logue SDK](https://github.com/korginc/logue-sdk). The filter
is Andrew Simper / Vadim Zavalishin's topology-preserving SVF; the anti-aliasing
is the standard polyBLEP correction; `pluck` is Karplus-Strong.

Every unit here uses the placeholder `dev_id` `'IKOL'`. If you fork this and
publish units of your own, register a real developer ID via
[developer_ids.md](https://github.com/korginc/logue-sdk/blob/master/developer_ids.md)
so slots don't collide with someone else's.
