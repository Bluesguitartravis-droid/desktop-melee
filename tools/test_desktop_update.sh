#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
cd -- "$repo_root"
"${MELEE_TEST_CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Werror \
    -Itools/windows tests/desktop_update_test.cpp -o "$test_dir/desktop_update_test"
"$test_dir/desktop_update_test"
