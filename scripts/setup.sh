#!/usr/bin/env bash
#
# One-time setup: SDK submodule, CMSIS headers, ARM toolchain.
# Safe to re-run; each step is skipped if already done.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo ">> logue-sdk submodule"
git submodule update --init logue-sdk

echo ">> CMSIS (provides arm_math.h; ~290 MB)"
git -C logue-sdk submodule update --init --depth 1 platform/ext/CMSIS

TOOLCHAIN="logue-sdk/tools/gcc/gcc-arm-none-eabi-10.3-2021.10"
if [[ -x "${TOOLCHAIN}/bin/arm-none-eabi-gcc" ]]; then
  echo ">> toolchain already present"
else
  echo ">> ARM toolchain 10.3-2021.10 (~1 GB unpacked)"
  ./logue-sdk/tools/gcc/get_gcc_10_3-2021_10_macos.sh
fi

if ! "${TOOLCHAIN}/bin/arm-none-eabi-gcc" --version >/dev/null 2>&1; then
  cat <<'EOF'

The toolchain is installed but did not run. It is an x86_64 binary; on Apple
Silicon it needs Rosetta 2:

    softwareupdate --install-rosetta --agree-to-license

EOF
  exit 1
fi

echo
echo ">> ready. Try:  make test   then   make"
