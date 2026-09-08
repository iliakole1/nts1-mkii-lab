#!/usr/bin/env python3
"""Validate the parameter descriptors in every unit header.c.

The device rejects a unit outright ("wrong unit min max or center") if a
descriptor is inconsistent, and nothing in the C build catches it. Rules, per
the SDK's platform README:

  min <= center <= max      center marks the logical centre; for unipolar
                            parameters set it to min
  min <= init <= max
  name <= 4 characters      longer names are truncated on the display
  bipolar types (pan/spread/drywet) want center == 0

Run: python3 scripts/lint_headers.py   (also wired into `make test`)
"""

import glob
import os
import re
import sys

PARAM_RE = re.compile(
    r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
    r"\s*(k_unit_param_type_\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,"
    r'\s*\{\s*"([^"]*)"\s*\}\s*\}'
)

BIPOLAR = {
    "k_unit_param_type_pan",
    "k_unit_param_type_spread",
    "k_unit_param_type_drywet",
}


def lint(path):
    errors = []
    with open(path) as f:
        source = f.read()

    params = PARAM_RE.findall(source)
    if not params:
        return ["no parameter descriptors found (did the format change?)"]

    declared = re.search(r"\.num_params\s*=\s*(\d+)", source)
    if declared and int(declared.group(1)) > len(params):
        errors.append(
            f"num_params = {declared.group(1)} but only {len(params)} descriptors"
        )

    for i, (mn, mx, ctr, init, ptype, frac, fmode, reserved, name) in enumerate(params):
        mn, mx, ctr, init = int(mn), int(mx), int(ctr), int(init)
        label = f"param {i} ({name or 'unused'})"

        if mn > mx:
            errors.append(f"{label}: min {mn} > max {mx}")
        if not mn <= ctr <= mx:
            errors.append(
                f"{label}: center {ctr} outside [{mn}, {mx}] "
                f"— use {mn} for a unipolar parameter"
            )
        if not mn <= init <= mx:
            errors.append(f"{label}: init {init} outside [{mn}, {mx}]")
        if len(name) > 4:
            errors.append(f"{label}: name '{name}' is longer than 4 characters")
        if ptype in BIPOLAR and ctr != 0:
            errors.append(f"{label}: {ptype} expects center 0, got {ctr}")
        if int(reserved) != 0:
            errors.append(f"{label}: reserved bits must be zero")

    return errors


def lint_casio_pins(root):
    """Each Casio unit must pin the tone its directory is named after.

    The seven units share one engine and differ only in the enum they hand to
    init(). Nothing catches a copy-paste slip there: a horn/dsp.h that says
    kFlute compiles, links, loads, and quietly ships a second flute.
    """
    errors = []
    pattern = os.path.join(root, "units", "casio", "*", "dsp.h")
    pins = {}
    for path in sorted(glob.glob(pattern)):
        name = os.path.basename(os.path.dirname(path))
        with open(path) as f:
            source = f.read()
        found = re.search(r"Pt20Engine::init\(Pt20Engine::(k\w+)\)", source)
        if not found:
            errors.append(f"casio/{name}: no Pt20Engine::init(Pt20Engine::k...) call")
            continue
        pinned = found.group(1)
        expected = "k" + name.capitalize()
        if pinned != expected:
            errors.append(f"casio/{name}: pins {pinned}, expected {expected}")
        if pinned in pins:
            errors.append(f"casio/{name}: pins {pinned}, already used by casio/{pins[pinned]}")
        pins[pinned] = name
    return errors


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    headers = sorted(
        glob.glob(os.path.join(root, "units", "*", "header.c"))
        + glob.glob(os.path.join(root, "units", "*", "*", "header.c"))
    )
    if not headers:
        print("no unit headers found")
        return 1

    failed = 0

    casio_errors = lint_casio_pins(root)
    if casio_errors:
        failed += 1
        print("  [FAIL] units/casio/*/dsp.h tone pinning")
        for e in casio_errors:
            print(f"         {e}")
    elif glob.glob(os.path.join(root, "units", "casio", "*", "dsp.h")):
        print("  [PASS] units/casio/*/dsp.h tone pinning")

    for path in headers:
        rel = os.path.relpath(path, root)
        errors = lint(path)
        if errors:
            failed += 1
            print(f"  [FAIL] {rel}")
            for e in errors:
                print(f"         {e}")
        else:
            print(f"  [PASS] {rel}")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
