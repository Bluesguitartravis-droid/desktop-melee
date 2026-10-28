// Standalone: c++ -std=c++20 -O2 desktop_pixels_test.cpp -o desktop_pixels_test
#include "../lib/desktop_pixels.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool value, const char* label) {
  if (!value) {
    std::fprintf(stderr, "FAIL: %s\n", label);
    std::exit(1);
  }
}
}

int main() {
  using namespace aurora::desktop::pixels;
  const auto normal = render_size(1920, 1080);
  check(normal.width == 1920 && normal.height == 1080, "1080p stays native");
  const auto retina = render_size(3840, 2160);
  check(retina.width == 1920 && retina.height == 1080, "4K fits readback budget");
  const auto portrait = render_size(1440, 2560);
  check(portrait.width == 607 && portrait.height == 1080, "portrait aspect fits height budget");
  check(render_size(0, 1080).width == 0, "zero-size does not divide by zero");

  // Padding deliberately contains opaque colors: none may leak into output.
  const std::array<uint8_t, 24> rgba{
      80, 40, 20, 128, 90, 10, 200, 0, 255, 255, 255, 255,
      20, 40, 80, 255, 200, 180, 160, 64, 255, 255, 255, 255};
  std::array<uint8_t, 16> out{};
  copy_bgra(out.data(), rgba.data(), 2, 2, 12, false);
  check(out == std::array<uint8_t, 16>{20,40,80,128, 0,0,0,0,
                                      80,40,20,255, 64,64,64,64},
         "RGBA swizzle, transparent RGB, padded stride and additive clamp");
  const auto premultiplied = out;
  copy_bgra(out.data(), premultiplied.data(), 2, 2, 8, true);
  check(out == premultiplied, "valid premultiplied BGRA is not multiplied twice");

  std::array<uint8_t, 64> scaled{};
  scale_bgra(scaled.data(), 4, 4, out.data(), 2, 2);
  check(scaled[0] == 20 && scaled[3] == 128 && scaled[7] == 128 &&
         scaled[11] == 0 && scaled[32] == 80 && scaled[63] == 64,
         "color and coverage follow identical pixel scaling");
  std::puts("Desktop pixels: 7 checks passed");
  return 0;
}
