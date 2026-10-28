/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <aurora/desktop.h>
#include "desktop_window_geometry.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace aurora::desktop::linux_x11 {
enum Event : uint32_t {
  Settings = 1u << 0,
  Quit = 1u << 1,
  LayoutChanged = 1u << 2,
  Failed = 1u << 3,
};

/* UI-thread-only XCB connection. It never shares SDL's X connection, changes
 * the process-global Xlib error handler, or reads application pixel contents.
 * This backend deliberately rejects Wayland/XWayland and Gamescope: an X11
 * client cannot provide complete native Wayland window-platform geometry. */
bool query_geometry(AuroraDesktopGeometry& geometry);
bool create_overlay(const AuroraDesktopGeometry& geometry);
void destroy_overlay();
bool present(std::span<const uint8_t> premultiplied_bgra, uint32_t width, uint32_t height);
uint32_t poll();
bool controls_active() noexcept;
void shutdown();
const char* unavailable_reason() noexcept;
std::vector<window_geometry::Window> enumerate_windows(const AuroraDesktopGeometry& geometry);
} // namespace aurora::desktop::linux_x11
