# The NTS-1 mkII platform, in detail

Everything here was checked against logue-sdk `master` (SDK v2.0.0) as vendored in
`logue-sdk/`, and against a real build on this machine.

## The machine

| | |
|---|---|
| MCU | STM32H725, ARM Cortex-M7, single-precision FPU (`fpv4-sp-d16`) |
| Sample rate | 48 kHz, fixed |
| User unit format | ELF 32-bit LSB **shared object** (`-fPIC -shared`), loaded dynamically by the firmware |
| Firmware needed | ≥ 1.0.0 for SDK 2.0.0 units |
| Toolchain | `gcc-arm-none-eabi-10.3-2021.10` |

A unit is not firmware. It is a relocatable shared object the synth `dlopen`s
into a slot, with a fixed table of callbacks and a header struct describing
itself. That is why the build links with `-shared --entry=0 -nostartfiles`.

## Module types, slots and budgets

| Module | Slots | Max unit size | External RAM |
|---|---|---|---|
| `osc` | 16 | ~48 KB | none |
| `modfx` | 16 | ~16 KB | 256 KB |
| `delfx` | 8 | ~24 KB | 3 MB |
| `revfx` | 8 | ~24 KB | 3 MB |

"Max unit size" is the whole stripped ELF: code, constants, and static data.
Big wavetables do not fit in an oscillator — there is no SDRAM on the `osc`
runtime, which is why `Processor::getBufferSize()` returns 0 in
`units/poly8/osc.h`. Effects get SDRAM through `desc->hooks.sdram_alloc()`,
called once from `unit_init()`.

Where the units in this repo currently sit (`make` prints these on every
build):

| Unit | Module | text | data | bss |
|---|---|---|---|---|
| `poly8` | osc | 13.5 KB | 1.1 KB | — |
| `epiano` | osc | 14.1 KB | 2.0 KB | — |
| `organ` | osc | 14.7 KB | 2.1 KB | — |
| `piano` | osc | 16.6 KB | 2.7 KB | — |
| `flute` | osc | 15.1 KB | 2.1 KB | — |
| `pluck` | osc | 14.7 KB | 1.0 KB | 24.0 KB |
| `ensemble` | modfx | 5.7 KB | 0.4 KB | — |
| `drive` | modfx | 5.9 KB | 0.4 KB | — |
| `shimmer` | revfx | 7.4 KB | 0.7 KB | — |
| `casio-*` | osc | 15.0 KB | 1.1 KB | — |

The seven `casio-*` units are identical in size: they share one engine and
differ only in the tone constant each pins.

**`text` + `data` is what counts against the budget; `bss` is free.** Only
`data` ships bytes in the ELF, so a large buffer must be zero-initialised and
land in `.bss` — an object holding it has to be zero-initialisable end to end,
which one non-zero member anywhere inside is enough to prevent. `pluck` learned
this the hard way: its 24 KB of Karplus-Strong delay lines started as a member
of an engine that also had non-zero defaults, so the whole object was emitted
as `.data` and the unit shipped 24 KB of zeros — 41 KB of its ~48 KB budget for
a 15 KB program. Handing the storage in from a static in `units/pluck/osc.h`
moved it to `.bss` and cut the shipped unit to 17 KB. Check with
`arm-none-eabi-size` when a unit is unexpectedly large.

## Signal path

```
        ┌──────────────┐
 MIDI ─►│  OSC slot    │─► filter ─► amp EG ─► MOD FX ─► DEL FX ─► REV FX ─► out
        │ (your unit)  │      ▲         ▲
        └──────────────┘      └─────────┴── hardware, monophonic, after you
```

Three consequences that shape every design decision in this repo:

1. The oscillator slot is **one mono voice** as far as the hardware is
   concerned. Its output is a single float channel.
2. The filter and amp EG sit **after** your unit and are applied to the summed
   output. A chord rendered by a poly unit gets one shared envelope — see
   [polyphony.md](polyphony.md).
3. The `in` buffer handed to an oscillator is the stereo audio input, usable
   only if the global routing setting enables it. The runtime assumes it is
   unused unless you call `context->notify_input_usage()`.

## Callback surface

Implemented in `unit.cc` for each unit; the weak defaults in the SDK's
`_unit_base.c` cover anything you leave out.

