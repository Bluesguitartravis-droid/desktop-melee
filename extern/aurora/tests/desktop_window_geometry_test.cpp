/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../lib/desktop_window_geometry.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

using aurora::desktop::window_geometry::Window;
using aurora::desktop::window_geometry::visible_edges;

static void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

static std::vector<AuroraDesktopWindowEdge> for_window(
    const std::vector<AuroraDesktopWindowEdge>& edges, uint64_t id) {
  std::vector<AuroraDesktopWindowEdge> result;
  for (const auto& edge : edges) if (edge.window_id == id) result.push_back(edge);
  return result;
}

int main() {
  unsigned checks = 0;
  {
    const std::array windows{Window{7, {-50, 120, 2200, 700}}};
    const auto edges = visible_edges(windows, 1920, 1080, 1040);
    check(edges.size() == 1 && edges[0].left == 0 && edges[0].right == 1920 &&
          edges[0].top == 120, "clip a top edge to the primary monitor");
    check(edges[0].window_left == -50 && edges[0].window_right == 2200 &&
          edges[0].window_top == 120 && edges[0].window_bottom == 700,
          "preserve full bounds for motion and resizing");
    checks += 2;
  }
  {
    const std::array windows{Window{9, {200, 50, 300, 350}},
                             Window{3, {100, 200, 600, 500}}};
    const auto edges = visible_edges(windows, 1920, 1080, 1040);
    const auto back = for_window(edges, 3);
    check(back.size() == 2 && back[0].left == 100 && back[0].right == 200 &&
          back[1].left == 300 && back[1].right == 600,
          "subtract a foreground window from the lower top edge");
    check(back[0].span_index == 0 && back[1].span_index == 1 && edges.front().window_id == 3,
          "fragments have left-to-right indices and output has stable window ordering");
    checks += 2;
  }
  {
    const std::array windows{Window{8, {0, 0, 900, 600}},
                             Window{3, {100, 200, 600, 500}}};
    check(for_window(visible_edges(windows, 1920, 1080, 1040), 3).empty(),
          "omit a fully covered top edge");
    ++checks;
  }
  {
    const std::array windows{Window{8, {180, 40, 350, 400}},
                             Window{9, {250, 50, 500, 400}},
                             Window{3, {100, 200, 700, 500}}};
    const auto back = for_window(visible_edges(windows, 1920, 1080, 1040), 3);
    check(back.size() == 2 && back[0].left == 100 && back[0].right == 180 &&
          back[1].left == 500 && back[1].right == 700,
          "overlapping occluders behave as their interval union");
    ++checks;
  }
  {
    const std::array windows{Window{8, {100, 20, 500, 200}},
                             Window{9, {0, 20, 100, 400}},
                             Window{3, {100, 200, 600, 500}}};
    const auto back = for_window(visible_edges(windows, 1920, 1080, 1040), 3);
    check(back.size() == 1 && back[0].left == 100 && back[0].right == 600,
          "exclusive bottom/right boundaries do not hide adjacent edges");
    ++checks;
  }
  {
    const std::array windows{Window{8, {-100, -50, 400, 80}},
                             Window{3, {100, 40, 800, 500}}};
    const auto edges = visible_edges(windows, 1920, 1080, 1040);
    check(for_window(edges, 8).empty() && edges.size() == 1 &&
          edges[0].left == 400 && edges[0].right == 800,
          "offscreen tops are omitted while their visible window bodies still occlude");
    ++checks;
  }
  {
    const std::array windows{Window{8, {120, 40, 580, 400}},
                             Window{3, {100, 200, 600, 500}}};
    check(for_window(visible_edges(windows, 1920, 1080, 1040), 3).empty(),
          "discard unplayably narrow leftover spans");
    ++checks;
  }
  {
    const std::array windows{Window{1, {100, 1040, 500, 1070}},
                             Window{2, {100, 1100, 500, 1400}},
                             Window{3, {2100, 200, 2600, 500}},
                             Window{4, {500, 200, 100, 400}}};
    check(visible_edges(windows, 1920, 1080, 1040).empty(),
          "omit taskbar-level, off-monitor and malformed windows");
    check(visible_edges(windows, 0, 1080, 1040).empty() &&
          visible_edges(windows, 1920, 1080, 1040, 0).empty(),
          "invalid screen or zero capacity produces no edges");
    checks += 2;
  }
  {
    const std::array before{Window{11, {100, 200, 500, 600}},
                            Window{6, {600, 300, 1000, 700}}};
    const std::array after{Window{6, {600, 300, 1000, 700}},
                           Window{11, {125, 210, 625, 610}}};
    const auto a = visible_edges(before, 1920, 1080, 1040);
    const auto b = visible_edges(after, 1920, 1080, 1040);
    check(a.size() == 2 && b.size() == 2 && a[0].window_id == b[0].window_id &&
          a[1].window_id == b[1].window_id, "z-order changes do not reorder unoccluded window IDs");
    const auto moved = for_window(b, 11);
    check(moved.size() == 1 && moved[0].window_left == 125 && moved[0].window_top == 210 &&
          moved[0].window_right == 625 && moved[0].right == 625,
          "moving and resizing updates geometry while keeping window identity");
    const std::array closed{after[0]};
    check(for_window(visible_edges(closed, 1920, 1080, 1040), 11).empty(),
          "closed windows disappear from the next snapshot");
    checks += 3;
  }
  {
    std::vector<Window> windows;
    for (int i = 0; i < 40; ++i)
      windows.push_back({static_cast<uint64_t>(40 - i), {i * 40, 50, i * 40 + 32, 100}});
    const auto bounded = visible_edges(windows, 1920, 1080, 1040, 100);
    check(bounded.size() == AURORA_DESKTOP_MAX_WINDOW_EDGES,
          "bounded output never exceeds the shared collision capacity");
    check(visible_edges(windows, 1920, 1080, 1040, 5).size() == 5,
          "respect caller's smaller capacity");
    checks += 2;
  }
  {
    /* Compare the interval implementation with an independent pixel oracle
     * across mixed overlaps, clipping, equal edges and offscreen rectangles. */
    uint32_t seed = 0x4d454c45;
    const auto random = [&] { seed = seed * 1664525u + 1013904223u; return seed; };
    for (unsigned scene = 0; scene < 128; ++scene) {
      std::array<Window, 6> windows{};
      for (size_t i = 0; i < windows.size(); ++i) {
        const int32_t left = static_cast<int32_t>(random() % 112) - 16;
        const int32_t top = static_cast<int32_t>(random() % 56) - 8;
        windows[i] = {i + 1, {left, top, left + 1 + static_cast<int32_t>(random() % 48),
                             top + 1 + static_cast<int32_t>(random() % 30)}};
      }
      const auto edges = visible_edges(windows, 96, 48, 46, 32, 1);
      for (size_t i = 0; i < windows.size(); ++i) {
        const auto& candidate = windows[i];
        for (int32_t x = 0; x < 96; ++x) {
          bool expected = candidate.bounds.top >= 0 && candidate.bounds.top < 46 &&
                          x >= candidate.bounds.left && x < candidate.bounds.right;
          for (size_t j = 0; expected && j < i; ++j) {
            const auto& above = windows[j].bounds;
            if (x >= above.left && x < above.right && above.top <= candidate.bounds.top &&
                candidate.bounds.top < above.bottom) expected = false;
          }
          bool actual = false;
          for (const auto& edge : edges)
            if (edge.window_id == candidate.id && x >= edge.left && x < edge.right) actual = true;
          check(actual == expected, "128-scene pixel oracle agrees with interval visibility");
        }
      }
    }
    ++checks;
  }
  std::printf("Desktop window geometry: %u checks passed\n", checks);
}
