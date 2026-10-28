#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
cd -- "$repo_root"
"${MELEE_TEST_CC:-gcc}" -std=gnu11 -O2 -g \
    -DTARGET_PC=1 -DMELEE_PC=1 \
    -Iextern/aurora/include -Isrc -Isrc/sdk_include \
    -include src/pc/compat.h -fno-strict-aliasing \
    -ffunction-sections -fdata-sections \
    -Wno-scalar-storage-order -Wno-int-conversion \
    -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
    tests/desktop_taskbar_test.c src/pc/desktop_game.c src/pc/desktop_fighter.c \
    src/melee/ft/fighter.c src/melee/gr/stage.c \
    src/melee/lb/lbvector.c \
    src/melee/mp/mplib.c src/melee/mp/mpisland.c src/melee/mp/mpcoll.c \
    -Wl,--gc-sections -lm \
    -o "$test_dir/desktop_taskbar_test"
"$test_dir/desktop_taskbar_test"
