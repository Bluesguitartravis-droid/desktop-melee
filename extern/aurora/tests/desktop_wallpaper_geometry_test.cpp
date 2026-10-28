/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../lib/desktop_wallpaper_geometry.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

using aurora::desktop::wallpaper_geometry::detect_ledges;
using aurora::desktop::wallpaper_geometry::Ledge;

namespace {
constexpr uint32_t Width = 320, Height = 180;
using RGB = std::array<uint8_t, 3>;
struct Picture {
  std::vector<uint8_t> rgba = std::vector<uint8_t>(Width * Height * 4);
  Picture(RGB background = {20, 35, 65}) { rectangle(0, 0, Width, Height, background); }
  void rectangle(int left, int top, int right, int bottom, RGB color) {
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
      const size_t i = (y * Width + x) * 4;
      rgba[i] = color[0]; rgba[i + 1] = color[1]; rgba[i + 2] = color[2]; rgba[i + 3] = 255;
    }
  }
  auto ledges(uint32_t capacity = 32) const {
    return detect_ledges(rgba, Width, Height, 1920, 1080, 1040, capacity);
  }
};
void check(bool condition, const char* description) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
bool contains(const std::vector<Ledge>& edges, int left, int right, int top, int tolerance = 0) {
  return std::any_of(edges.begin(), edges.end(), [&](const Ledge& edge) {
    return std::abs(edge.left - left) <= tolerance && std::abs(edge.right - right) <= tolerance &&
           std::abs(edge.top - top) <= tolerance && std::abs(edge.right_top - top) <= tolerance;
  });
}
}

