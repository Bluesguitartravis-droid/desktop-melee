/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "desktop_window_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace aurora::desktop::wallpaper_mapping {

enum class Position { Center, Tile, Stretch, Fit, Fill };
struct Size { uint32_t width = 0, height = 0; };
struct Crop { uint32_t x = 0, y = 0, width = 0, height = 0; };
struct Layout {
  Size source, screen;
  Position position = Position::Fill;
  double scale_x = 0, scale_y = 0, offset_x = 0, offset_y = 0;
};

inline Size analysis_size(Size screen) {
  if (!screen.width || !screen.height) return {};
  const double scale = std::min({1.0, 640.0 / screen.width, 360.0 / screen.height});
  return {std::max(1u, static_cast<uint32_t>(screen.width * scale)),
          std::max(1u, static_cast<uint32_t>(screen.height * scale))};
}

inline Layout make_layout(Size source, Size screen, Position position) {
  Layout layout{source, screen, position};
  if (!source.width || !source.height || !screen.width || !screen.height) return layout;
  layout.scale_x = static_cast<double>(screen.width) / source.width;
  layout.scale_y = static_cast<double>(screen.height) / source.height;
  if (position == Position::Center || position == Position::Tile) {
    layout.scale_x = layout.scale_y = 1.0;
  } else if (position == Position::Fit) {
    layout.scale_x = layout.scale_y = std::min(layout.scale_x, layout.scale_y);
  } else if (position == Position::Fill) {
    layout.scale_x = layout.scale_y = std::max(layout.scale_x, layout.scale_y);
  }
  if (position != Position::Tile) {
    layout.offset_x = (screen.width - source.width * layout.scale_x) * 0.5;
    layout.offset_y = (screen.height - source.height * layout.scale_y) * 0.5;
  }
  return layout;
}

/* The decoded region is the only part of the source that can appear on the
 * primary monitor. Crop before WIC scaling, so a huge centered image does
 * not reduce its tiny visible portion to just a few analysis pixels. */
inline Crop source_crop(const Layout& layout) {
  if (layout.scale_x <= 0 || layout.scale_y <= 0) return {};
  if (layout.position == Position::Tile) {
    return {0, 0, std::min(layout.source.width, layout.screen.width),
                  std::min(layout.source.height, layout.screen.height)};
  }
  const double left = std::clamp(-layout.offset_x / layout.scale_x, 0.0,
                                 static_cast<double>(layout.source.width));
  const double top = std::clamp(-layout.offset_y / layout.scale_y, 0.0,
                                static_cast<double>(layout.source.height));
  const double right = std::clamp((layout.screen.width - layout.offset_x) / layout.scale_x,
                                  0.0, static_cast<double>(layout.source.width));
  const double bottom = std::clamp((layout.screen.height - layout.offset_y) / layout.scale_y,
                                   0.0, static_cast<double>(layout.source.height));
  const uint32_t x = static_cast<uint32_t>(std::floor(left));
  const uint32_t y = static_cast<uint32_t>(std::floor(top));
  return {x, y, static_cast<uint32_t>(std::ceil(right)) - x,
                static_cast<uint32_t>(std::ceil(bottom)) - y};
}

inline bool source_point(const Layout& layout, double screen_x, double screen_y,
                          double& source_x, double& source_y) {
  if (layout.scale_x <= 0 || layout.scale_y <= 0) return false;
  source_x = (screen_x - layout.offset_x) / layout.scale_x;
  source_y = (screen_y - layout.offset_y) / layout.scale_y;
  if (layout.position == Position::Tile) {
    source_x = std::fmod(source_x, layout.source.width);
    source_y = std::fmod(source_y, layout.source.height);
    if (source_x < 0) source_x += layout.source.width;
    if (source_y < 0) source_y += layout.source.height;
  }
  return source_x >= 0 && source_y >= 0 && source_x < layout.source.width &&
         source_y < layout.source.height;
}

/* Input is the WIC-decoded, optionally downsampled crop in straight RGBA8.
 * Sampling is done in primary-monitor pixels, including real letterbox
 * color. Alpha is composited locally, never used to capture the desktop. */
