#!/usr/bin/env bash
# Native x86-64 Linux build. No administrator changes and no disc data needed.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-${root}/build-linux}"
output="${OUTPUT_DIR:-${root}/dist/linux}"
packaging_tools="${PACKAGING_TOOLS_DIR:-${build}/packaging-tools}"
if [[ "$(uname -m)" != "x86_64" ]]; then
    echo "This Linux desktop preview currently builds for x86_64 (including Steam Deck)." >&2
    exit 1
fi
export MELEE_VERSION="$(cat "${root}/DESKTOP_VERSION")"
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DMELEE_VERSION="$MELEE_VERSION" \
    -DAURORA_SDL3_PROVIDER=vendor -DAURORA_SDL3_LINKAGE=shared \
    -DAURORA_DAWN_PROVIDER=package -DAURORA_NOD_PROVIDER=package "$@"
cmake --build "$build" --target melee DesktopMelee --parallel "${BUILD_JOBS:-4}"
python3 "${root}/tools/linux/fetch_packaging_tools.py" --output-dir "$packaging_tools"
python3 "${root}/tools/package_desktop_linux.py" \
    --build-dir "$build" --output-dir "$output" \
    --linuxdeploy "${packaging_tools}/linuxdeploy" \
    --appimagetool "${packaging_tools}/appimagetool" \
    --runtime "${packaging_tools}/runtime-x86_64"