int main() {
  unsigned checks = 0;
  {
    Picture image;
    check(image.ledges().empty(), "flat wallpaper creates no platforms"); ++checks;
    image.rectangle(20, 120, 120, 140, {220, 180, 65});
    image.rectangle(160, 80, 280, 100, {65, 220, 140});
    const auto edges = image.ledges();
    check(edges.size() == 2 && contains(edges, 120, 720, 720) &&
          contains(edges, 960, 1680, 480), "two solid ledges map to exact screen coordinates"); ++checks;
    check(!contains(edges, 120, 720, 840) && !contains(edges, 960, 1680, 600),
          "solid shapes' undersides are not converted to floors"); ++checks;
  }
  {
    Picture image({245, 240, 225});
    image.rectangle(30, 95, 230, 120, {30, 40, 50});
    check(contains(image.ledges(), 180, 1380, 570),
          "dark platforms on a light background work too"); ++checks;
  }
  {
    Picture image;
    for (int x = 0; x < static_cast<int>(Width); ++x) {
      const uint8_t level = static_cast<uint8_t>(x % 100);
      image.rectangle(x, 0, x + 1, Height, {level, level, level});
    }
    image.rectangle(20, 100, 300, 135, {240, 240, 240});
    check(contains(image.ledges(), 120, 1800, 600),
          "strong broad ledges survive the nonuniform-background fallback"); ++checks;
  }
  {
    Picture image;
    for (int x = 30; x < 150; ++x) {
      const int top = 55 + (x - 30) / 3;
      image.rectangle(x, top, x + 1, 130, {230, 150, 40});
    }
    check(image.ledges().empty(), "a slope is not turned into an invisible horizontal bridge"); ++checks;
  }
  {
    Picture image;
    image.rectangle(20, 60, 25, 100, {240, 240, 240});
    image.rectangle(45, 60, 220, 61, {240, 240, 240});
    check(image.ledges().empty(), "narrow details and thin decorative strokes are rejected"); ++checks;
  }
  {
    Picture image;
    for (int y = 0; y < static_cast<int>(Height); ++y) {
      const auto level = static_cast<uint8_t>(20 + y);
      image.rectangle(0, y, Width, y + 1, {level, level, level});
    }
    check(image.ledges().empty(), "a smooth gradient creates no platforms"); ++checks;
  }
  {
    Picture image;
    uint32_t seed = 1234567;
    for (size_t i = 0; i < image.rgba.size(); i += 4) {
      seed = seed * 1664525U + 1013904223U;
      image.rgba[i] = seed >> 24; image.rgba[i + 1] = seed >> 16; image.rgba[i + 2] = seed >> 8;
    }
    check(image.ledges().empty(), "unstructured visual noise creates no long solid platforms"); ++checks;
  }
  {
    Picture image;
    image.rectangle(20, 100, 280, 130, {230, 210, 90});
    image.rectangle(20, 100, 280, 101, {90, 90, 75});
    image.rectangle(20, 101, 280, 102, {160, 150, 85});
    const auto edges = image.ledges();
    check(edges.size() == 1 && contains(edges, 120, 1680, 600, 12),
          "softened edges collapse into one nearby platform"); ++checks;
  }
  {
    Picture image;
    image.rectangle(20, 75, 130, 95, {230, 210, 90});
    image.rectangle(132, 75, 260, 95, {230, 210, 90});
    check(contains(image.ledges(), 120, 1560, 450),
          "tiny texture gaps do not fragment a continuous ledge"); ++checks;
    image.rectangle(130, 75, 160, 95, {20, 35, 65});
    const auto edges = image.ledges();
    check(edges.size() == 2 && contains(edges, 120, 780, 450) &&
          contains(edges, 960, 1560, 450), "real gaps remain separate landing surfaces"); ++checks;
  }
  {
    Picture image;
    for (int i = 0; i < 7; ++i)
      image.rectangle(10 + i * 35, 55 + i * 15, 40 + i * 35, 64 + i * 15, {230, 210, 90});
    check(image.ledges(3).size() == 3 && image.ledges(0).empty(), "capacity is enforced"); ++checks;
    const auto edges = detect_ledges(image.rgba, Width, Height, 2560, 1440, 1380);
    check(contains(edges, 80, 320, 440), "physical projection follows another monitor resolution"); ++checks;
    check(detect_ledges(std::span<const uint8_t>(image.rgba).first(5), Width, Height,
                        1920, 1080, 1040).empty(), "truncated pixel input is rejected"); ++checks;
  }

  {
    /* The geometry is specified analytically, independently of segmentation.
     * Purple earth also prevents accidentally implementing a green-grass test. */
    Picture image({40, 95, 175});
    std::array<int, Width> skyline{};
    for (uint32_t x = 0; x < Width; ++x) {
      const float phase = static_cast<float>(x) / Width * 5.1f;
      skyline[x] = 100 + static_cast<int>(17 * std::sin(phase));
      image.rectangle(x, skyline[x], x + 1, Height, {120, 45, 125});
    }
    const auto edges = image.ledges();
    check(edges.size() > 2 && edges.size() <= 24 && edges.front().left == 0 &&
          edges.back().right == 1920, "a non-green curved hillside spans the whole image"); ++checks;
    int covered = 0;
    for (const auto& edge : edges) {
      check(edge.contour_id != 0, "base segments have a contour identity");
      for (int x = edge.left; x < edge.right; ++x) {
        const float y = edge.top + static_cast<float>(edge.right_top - edge.top) *
                                      (x - edge.left) / (edge.right - edge.left);
        check(std::fabs(y - skyline[static_cast<size_t>(x) / 6] * 6) <= 15,
              "simplified slopes remain within 2.5 analysis pixels of the known boundary");
        ++covered;
      }
    }
    check(covered == 1920, "curved terrain has neither hidden gaps nor overlapping segments"); ++checks;
    for (size_t i = 1; i < edges.size(); ++i)
      check(edges[i - 1].right == edges[i].left && edges[i - 1].right_top == edges[i].top &&
            edges[i - 1].contour_id == edges[i].contour_id,
            "adjacent slope endpoints and contour identities match exactly");
    ++checks;
    const auto scaled = detect_ledges(image.rgba, Width, Height, 2560, 1440, 1380);
    check(scaled.size() == edges.size() && scaled.front().left == 0 &&
          scaled.back().right == 2560, "slopes project to another physical monitor size"); ++checks;
  }
  {
    Picture image({50, 120, 210});
    for (uint32_t x = 0; x < Width; ++x)
      image.rectangle(x, 100 + static_cast<int>(x / 12), x + 1, Height, {180, 100, 35});
    image.rectangle(30, 55, 140, 70, {150, 70, 25});
    const auto edges = image.ledges();
    check(contains(edges, 180, 840, 330),
          "clear floating ledges coexist above a connected terrain silhouette"); ++checks;
    check(std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
            return edge.contour_id && edge.right_top != edge.top;
          }), "an orange landscape supplies genuine sloping collision"); ++checks;
    check(std::none_of(edges.begin(), edges.end(), [](const auto& edge) {
            return !edge.contour_id && edge.top > 600;
          }), "terrain texture and internal edges do not create duplicate floors"); ++checks;
  }
  {
    /* A varied sky is a photograph-style case. Some clouds have colors absent
     * from the top seed strip and must still not turn into floating platforms. */
    Picture image;
    for (uint32_t y = 0; y < Height; ++y) for (uint32_t x = 0; x < Width; ++x) {
      const auto r = static_cast<uint8_t>(30 + x * 110 / Width);
      const auto g = static_cast<uint8_t>(80 + y * 80 / Height);
      image.rectangle(x, y, x + 1, y + 1, {r, g, 210});
    }
    image.rectangle(30, 50, 150, 62, {245, 245, 245});
    image.rectangle(170, 70, 290, 80, {220, 225, 230});
    for (uint32_t x = 0; x < Width; ++x) {
      const int top = 110 + static_cast<int>(x / 16);
      image.rectangle(x, top, x + 1, Height, {100, 150, 35});
      image.rectangle(x, top + 15, x + 1, top + 19, {50, 70, 20});
    }
    const auto edges = image.ledges();
    check(!edges.empty() && edges.front().left == 0 && edges.back().right == 1920 &&
          std::all_of(edges.begin(), edges.end(), [](const auto& edge) {
            return edge.contour_id && edge.top >= 650 && edge.right_top >= 650;
          }), "clouds and grass texture do not acquire collision above or inside a landscape"); ++checks;
  }
  {
    Picture image({20, 35, 65});
    image.rectangle(0, 100, 140, Height, {220, 150, 70});
    image.rectangle(180, 110, Width, Height, {220, 150, 70});
    const auto edges = image.ledges();
    check(edges.size() == 2 && edges[0].left == 0 && edges[0].right == 840 &&
          edges[1].left == 1080 && edges[1].right == 1920 &&
          edges[0].contour_id != edges[1].contour_id,
          "a real gap through the terrain remains open and separates contour identities"); ++checks;
  }
  {
    Picture image;
    for (uint32_t y = 0; y < Height; ++y) {
      const auto level = static_cast<uint8_t>(y * 255 / (Height - 1));
      image.rectangle(0, y, Width, y + 1, {level, level, level});
    }
    check(image.ledges().empty(), "distinct top and bottom colors alone cannot turn a gradient into terrain"); ++checks;
  }
  {
    Picture image;
    for (uint32_t x = 0; x < Width; ++x)
      image.rectangle(x, 95 + static_cast<int>(15 * std::sin(x * 0.02)), x + 1,
                      Height, {200, 140, 30});
    const auto edges = image.ledges(4);
    check(edges.size() <= 4 && image.ledges(0).empty(), "silhouette simplification honors the shared segment capacity"); ++checks;
  }

  {
    Picture image({7, 7, 7});
    image.rectangle(0, 20, Width, 160, {70, 145, 225});
    for (uint32_t x = 0; x < Width; ++x)
      image.rectangle(x, 90 + static_cast<int>(x / 16), x + 1, 160, {95, 140, 30});
    const auto edges = image.ledges();
    check(!edges.empty() && edges.front().left == 0 && edges.back().right == 1920 &&
          std::all_of(edges.begin(), edges.end(), [](const auto& edge) {
            return edge.contour_id && edge.top >= 530 && edge.right_top >= 530 &&
                   edge.top < 700 && edge.right_top < 700;
          }), "symmetric letterbox bars are excluded without creating collision at the image border"); ++checks;
  }

  {
    Picture image({7, 7, 7});
    for (uint32_t y = 20; y < 160; ++y) for (uint32_t x = 0; x < Width; ++x)
      image.rectangle(x, y, x + 1, y + 1,
                      {static_cast<uint8_t>(30 + x * 110 / Width),
                       static_cast<uint8_t>(80 + y * 80 / Height), 210});
    image.rectangle(30, 50, 150, 62, {245, 245, 245});
    for (uint32_t x = 0; x < Width; ++x)
      image.rectangle(x, 90 + static_cast<int>(x / 16), x + 1, 160, {95, 140, 30});
    const auto edges = image.ledges();
    check(!edges.empty() && std::all_of(edges.begin(), edges.end(), [](const auto& edge) {
            return edge.contour_id && edge.top >= 530 && edge.right_top >= 530;
          }), "letterbox bars do not make a photographic sky appear to be flat-background art"); ++checks;
  }

  {
    Picture image({30, 75, 160});
    image.rectangle(0, 90, 160, Height, {180, 110, 40});
    image.rectangle(160, 130, Width, Height, {180, 110, 40});
    const auto edges = image.ledges();
    check(edges.size() == 2 && contains(edges, 0, 960, 540) &&
          contains(edges, 960, 1920, 780) && edges[0].contour_id != edges[1].contour_id,
          "an abrupt cliff splits contours instead of adding a diagonal bridge through empty space"); ++checks;
  }
  std::printf("Desktop wallpaper geometry: %u checks passed\n", checks);
}
