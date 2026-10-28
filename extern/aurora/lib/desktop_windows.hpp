/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "desktop_window_geometry.hpp"
#include <span>

namespace aurora::desktop::windows {
/* UI-thread-only cached read-only bounds; independent of the window-platform
 * checkbox, because wallpaper collision also needs app-window occlusion. */
std::span<const window_geometry::Window> current_windows();
}
