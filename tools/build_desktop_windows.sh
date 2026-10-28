#!/usr/bin/env bash
# Build and package the experimental Windows x64 Desktop Melee fork on Linux.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
export BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-win}"
export TARGET_ARCH=x86_64
cd "${ROOT_DIR}"

# Desktop tags use their own namespace; the upstream CMake version parser
# expects a bare numeric version, not desktop-v0.1.6. Both launcher and game
# metadata are built from the checked-in desktop version.
if [[ -f "${ROOT_DIR}/DESKTOP_VERSION" ]]; then
    DESKTOP_BUILD_VERSION="$(tr -d '\r\n' < "${ROOT_DIR}/DESKTOP_VERSION")"
    if [[ ! "${DESKTOP_BUILD_VERSION}" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
        echo "error: DESKTOP_VERSION must be MAJOR.MINOR.PATCH" >&2
        exit 1
    fi
    export MELEE_VERSION="${DESKTOP_BUILD_VERSION}"
fi

# Upstream owns dependency acquisition, static libusb, Microsoft runtime
# extraction and the clean-Windows import check. TOOLCHAIN_FILE may point at
# a MinGW toolchain installed in a private prefix.
bash "${SCRIPT_DIR}/package_windows.sh"
cmake --build "${BUILD_DIR}" --target DesktopMelee
python3 "${SCRIPT_DIR}/package_desktop_windows.py" \
    --build-dir "${BUILD_DIR}" \
    --runtime-dir "${ROOT_DIR}/dist/melee-windows-x86_64" \
    --output-dir "${ROOT_DIR}/dist" \
    --package-name Desktop-Melee-Windows-x64 \
    --readme "${ROOT_DIR}/docs/DESKTOP_MELEE.md"
python3 "${SCRIPT_DIR}/package_desktop_installer.py" \
    --package-dir "${ROOT_DIR}/dist/Desktop-Melee-Windows-x64" \
    --portable-archive "${ROOT_DIR}/dist/Desktop-Melee-Windows-x64.zip" \
    --output-dir "${ROOT_DIR}/dist" \
    --makensis "${MAKENSIS:-makensis}"
