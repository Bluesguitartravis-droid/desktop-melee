#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/desktop-melee-linux-test.XXXXXX")"
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pthread \
    "${root}/tests/desktop_linux_test.cpp" -o "${test_dir}/desktop_linux_test"
"${test_dir}/desktop_linux_test"
