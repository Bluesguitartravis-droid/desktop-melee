#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
repo_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
gpu_smoke=''
gpu_resources=''
if (( $# )); then
  if [[ $# != 3 || "$1" != --gpu-smoke || ! -x "$2" || ! -d "$3" ]]; then
    echo 'Usage: test_desktop_linux.sh [--gpu-smoke EXECUTABLE RESOURCES_DIRECTORY]' >&2
    exit 2
  fi
  gpu_smoke=$(realpath -- "$2")
  gpu_resources=$(realpath -- "$3")
fi
test_dir=$(mktemp -d)
xvfb_pid=''
compositor_pid=''
cleanup() {
  if [[ -n "$compositor_pid" ]]; then kill "$compositor_pid" 2>/dev/null || true; fi
  if [[ -n "$xvfb_pid" ]]; then kill "$xvfb_pid" 2>/dev/null || true; fi
  rm -rf -- "$test_dir"
}
trap cleanup EXIT
read -r -a xcb_flags <<< "$(pkg-config --cflags --libs xcb xcb-render xcb-shape xcb-randr)"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pthread \
  -I"$repo_dir/extern/aurora/include" -I"$repo_dir/extern/aurora/lib" \
  "$repo_dir/tests/desktop_linux_x11_test.cpp" \
  "$repo_dir/extern/aurora/lib/desktop_linux_x11.cpp" \
  "${xcb_flags[@]}" -o "$test_dir/desktop_linux_x11_test"
Xvfb -screen 0 1280x800x24 -nolisten tcp -ac -displayfd 3 \
  3>"$test_dir/display" >"$test_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
for ((attempt=0; attempt<60; ++attempt)); do
  [[ -s "$test_dir/display" ]] && break
  if ! kill -0 "$xvfb_pid" 2>/dev/null; then tail -40 "$test_dir/xvfb.log" >&2; exit 1; fi
  sleep 0.05
done
[[ -s "$test_dir/display" ]] || { tail -40 "$test_dir/xvfb.log" >&2; exit 1; }
export DISPLAY=":$(<"$test_dir/display")"
export XDG_SESSION_TYPE=x11
unset WAYLAND_DISPLAY GAMESCOPE_WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP DESKTOP_SESSION
"$test_dir/desktop_linux_x11_test" --without-compositor
xcompmgr -a -d "$DISPLAY" >"$test_dir/compositor.log" 2>&1 &
compositor_pid=$!
# The compositor has one short initialization round trip before claiming its
# selection. A bounded retry below handles slower CI hosts without races.
for ((attempt=0; attempt<50; ++attempt)); do
  if ! kill -0 "$compositor_pid" 2>/dev/null; then cat "$test_dir/compositor.log" >&2; exit 1; fi
  if "$test_dir/desktop_linux_x11_test" >"$test_dir/test.log" 2>&1; then
    cat "$test_dir/test.log"
    if [[ -n "$gpu_smoke" ]]; then
      timeout 60s "$gpu_smoke" "$gpu_resources" "$test_dir/gpu-state"
    fi
    exit 0
  fi
  if ! grep -q 'native primary display and panel discovered.*compositing' "$test_dir/test.log"; then
    cat "$test_dir/test.log" >&2
    exit 1
  fi
  sleep 0.05
done
cat "$test_dir/test.log" >&2
exit 1
