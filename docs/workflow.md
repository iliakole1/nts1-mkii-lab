# Day-to-day workflow

## One-time setup

```bash
./scripts/setup.sh
```

That initialises the `logue-sdk` submodule, fetches CMSIS (~290 MB, needed for
`arm_math.h`), and downloads the ARM toolchain into
`logue-sdk/tools/gcc/` (~1 GB unpacked).

### A note on Apple Silicon

Korg pins `gcc-arm-none-eabi-10.3-2021.10`, and ARM only ever shipped that
release as an x86_64 macOS binary. It runs fine under Rosetta 2 — a full unit
build here takes about 8 seconds. If `arm-none-eabi-gcc` reports "bad CPU type",
install Rosetta:

```bash
softwareupdate --install-rosetta --agree-to-license
```

Newer arm64-native toolchains (Homebrew's `gcc-arm-embedded`) usually work too;
point the build at one with `make GCC_BIN_PATH=/path/to/bin`. Stick with 10.3
when in doubt — it is what Korg tests against.

## The loop

```bash
make test          # host build of the DSP + WAV renders, ~2 s
make               # cross-build every unit into dist/
make poly8         # just one unit
make clean
```

`make test` is where most of the work happens. `tests/render.cc` compiles the
same `units/*/dsp.h` code natively, checks tuning, voice allocation, headroom
and stereo behaviour, and writes `dist/test/*.wav` to listen to. The unit DSP
deliberately includes no logue-sdk headers so this stays possible — SDK-specific
glue lives in `unit.cc`, `header.c` and the `osc.h` / `modfx.h` adapters.

### Optional: the browser simulator

The SDK ships a WebAudio sandbox. It needs emscripten:

```bash
cd logue-sdk && git submodule update --init tools/emsdk
cd tools/emsdk && ./emsdk install latest && ./emsdk activate latest
cd ../../../units/poly8 && make wasm
```

`make wasm` builds the unit to WebAssembly and serves a page with knobs. Every
unit here keeps a `wasm.cc` shell for exactly this. Oscillators get the SDK's
`osc.html` sandbox, which has a keyboard; effects get `fx.html`, which has an
audio source to feed them. `mk/nts1mkii.mk` picks the right one from the unit's
`PROJECT_TYPE`.

The shell declares the processor by class name, so `wasm.cc` must agree with
the adapter header: `Osc` for oscillators, `ModFx` for mod effects, `RevFx` for
the reverb. The SDK's own copies say `Modfx` and `Reverb`; a shell copied
straight from the SDK will not compile here until it is renamed.

## Getting a unit onto the synth

1. Connect the NTS-1 mkII over USB and leave it powered on.
2. Open **KORG KONTROL Editor** (free, Mac/Windows — see the
   [editor page](https://www.korg.com/us/products/synthesizers/nts_1_mk2/editor.php)).
3. Drag `dist/poly8.nts1mkiiunit` onto a row of the **oscillator** user unit
   list, or use *File → Import User Unit…*. Effects go into the mod/delay/reverb
   lists by type.
4. On the device the unit appears at the end of the OSC (or FX) selection list.

Do not unplug or power off while the editor is transferring.

The device remembers a unit by `dev_id` + `unit_id` + name, so slots can be
reshuffled freely — but **bump `version` in `header.c` whenever you reload a
changed build**, and keep `unit_id` unique per unit. Both units here use the
placeholder developer ID `0x494B4F4C` ('IKOL'); if you ever publish, register a
real one via a PR against
[developer_ids.md](https://github.com/korginc/logue-sdk/blob/master/developer_ids.md).

## Troubleshooting

| Symptom | Cause |
|---|---|
| `arm_math.h: No such file or directory` | CMSIS submodule not fetched — rerun `scripts/setup.sh`. |
| `wrong unit min max or center` when loading | A parameter descriptor in `header.c` has `center` outside `[min, max]` (or `init` outside it). `make lint` catches this. |
| Unit refuses to load | `unit_init()` returned an error: check target/API/sample rate/channel geometry, or the unit exceeded its size budget. |
| Unit loads but is silent | For a poly unit, check the hardware amp EG is set to `Open` and the filter is open. |
| A poly unit plays only one note | Global `EG Legato` is `On` (the factory default), so the runtime sends a single note-on for the whole phrase. Power on holding **REVERB**, set `EG Legato` to `0`, press **ARP**. |
| Chord ducks and swells as one | Hardware amp EG is not `Open`. See [polyphony.md](polyphony.md). |
| Audio crackles or drops out | You are over budget in `unit_render` — move work to per-block or per-event, and check for accidental `double` math. |
