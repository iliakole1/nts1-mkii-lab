# Working in this repo

Custom units for the Korg NTS-1 digital kit mkII (logue SDK v2). Read
`docs/platform.md` before touching DSP or headers — it lists the constraints
that are easy to violate silently.

## Non-negotiables

- **Unit DSP (`units/*/dsp.h`) must not include logue-sdk headers.** That
  portability is what lets `tests/render.cc` build and verify the same code
  natively. SDK-specific code belongs in `unit.cc`, `header.c`, and the
  `osc.h` / `modfx.h` adapters.
- **Single precision only.** No `double`, no `sin()`/`pow()` — use `sinf`,
  `powf`, or the helpers in `common/`. The FPU is `fpv4-sp-d16`.
- **No heap.** Static allocation, or `sdram_alloc()` from `unit_init()` for
  effects. Oscillators get no SDRAM at all.
- **Parameter order in `header.c` must match the `ParamId` enum in `dsp.h`.**
  Nothing checks this; the symptom is knobs doing the wrong thing.
- **`center` must be inside `[min, max]`** — `min` for unipolar parameters, `0`
  for bipolar. Otherwise the device rejects the unit with "wrong unit min max
  or center". `make lint` enforces this and the 4-char name limit.
- **Size budgets:** osc ~48 KB, modfx ~16 KB, delfx/revfx ~24 KB. `make` prints
  the text/data/bss sizes on every build — watch them.

## Verify before claiming it works

`make test` runs the host harness (tuning, voice lifecycle, headroom, silence,
stereo) and writes `dist/test/*.wav`. Add coverage there for anything new.
`make` cross-builds; a clean build is not evidence that it sounds right.

Neither of these proves behaviour on hardware. Say so plainly when reporting.

## Toolchain

`scripts/setup.sh` fetches everything. The ARM toolchain is x86_64 and runs
under Rosetta on Apple Silicon; that is expected, not a bug.