| Callback | Notes |
|---|---|
| `unit_init(desc)` | Validate target, API, sample rate, channel geometry. Allocate SDRAM here or nowhere. |
| `unit_render(in, out, frames)` | The audio callback. `frames` is small — assume ~64. |
| `unit_set_param_value(id, v)` / `unit_get_param_value(id)` | The runtime does not store your values; cache them yourself. |
| `unit_get_param_str_value(id, v)` | For `k_unit_param_type_strings`. Return a pointer that stays valid after return. |
| `unit_note_on/off`, `unit_all_note_off` | Every note event reaches you, including simultaneous ones. This is the door to polyphony. |
| `unit_pitch_bend`, `unit_channel_pressure`, `unit_aftertouch` | MIDI expression. |
| `unit_set_tempo`, `unit_tempo_4ppqn_tick` | Tempo sync. |
| `unit_reset/resume/suspend/teardown` | Lifecycle. `suspend` means another unit was selected. |

Oscillators also get a runtime context struct each render:

```c
typedef struct unit_runtime_osc_context {
  int32_t  shape_lfo;   // Q31, the hardware LFO routed to shape
  uint16_t pitch;       // 8.8 fixed point: note in the high byte, fraction in the low
  ...
} unit_runtime_osc_context_t;
```

`pitch` tracks **one** note — the last one. Monophonic units use it directly;
poly units ignore it (again, see [polyphony.md](polyphony.md)).

## Parameters

Ten maximum. Slots 0 and 1 are hard-wired to the A and B knobs; slots 2–9 live
in the EDIT menu. Each descriptor is `{min, max, center, default, type, frac,
frac_mode, reserved, name}` with a 4-character name and a type that controls
display formatting (`percent`, `msec`, `cents`, `semi`, `hertz`, `strings`,
`onoff`, `drywet`, `pan`…). Values arrive as `int32_t` in the declared range;
map them to floats yourself.

`center` is not decoration. It must satisfy `min <= center <= max`: set it to
`min` for a unipolar parameter and to `0` for a bipolar one. A descriptor that
breaks this is rejected at load time with **"wrong unit min max or center"**,
and nothing in the C build warns you — `make lint` (`scripts/lint_headers.py`)
checks it instead, along with `init` range and the 4-character name limit.

On the mkII, parameters 0 and 1 are wired to the A and B knobs, where the
7-segment display shows a bare value — a `k_unit_param_type_strings` parameter
there does not show its name. Put a copy of the control in the EDIT menu if the
name matters, and have `unit_get_param_value` report the engine's live value for
both copies so they cannot disagree. `units/piano` does this.

Knob parameters are conventionally `0..1023`. Nothing enforces that, but the
knob resolution is 10-bit, so a wider range just quantises.

## Things that will bite you

- **No doubles.** The FPU is single precision. A stray `double` (including
  bare literals like `0.5` in a C file, or `sin()` instead of `sinf()`) drops
  into software emulation and can cost you the whole CPU budget. The build
  passes `-fsingle-precision-constant` for C/C++ sources, but be deliberate.
- **No heap.** `nano.specs`/`nosys.specs` are linked but there is no usable
  allocator. Everything is static or comes from `sdram_alloc`.
- **No exceptions, no RTTI.** Built with `-fno-exceptions -fno-rtti`.
- **LTO is off** — the SDK Makefile documents a PLT breakage with `-flto`
  alongside `-nostartfiles`. Leave it off.
- **`Processor::pitchBend` takes a `uint8_t`** in the SDK's `processor.h`,
  while the runtime calls `unit_pitch_bend` with a 14-bit `uint16_t`. Inherit
  it blindly and you silently keep only the low 7 bits. `units/poly8/osc.h`
  declares its own `pitchBend(uint16_t)` overload to avoid this.
- **Note delivery depends on a global setting.** With `EG Legato = On` (the
  factory default) the runtime calls `unit_note_on` once per *phrase*, not once
  per key. Anything polyphonic needs `EG Legato = 0`. See
  [polyphony.md](polyphony.md).
- **Velocity was always 0 before firmware 1.2**, and gate events from the
  internal sequencer arrive with note `0xFF`. Handle both, or a unit misbehaves
  on older firmware and under the arpeggiator.
- **`unit_init` must reject mismatches.** Wrong target, API, sample rate or
  channel count has to return the matching `k_unit_err_*`, or the firmware may
  load something it cannot run.
- **Static instances are never destructed.** Free anything you own in
  `unit_teardown()`.

## Sources

- [logue SDK docs](https://korginc.github.io/logue-sdk/)
- [korginc/logue-sdk on GitHub](https://github.com/korginc/logue-sdk) — vendored here as a submodule
- [NTS-1 mkII product page](https://www.korg.com/us/products/synthesizers/nts_1_mk2/)
