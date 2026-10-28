#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace aurora::desktop::pixels {
struct Size { uint32_t width, height; };

inline Size render_size(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) return {};
  const double scale = std::min({1.0, 1920.0 / width, 1080.0 / height});
  return {std::max(1u, static_cast<uint32_t>(width * scale)),
          std::max(1u, static_cast<uint32_t>(height * scale))};
}

/* The desktop GX blend path already accumulates premultiplied RGB. Clamp
 * additive effects to coverage so UpdateLayeredWindow never receives an
 * invalid premultiplied pixel. Do NOT multiply by alpha a second time. */
inline void copy_bgra(uint8_t* dst, const uint8_t* src, uint32_t width,
                      uint32_t height, uint32_t source_stride, bool source_is_bgra) {
  for (uint32_t y = 0; y < height; ++y) {
    const auto* row = src + static_cast<size_t>(y) * source_stride;
    auto* out = dst + static_cast<size_t>(y) * width * 4;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t a = row[3];
      out[0] = std::min(row[source_is_bgra ? 0 : 2], a);
      out[1] = std::min(row[1], a);
      out[2] = std::min(row[source_is_bgra ? 2 : 0], a);
      out[3] = a;
      row += 4;
      out += 4;
    }
  }
}

/* Scaling is applied equally to color and alpha. For equal dimensions this
 * is a single copy. Nearest sampling keeps premultiplied pixels valid. */
inline void scale_bgra(uint8_t* dst, uint32_t dw, uint32_t dh,
                       const uint8_t* src, uint32_t sw, uint32_t sh) {
  if (!dw || !dh || !sw || !sh) return;
  if (dw == sw && dh == sh) {
    std::memcpy(dst, src, static_cast<size_t>(sw) * sh * 4);
    return;
  }
  for (uint32_t y = 0; y < dh; ++y) {
    const uint32_t sy = static_cast<uint64_t>(y) * sh / dh;
    auto* out = dst + static_cast<size_t>(y) * dw * 4;
    const auto* row = src + static_cast<size_t>(sy) * sw * 4;
    for (uint32_t x = 0; x < dw; ++x) {
      const uint32_t sx = static_cast<uint64_t>(x) * sw / dw;
      std::memcpy(out + x * 4, row + sx * 4, 4);
    }
  }
}
} // namespace aurora::desktop::pixels
