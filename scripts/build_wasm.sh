#!/usr/bin/env bash
# Compile le moteur en WebAssembly et le copie pour le site.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
emsdk="${EMSDK:-$HOME/emsdk}"
if ! command -v emcmake >/dev/null 2>&1; then
    # shellcheck disable=SC1091
    source "$emsdk/emsdk_env.sh" >/dev/null 2>&1
fi
emcmake cmake -S "$root" -B "$root/build-wasm" -G Ninja >/dev/null
cmake --build "$root/build-wasm"
cp "$root/build-wasm/revforge.wasm" "$root/web/public/revforge.wasm"
