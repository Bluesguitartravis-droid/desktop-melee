/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../lib/desktop_wallpaper_mapping.hpp"

#include <cstdio>
#include <cstdlib>

namespace mapping = aurora::desktop::wallpaper_mapping;
using aurora::desktop::window_geometry::Window;
using RGB = std::array<uint8_t, 3>;

namespace {
void check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
RGB pixel(const std::vector<uint8_t>& image, uint32_t width, uint32_t x, uint32_t y) {
  const size_t i = (y * width + x) * 4;
  return {image[i], image[i + 1], image[i + 2]};
}
std::vector<uint8_t> raw_crop(std::span<const uint8_t> rgba, mapping::Size source,
                             mapping::Crop crop) {
  std::vector<uint8_t> result;
  for (uint32_t y = crop.y; y < crop.y + crop.height; ++y) {
    for (uint32_t x = crop.x; x < crop.x + crop.width; ++x) {
      const size_t offset = (y * source.width + x) * 4;
      result.insert(result.end(), rgba.begin() + offset, rgba.begin() + offset + 4);
    }
  }
  return result;
}
}

int main() {
  unsigned checks = 0;
  const RGB red{255, 0, 0}, blue{0, 0, 255}, green{0, 255, 0}, yellow{255, 255, 0};
  const RGB background{20, 40, 60};
  const std::vector<uint8_t> source{
    255,0,0,255, 255,0,0,255, 0,0,255,255, 0,0,255,255,
    0,255,0,255, 0,255,0,255, 255,255,0,255, 255,255,0,255,
  };
  const auto rendered = [&](mapping::Position position) {
    const auto layout = mapping::make_layout({4, 2}, {8, 8}, position);
    const auto crop = mapping::source_crop(layout);
    return mapping::compose(raw_crop(source, {4, 2}, crop), {crop.width, crop.height},
                            crop, layout, {8, 8}, background);
  };
  {
    const auto image = rendered(mapping::Position::Stretch);
    check(image.size() == 8 * 8 * 4 && pixel(image, 8, 0, 0) == red &&
          pixel(image, 8, 7, 0) == blue && pixel(image, 8, 0, 7) == green &&
          pixel(image, 8, 7, 7) == yellow, "Stretch fills all four display corners"); ++checks;
  }
  {
    const auto image = rendered(mapping::Position::Fit);
    check(pixel(image, 8, 0, 0) == background && pixel(image, 8, 7, 1) == background &&
          pixel(image, 8, 0, 6) == background && pixel(image, 8, 7, 7) == background,
          "Fit preserves exact letterbox bars"); ++checks;
    check(pixel(image, 8, 0, 2) == red && pixel(image, 8, 7, 2) == blue &&
          pixel(image, 8, 0, 5) == green && pixel(image, 8, 7, 5) == yellow,
          "Fit positions the uncropped image inside its bars"); ++checks;
  }
  {
    const auto layout = mapping::make_layout({4, 2}, {8, 8}, mapping::Position::Fill);
    const auto crop = mapping::source_crop(layout);
    check(crop.x == 1 && crop.y == 0 && crop.width == 2 && crop.height == 2,
          "Fill selects the centered source crop"); ++checks;
    const auto image = rendered(mapping::Position::Fill);
    check(pixel(image, 8, 0, 0) == red && pixel(image, 8, 7, 0) == blue &&
          pixel(image, 8, 0, 7) == green && pixel(image, 8, 7, 7) == yellow,
          "Fill covers the display without letterbox color"); ++checks;
  }
  {
    const auto image = rendered(mapping::Position::Center);
    check(pixel(image, 8, 1, 3) == background && pixel(image, 8, 6, 3) == background &&
          pixel(image, 8, 2, 2) == background && pixel(image, 8, 5, 5) == background &&
          pixel(image, 8, 2, 3) == red && pixel(image, 8, 5, 4) == yellow,
          "Center leaves the original image size unchanged"); ++checks;
    const auto layout = mapping::make_layout({10000, 8000}, {1920, 1080}, mapping::Position::Center);
    const auto crop = mapping::source_crop(layout);
    check(crop.x == 4040 && crop.y == 3460 && crop.width == 1920 && crop.height == 1080,
          "oversized centered wallpaper decodes only the visible crop"); ++checks;
  }
  {
    const auto image = rendered(mapping::Position::Tile);
    bool correct = true;
    for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 0; x < 8; ++x) {
      const RGB expected = y % 2 == 0 ? (x % 4 < 2 ? red : blue) :
                                             (x % 4 < 2 ? green : yellow);
      correct = correct && pixel(image, 8, x, y) == expected;
    }
    check(correct, "Tile repeats the unscaled image at exact pixel boundaries"); ++checks;
  }
  {
    const auto layout = mapping::make_layout({1, 1}, {1, 1}, mapping::Position::Stretch);
    const std::array<uint8_t, 4> rgba{200, 100, 0, 128};
    const auto image = mapping::compose(rgba, {1, 1}, {0, 0, 1, 1}, layout, {1, 1}, background);
    check(pixel(image, 1, 0, 0) == RGB{110, 70, 30} && image[3] == 255,
          "transparent wallpaper composites over the desktop background color"); ++checks;
    const auto mixed_layout = mapping::make_layout({2, 1}, {1, 1}, mapping::Position::Stretch);
    const std::array<uint8_t, 8> mixed{255,0,0,255, 0,255,0,0};
    const auto mixed_image = mapping::compose(mixed, {2, 1}, {0, 0, 2, 1}, mixed_layout,
                                              {1, 1}, {0, 0, 0});
    check(pixel(mixed_image, 1, 0, 0) == RGB{128, 0, 0},
          "filtering transparent pixels does not introduce colored fringes"); ++checks;
  }
  {
    const auto normal = mapping::analysis_size({1920, 1080});
    const auto portrait = mapping::analysis_size({1080, 1920});
    check(normal.width == 640 && normal.height == 360 && portrait.width <= 640 &&
          portrait.height == 360 && mapping::analysis_size({0, 1080}).width == 0,
          "analysis size respects landscape, portrait and invalid displays"); ++checks;
    check(mapping::compose({}, {}, {}, {}, {}, background).empty(),
          "invalid decoding or layout returns an empty image"); ++checks;
  }
  {
    const std::array edges{AuroraDesktopWallpaperEdge{
        77, 4, 0, 1000, 500, 500, 0, 0, 1000, 500, 500}};
    const std::array windows{Window{11, {100, 200, 300, 600}},
                             Window{12, {250, 100, 600, 550}},
                             Window{13, {0, 501, 1000, 700}}};
    const auto visible = mapping::clip_edges(edges, windows);
    check(visible.size() == 2 && visible[0].left == 0 && visible[0].right == 100 &&
          visible[1].left == 600 && visible[1].right == 1000,
          "overlapping app windows clip only the covered wallpaper regions"); ++checks;
    check(visible[0].generation == 77 && visible[1].generation == 77 &&
          visible[0].edge_id == 4 && visible[1].edge_id == 4,
          "wallpaper fragments keep their original generation and identity"); ++checks;
    const std::array covers{Window{22, {0, 0, 1000, 600}}};
    check(mapping::clip_edges(edges, covers).empty(), "covered wallpaper creates no invisible floor"); ++checks;
    const std::array adjacent{Window{23, {0, 0, 1000, 500}}};
    check(mapping::clip_edges(edges, adjacent).size() == 1,
          "a window's exclusive bottom boundary leaves an adjacent ledge visible"); ++checks;
    check(mapping::clip_edges(edges, windows, 1).size() == 1 &&
          mapping::clip_edges(edges, windows, 0).empty(), "clipping respects its output capacity"); ++checks;
  }
  {
    const std::array edges{AuroraDesktopWallpaperEdge{
        88, 7, 0, 1000, 300, 700, 1, 0, 1000, 300, 700}};
    const std::array windows{Window{11, {100, 400, 900, 600}}};
    const auto visible = mapping::clip_edges(edges, windows);
    check(visible.size() == 2 && visible[0].left == 0 && visible[0].right == 250 &&
          visible[0].top == 300 && visible[0].right_top == 400 &&
          visible[1].left == 750 && visible[1].right == 1000 &&
          visible[1].top == 600 && visible[1].right_top == 700,
          "sloped floor is clipped at both window height crossings"); ++checks;
    check(visible[0].generation == 88 && visible[1].edge_id == 7 &&
          visible[0].contour_id == 1 && visible[1].source_left == 0 &&
          visible[0].source_right == 1000 && visible[1].source_top == 300 &&
          visible[0].source_right_top == 700,
          "slope fragments preserve immutable geometry and contour identity"); ++checks;
    auto rising = edges;
    rising[0].top = rising[0].source_top = 700;
    rising[0].right_top = rising[0].source_right_top = 300;
    const auto reverse = mapping::clip_edges(rising, windows);
    check(reverse.size() == 2 && reverse[0].left == 0 && reverse[0].right == 250 &&
          reverse[0].top == 700 && reverse[0].right_top == 600 &&
          reverse[1].left == 750 && reverse[1].top == 400 && reverse[1].right_top == 300,
          "negative slopes use the same rectangular occlusion rule"); ++checks;
    const std::array below{Window{12, {0, 701, 1000, 900}}};
    const std::array above{Window{13, {0, 0, 1000, 300}}};
    check(mapping::clip_edges(edges, below).size() == 1 &&
          mapping::clip_edges(edges, above).size() == 1,
          "windows wholly above or below a slope leave it available"); ++checks;
  }
  {
    const std::array edges{AuroraDesktopWallpaperEdge{
        90, 2, 0, 100, 20, 53, 2, 0, 100, 20, 53}};
    const std::array first{Window{1, {0, 0, 17, 100}}};
    const auto a = mapping::clip_edges(edges, first);
    const std::array second{Window{2, {60, 0, 100, 100}}};
    const auto b = mapping::clip_edges(a, second);
    check(b.size() == 1 && b[0].left == 17 && b[0].right == 60 &&
          b[0].top == 26 && b[0].right_top == 40 && b[0].source_top == 20 &&
          b[0].source_right_top == 53,
          "successive clipping evaluates the original slope without rounding drift"); ++checks;
    const std::array fractional{Window{3, {0, 25, 100, 40}}};
    const auto crossed = mapping::clip_edges(edges, fractional);
    auto reverse = edges;
    reverse[0].top = reverse[0].source_top = 53;
    reverse[0].right_top = reverse[0].source_right_top = 20;
    const auto reversed = mapping::clip_edges(reverse, fractional);
    check(crossed.size() == 2 && crossed[0].right == 15 && crossed[1].left == 61 &&
          reversed.size() == 2 && reversed[0].right == 39 && reversed[1].left == 85,
          "fractional crossings conservatively hide covered pixels for either slope sign"); ++checks;
    const std::array short_edge{AuroraDesktopWallpaperEdge{
        90, 3, 0, 12, 53, 56, 2, 0, 12, 53, 56}};
    check(mapping::clip_edges(short_edge, {}).size() == 1,
          "short contour segments survive so a continuous hillside has no deleted joins"); ++checks;
    auto invalid = edges;
    invalid[0].source_right = invalid[0].source_left;
    check(mapping::clip_edges(invalid, {}).empty(),
          "degenerate source segments never create collision"); ++checks;
  }
  {
    /* Compare each pixel-center with an independent line/rectangle oracle.
     * Integral crossing positions isolate topology from subpixel rounding. */
    uint32_t random = 0x91b82a63u;
    const auto next = [&]() { random = random * 1664525u + 1013904223u; return random; };
    for (unsigned scene = 0; scene < 96; ++scene) {
      const int top = scene % 2 ? 400 : 100;
      const int end = scene % 2 ? 200 : 300;
      const std::array edges{AuroraDesktopWallpaperEdge{
          100, 1, 0, 400, top, end, 1, 0, 400, top, end}};
      std::array<Window, 6> windows;
      for (auto& window : windows) {
        const int x = static_cast<int>(next() % 360);
        const int y = static_cast<int>(next() % 440);
        window = {next(), {x, y, x + 10 + static_cast<int>(next() % 90),
                                y + 10 + static_cast<int>(next() % 150)}};
      }
      std::array<bool, 400> expected{};
      for (int x = 0; x < 400; ++x) {
        const double px = x + 0.5;
        const double py = top + px * (end - top) / 400;
        expected[x] = std::none_of(windows.begin(), windows.end(), [&](const auto& window) {
          const auto& b = window.bounds;
          return px >= b.left && px < b.right && py >= b.top && py < b.bottom;
        });
      }
      // Contour fragments shorter than two physical pixels are intentionally omitted.
      for (int x = 0; x < 400; ++x) {
        if (expected[x] && (x == 0 || !expected[x - 1]) && (x == 399 || !expected[x + 1]))
          expected[x] = false;
      }
      const auto visible = mapping::clip_edges(edges, windows);
      for (int x = 0; x < 400; ++x) {
        const double px = x + 0.5;
        const bool actual = std::any_of(visible.begin(), visible.end(), [&](const auto& edge) {
          return px >= edge.left && px < edge.right;
        });
        check(actual == expected[x], "sloped occlusion matches an independent per-pixel oracle");
      }
    }
    ++checks;
  }
  std::printf("Desktop wallpaper mapping: %u checks passed\n", checks);
}