inline std::vector<uint8_t> compose(std::span<const uint8_t> decoded, Size decoded_size,
                                    Crop crop, const Layout& layout, Size analysis,
                                    std::array<uint8_t, 3> background) {
  if (!decoded_size.width || !decoded_size.height || !crop.width || !crop.height ||
      !analysis.width || !analysis.height || layout.scale_x <= 0 || layout.scale_y <= 0 ||
      decoded.size() < static_cast<uint64_t>(decoded_size.width) * decoded_size.height * 4)
    return {};
  std::vector<uint8_t> result(static_cast<size_t>(analysis.width) * analysis.height * 4);
  for (uint32_t y = 0; y < analysis.height; ++y) {
    for (uint32_t x = 0; x < analysis.width; ++x) {
      auto* pixel = &result[(static_cast<size_t>(y) * analysis.width + x) * 4];
      pixel[0] = background[0]; pixel[1] = background[1]; pixel[2] = background[2]; pixel[3] = 255;
      double sx, sy;
      if (!source_point(layout, (x + 0.5) * layout.screen.width / analysis.width,
                         (y + 0.5) * layout.screen.height / analysis.height, sx, sy)) continue;
      const double dx = std::clamp((sx - crop.x) * decoded_size.width / crop.width - 0.5,
                                   0.0, static_cast<double>(decoded_size.width - 1));
      const double dy = std::clamp((sy - crop.y) * decoded_size.height / crop.height - 0.5,
                                   0.0, static_cast<double>(decoded_size.height - 1));
      const uint32_t x0 = static_cast<uint32_t>(dx), y0 = static_cast<uint32_t>(dy);
      const uint32_t x1 = std::min(x0 + 1, decoded_size.width - 1);
      const uint32_t y1 = std::min(y0 + 1, decoded_size.height - 1);
      const double fx = dx - x0, fy = dy - y0;
      const std::array<size_t, 4> offsets{
          (static_cast<size_t>(y0) * decoded_size.width + x0) * 4,
          (static_cast<size_t>(y0) * decoded_size.width + x1) * 4,
          (static_cast<size_t>(y1) * decoded_size.width + x0) * 4,
          (static_cast<size_t>(y1) * decoded_size.width + x1) * 4};
      const std::array<double, 4> weights{(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
      double alpha = 0, rgb[3]{};
      for (size_t sample = 0; sample < offsets.size(); ++sample) {
        const auto* color = decoded.data() + offsets[sample];
        const double weight = weights[sample] * color[3] / 255.0;
        alpha += weight;
        for (size_t c = 0; c < 3; ++c) rgb[c] += color[c] * weight;
      }
      for (size_t c = 0; c < 3; ++c)
        pixel[c] = static_cast<uint8_t>(std::clamp(std::lround(rgb[c] + background[c] * (1 - alpha)), 0l, 255l));
    }
  }
  return result;
}

inline std::vector<AuroraDesktopWallpaperEdge> clip_edges(
    std::span<const AuroraDesktopWallpaperEdge> edges,
    std::span<const window_geometry::Window> windows,
    uint32_t capacity = AURORA_DESKTOP_MAX_WALLPAPER_EDGES) {
  using window_geometry::Interval;
  std::vector<AuroraDesktopWallpaperEdge> result;
  capacity = std::min<uint32_t>(capacity, AURORA_DESKTOP_MAX_WALLPAPER_EDGES);
  for (const auto& edge : edges) {
    if (result.size() >= capacity) break;
    if (edge.left >= edge.right || edge.source_left >= edge.source_right ||
        edge.left < edge.source_left || edge.right > edge.source_right) continue;
    /* Always evaluate the original line. Repeated clipping and integer
     * endpoint rounding must not gradually change its slope or carry riders. */
    const double slope = (static_cast<double>(edge.source_right_top) - edge.source_top) /
                         (static_cast<double>(edge.source_right) - edge.source_left);
    const auto height_at = [&](double x) {
      return edge.source_top + (x - edge.source_left) * slope;
    };
    std::vector<Interval> covered;
    for (const auto& window : windows) {
      const auto& bounds = window.bounds;
      if (!window_geometry::valid(bounds)) continue;
      double left = std::max(edge.left, bounds.left);
      double right = std::min(edge.right, bounds.right);
      if (left >= right) continue;
      if (slope == 0.0) {
        if (height_at(left) < bounds.top || height_at(left) >= bounds.bottom) continue;
      } else {
        const double at_top = edge.source_left + (static_cast<double>(bounds.top) - edge.source_top) / slope;
        const double at_bottom = edge.source_left + (static_cast<double>(bounds.bottom) - edge.source_top) / slope;
        left = std::max(left, std::min(at_top, at_bottom));
        right = std::min(right, std::max(at_top, at_bottom));
      }
      if (left < right) {
        /* At a fractional crossing, hide up to one extra pixel rather than
         * leave an invisible sliver of collision underneath an app window. */
        covered.push_back({static_cast<int32_t>(std::floor(left)),
                           static_cast<int32_t>(std::ceil(right))});
      }
    }
    std::sort(covered.begin(), covered.end(), [](const Interval& a, const Interval& b) {
      return a.left < b.left || (a.left == b.left && a.right < b.right);
    });
    int32_t cursor = edge.left;
    const auto append = [&](int32_t left, int32_t right) {
      const int32_t minimum_width = edge.contour_id ? 2 : 32;
      if (static_cast<int64_t>(right) - left < minimum_width || result.size() >= capacity) return;
      auto fragment = edge;
      fragment.left = left; fragment.right = right;
      fragment.top = static_cast<int32_t>(std::lround(height_at(left)));
      fragment.right_top = static_cast<int32_t>(std::lround(height_at(right)));
      result.push_back(fragment);
    };
    for (const auto& interval : covered) {
      if (cursor < interval.left) append(cursor, interval.left);
      cursor = std::max(cursor, interval.right);
    }
    if (cursor < edge.right) append(cursor, edge.right);
  }
  return result;
}

} // namespace aurora::desktop::wallpaper_mapping
