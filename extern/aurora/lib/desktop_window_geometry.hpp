/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <aurora/desktop.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace aurora::desktop::window_geometry {

struct Rect {
  int32_t left, top, right, bottom;
};

struct Window {
  uint64_t id;
  Rect bounds;
};

struct Interval {
  int32_t left, right;
};

inline bool valid(const Rect& rect) {
  return rect.left < rect.right && rect.top < rect.bottom;
}

/* Windows arrive front-to-back. Rectangles deliberately describe visible
 * frame bounds, not shadows or invisible resize handles. Occlusion uses
 * rectangles only; translucent regions and rounded corners are not sampled. */
inline std::vector<AuroraDesktopWindowEdge> visible_edges(
    std::span<const Window> windows, int32_t screen_width, int32_t screen_height,
    int32_t floor_top, uint32_t capacity = AURORA_DESKTOP_MAX_WINDOW_EDGES,
    int32_t minimum_width = 32) {
  std::vector<AuroraDesktopWindowEdge> edges;
  capacity = std::min<uint32_t>(capacity, AURORA_DESKTOP_MAX_WINDOW_EDGES);
  if (screen_width <= 0 || screen_height <= 0 || floor_top <= 0 || capacity == 0)
    return edges;
  const int32_t last_top = std::min(screen_height, floor_top);
  minimum_width = std::max(1, minimum_width);
  edges.reserve(capacity);

  for (size_t i = 0; i < windows.size() && edges.size() < capacity; ++i) {
    const auto& window = windows[i];
    const auto& rect = window.bounds;
    if (!window.id || !valid(rect) || rect.top < 0 || rect.top >= last_top)
      continue;
    const int32_t left = std::max(0, rect.left);
    const int32_t right = std::min(screen_width, rect.right);
    if (right <= left || static_cast<int64_t>(right) - left < minimum_width)
      continue;

    std::vector<Interval> covered;
    covered.reserve(i);
    for (size_t front = 0; front < i; ++front) {
      const auto& other = windows[front].bounds;
      if (!windows[front].id || !valid(other) ||
          other.top > rect.top || other.bottom <= rect.top)
        continue;
      const int32_t cover_left = std::max(left, other.left);
      const int32_t cover_right = std::min(right, other.right);
      if (cover_left < cover_right)
        covered.push_back({cover_left, cover_right});
    }
    std::sort(covered.begin(), covered.end(), [](const Interval& a, const Interval& b) {
      return a.left < b.left || (a.left == b.left && a.right < b.right);
    });

    uint32_t span_index = 0;
    const auto append = [&](int32_t span_left, int32_t span_right) {
      const uint32_t index = span_index++;
      if (static_cast<int64_t>(span_right) - span_left < minimum_width || edges.size() >= capacity)
        return;
      edges.push_back({window.id, index, span_left, span_right, rect.top,
                       rect.left, rect.top, rect.right, rect.bottom});
    };
    int32_t cursor = left;
    for (const auto& interval : covered) {
      if (interval.left > cursor) append(cursor, interval.left);
      cursor = std::max(cursor, interval.right);
      if (cursor >= right) break;
    }
    if (cursor < right) append(cursor, right);
  }

  /* Z order determines visibility and bounded selection, but never the
   * returned ordering: bringing an app forward must not reorder its slots. */
  std::sort(edges.begin(), edges.end(), [](const AuroraDesktopWindowEdge& a,
                                          const AuroraDesktopWindowEdge& b) {
    return a.window_id < b.window_id ||
           (a.window_id == b.window_id && a.span_index < b.span_index);
  });
  return edges;
}

} // namespace aurora::desktop::window_geometry
