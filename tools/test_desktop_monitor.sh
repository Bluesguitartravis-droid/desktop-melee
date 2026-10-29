#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
${CXX:-g++} -std=c++20 -O2 -Wall -Wextra -Werror -Iextern/aurora/include extern/aurora/tests/desktop_monitor_geometry_test.cpp -o "$out/test"
"$out/test"
