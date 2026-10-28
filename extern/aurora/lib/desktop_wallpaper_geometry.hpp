/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace aurora::desktop::wallpaper_geometry {

struct Ledge {
  int32_t left, right, top, right_top;
  float confidence;
  uint32_t contour_id;
};

namespace detail {
struct Color { float r, g, b; };
struct Candidate { int32_t left, right, top; float strength, density; };

inline float difference(const Color& a, const Color& b) {
  return (std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b)) / 3.0f;
}

inline Color pixel(std::span<const uint8_t> rgba, uint32_t width, uint32_t x, uint32_t y) {
  const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
  return {static_cast<float>(rgba[offset]), static_cast<float>(rgba[offset + 1]),
          static_cast<float>(rgba[offset + 2])};
}

inline Color band(std::span<const uint8_t> rgba, uint32_t width, uint32_t x, uint32_t y) {
  Color result{};
  for (uint32_t k = 0; k < 3; ++k) {
    const Color value = pixel(rgba, width, x, y + k);
    result.r += value.r;
    result.g += value.g;
    result.b += value.b;
  }
  return {result.r / 3.0f, result.g / 3.0f, result.b / 3.0f};
}

/* A flat sky/background in the upper quarter makes silhouette detection
 * reliable. If it is absent, the fallback accepts only long, strong edges;
 * it does not pretend to infer the semantics of arbitrary photographs. */
inline Color background(std::span<const uint8_t> rgba, uint32_t width,
                        uint32_t first_row, uint32_t last_row, bool& reliable) {
  struct Bin { uint32_t count{}, r{}, g{}, b{}; };
  std::array<Bin, 4096> histogram{};
  const uint32_t rows = std::max<uint32_t>(1, (last_row - first_row) / 4);
  for (uint32_t y = first_row; y < first_row + rows; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
      const uint8_t r = rgba[offset], g = rgba[offset + 1], b = rgba[offset + 2];
      auto& bin = histogram[(r >> 4) * 256 + (g >> 4) * 16 + (b >> 4)];
      ++bin.count;
      bin.r += r; bin.g += g; bin.b += b;
    }
  }
  const auto best = std::max_element(histogram.begin(), histogram.end(),
      [](const Bin& a, const Bin& b) { return a.count < b.count; });
  reliable = best->count >= static_cast<double>(width) * rows * 0.22;
  const float count = static_cast<float>(std::max<uint32_t>(1, best->count));
  return {best->r / count, best->g / count, best->b / count};
}

/* A small, deterministic color model avoids a sky-color or grass-color rule.
 * The upper/lower fifths supply weighted colors; distant samples and a few
 * Lloyd iterations preserve clouds as background and shadows as foreground. */
struct Palette {
  std::array<Color, 8> colors{};
  uint32_t count = 0;
};
inline float distance_squared(const Color& a, const Color& b) {
  const float r = a.r - b.r, g = a.g - b.g, blue = a.b - b.b;
  return r * r + g * g + blue * blue;
}
inline float nearest_squared(const Color& color, const Palette& palette) {
  float best = std::numeric_limits<float>::max();
  for (uint32_t i = 0; i < palette.count; ++i)
    best = std::min(best, distance_squared(color, palette.colors[i]));
  return best;
}
inline float palette_difference(const Color& color, const Palette& palette) {
  float best = std::numeric_limits<float>::max();
  for (uint32_t i = 0; i < palette.count; ++i)
    best = std::min(best, difference(color, palette.colors[i]));
  return best;
}
inline Palette palette(std::span<const uint8_t> rgba, uint32_t width,
                       uint32_t first_row, uint32_t last_row) {
  struct Bin { uint32_t count{}; Color sum{}; };
  std::array<Bin, 4096> bins{};
  for (uint32_t y = first_row; y < last_row; y += 2) {
    for (uint32_t x = 0; x < width; x += 2) {
      const Color value = pixel(rgba, width, x, y);
      const uint32_t index = (static_cast<uint32_t>(value.r) >> 4) * 256 +
                            (static_cast<uint32_t>(value.g) >> 4) * 16 +
                            (static_cast<uint32_t>(value.b) >> 4);
      auto& bin = bins[index];
      ++bin.count;
      bin.sum.r += value.r; bin.sum.g += value.g; bin.sum.b += value.b;
    }
  }
  std::vector<Bin> used;
  for (auto bin : bins) {
    if (!bin.count) continue;
    bin.sum.r /= bin.count; bin.sum.g /= bin.count; bin.sum.b /= bin.count;
    used.push_back(bin);
  }
  Palette result;
  if (used.empty()) return result;
  const auto initial = std::max_element(used.begin(), used.end(),
      [](const Bin& a, const Bin& b) { return a.count < b.count; });
  result.colors[result.count++] = initial->sum;
  while (result.count < result.colors.size()) {
    const Bin* best = nullptr;
    float best_weight = 0;
    for (const auto& bin : used) {
      const float distance = nearest_squared(bin.sum, result);
      const float weight = distance * bin.count;
      if (distance >= 64 && weight > best_weight) { best = &bin; best_weight = weight; }
    }
    if (!best) break;
    result.colors[result.count++] = best->sum;
  }
  for (uint32_t iteration = 0; iteration < 6; ++iteration) {
    std::array<Bin, 8> clusters{};
    for (const auto& bin : used) {
      uint32_t nearest = 0;
      float best = distance_squared(bin.sum, result.colors[0]);
      for (uint32_t i = 1; i < result.count; ++i) {
        const float distance = distance_squared(bin.sum, result.colors[i]);
        if (distance < best) { best = distance; nearest = i; }
      }
      auto& cluster = clusters[nearest];
      cluster.count += bin.count;
      cluster.sum.r += bin.sum.r * bin.count;
      cluster.sum.g += bin.sum.g * bin.count;
      cluster.sum.b += bin.sum.b * bin.count;
    }
    for (uint32_t i = 0; i < result.count; ++i) {
      const auto& cluster = clusters[i];
      if (cluster.count)
        result.colors[i] = {cluster.sum.r / cluster.count, cluster.sum.g / cluster.count,
                            cluster.sum.b / cluster.count};
    }
  }
  return result;
}

struct ContentRows { uint32_t first, last; };

/* Fit/Center composition can add identical, symmetric solid-color bars.
 * Exclude only unambiguous broad framing: the content occupies at least half
 * the image and its first/last rows differ across nearly the entire width.
 * A small floating rectangle on a flat background must retain its top edge. */
inline ContentRows content_rows(std::span<const uint8_t> rgba, uint32_t width, uint32_t height) {
  const ContentRows full{0, height};
  const Color border = pixel(rgba, width, 0, 0);
  if (difference(pixel(rgba, width, 0, height - 1), border) > 1.0f) return full;
  const auto uniform = [&](uint32_t y) {
    for (uint32_t x = 0; x < width; ++x)
      if (difference(pixel(rgba, width, x, y), border) > 1.0f) return false;
    return true;
  };
  uint32_t first = 0, last = height;
  while (first < height / 4 && uniform(first)) ++first;
  while (last > height * 3 / 4 && uniform(last - 1)) --last;
  if (first < 2 || height - last < 2 || std::abs(static_cast<int>(first + last) -
      static_cast<int>(height)) > 2 || last - first < height / 2) return full;
  const auto content = [&](uint32_t y) {
    uint32_t changed = 0;
    for (uint32_t x = 0; x < width; ++x)
      changed += difference(pixel(rgba, width, x, y), border) > 12.0f;
    return changed * 10 >= width * 9;
  };
  return content(first) && content(last - 1) ? ContentRows{first, last} : full;
}

struct Silhouette {
  Palette background;
  std::vector<int32_t> rows;
  std::vector<Ledge> segments; // analysis-image coordinates until final projection
};

/* Trace only foreground connected to the image's bottom edge. This excludes
 * floating clouds without treating a photograph as a semantic scene model.
 * A sharp, broad, coherent boundary is required: a color gradient or textured
 * noise must not become an arbitrary floor. Independent platforms are handled
 * by the conservative edge detector below. */
inline Silhouette silhouette(std::span<const uint8_t> rgba, uint32_t width, uint32_t height,
                             int32_t floor_row, uint32_t capacity, ContentRows content) {
  Silhouette result;
  const uint32_t seed_rows = std::max(2u, (content.last - content.first) / 5);
  result.background = palette(rgba, width, content.first, content.first + seed_rows);
  const auto foreground = palette(rgba, width, content.last - seed_rows, content.last);
  if (!result.background.count || !foreground.count || !capacity) return result;
  const size_t size = static_cast<size_t>(width) * height;
  std::vector<uint8_t> ground(size, 0);
  for (uint32_t y = content.first; y < content.last; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const Color color = pixel(rgba, width, x, y);
      const float bg = std::sqrt(nearest_squared(color, result.background));
      const float fg = std::sqrt(nearest_squared(color, foreground));
      ground[static_cast<size_t>(y) * width + x] = bg > fg + 12.0f ? 1 : 0;
    }
  }
  std::vector<uint32_t> queue;
  queue.reserve(size / 2);
  const auto visit = [&](uint32_t index) {
    if (ground[index] == 1) { ground[index] = 2; queue.push_back(index); }
  };
  for (uint32_t x = 0; x < width; ++x) visit((content.last - 1) * width + x);
  for (size_t i = 0; i < queue.size(); ++i) {
    const uint32_t index = queue[i], x = index % width;
    if (x) visit(index - 1);
    if (x + 1 < width) visit(index + 1);
    if (index >= width) visit(index - width);
    if (index + width < size) visit(index + width);
  }
  if (queue.size() < static_cast<size_t>(width) * (content.last - content.first) / 12) return result;
  std::vector<int32_t> rows(width, -1);
  uint32_t covered = 0, strong = 0, rough = 0;
  for (uint32_t x = 0; x < width; ++x) {
    for (uint32_t y = content.first + 3; y + 5 < content.last &&
         static_cast<int32_t>(y) < floor_row - 2; ++y) {
      if (ground[static_cast<size_t>(y) * width + x] != 2) continue;
      /* If the component already reaches the top, this is not an identifiable
       * sky/terrain transition and should not acquire a floor at row3. */
      if (ground[(content.first + 2) * width + x] == 2) break;
      rows[x] = static_cast<int32_t>(y);
      ++covered;
      if (difference(band(rgba, width, x, y - 3), band(rgba, width, x, y)) >= 24.0f)
        ++strong;
      if (x && rows[x - 1] >= 0 && std::abs(rows[x] - rows[x - 1]) > 4) ++rough;
      break;
    }
  }
  if (covered * 5 < width * 3 || strong * 5 < covered * 4 || rough * 20 > covered)
    return result;

  /* Median smoothing removes individual antialiasing/texture outliers without
   * filling real gaps. Every endpoint is shared exactly after projection. */
  const auto raw = rows;
  for (uint32_t x = 2; x + 2 < width; ++x) {
    std::array<int32_t, 5> neighborhood{};
    bool valid = true;
    for (uint32_t k = 0; k < neighborhood.size(); ++k) {
      neighborhood[k] = raw[x + k - 2];
      valid &= neighborhood[k] >= 0;
    }
    if (valid) {
      std::sort(neighborhood.begin(), neighborhood.end());
      rows[x] = neighborhood[2];
    }
  }

  struct Run { uint32_t left, right; };
  std::vector<Run> runs;
  for (uint32_t x = 0; x < width;) {
    if (rows[x] < 0) { ++x; continue; }
    const uint32_t left = x++;
    while (x < width && rows[x] >= 0 && std::abs(rows[x] - rows[x - 1]) <= 4) ++x;
    if (x - left >= std::max(12u, width / 12)) runs.push_back({left, x});
  }
  const uint32_t budget = std::min(24u, capacity);
  if (runs.empty() || runs.size() > budget) return result;
  struct Piece { uint32_t left, right; int32_t a, b; uint32_t contour; float error; uint32_t split; };
  const auto make_piece = [&](uint32_t left, uint32_t right, uint32_t contour) {
    const uint32_t endpoint = right == runs[contour - 1].right ? right - 1 : right;
    Piece piece{left, right, rows[left], rows[endpoint], contour, 0, left};
    for (uint32_t x = left + 1; x < right; ++x) {
      const float y = piece.a + static_cast<float>(piece.b - piece.a) * (x - left) / (right - left);
      const float error = std::fabs(rows[x] - y);
      if (error > piece.error) { piece.error = error; piece.split = x; }
    }
    return piece;
  };
  std::vector<Piece> pieces;
  uint32_t contour = 0;
  for (const auto& run : runs) pieces.push_back(make_piece(run.left, run.right, ++contour));
  while (pieces.size() < budget) {
    const auto worst = std::max_element(pieces.begin(), pieces.end(),
        [](const Piece& a, const Piece& b) { return a.error < b.error; });
    if (worst == pieces.end() || worst->error <= 1.25f) break;
    const Piece old = *worst;
    *worst = make_piece(old.left, old.split, old.contour);
    pieces.push_back(make_piece(old.split, old.right, old.contour));
  }
  std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.left < b.left; });
  uint32_t retained_width = 0;
  for (const auto& piece : pieces) {
    /* Refuse a line that would bridge a large unmodelled terrain feature when
     * the segment budget is exhausted. Missing collision is preferable to a
     * long floor detached from the visible silhouette. */
    if (piece.error > 3.0f || std::abs(piece.b - piece.a) > static_cast<int32_t>(piece.right - piece.left))
      continue;
    result.segments.push_back({static_cast<int32_t>(piece.left), static_cast<int32_t>(piece.right),
                               piece.a, piece.b, 0.9f, piece.contour});
    retained_width += piece.right - piece.left;
  }
  if (retained_width * 5 < width * 3) { result.segments.clear(); return result; }
  result.rows = std::move(rows);
  return result;
}
} // namespace detail

/* Analyze a tightly packed RGBA8 image that has already been composed using
 * Windows' wallpaper placement. Input is deliberately bounded: callers use
 * at most 640x360. Results are physical primary-monitor coordinates. This is
 * bounded silhouette and horizontal-edge extraction, not a general scene
 * recognizer. A connected bottom foreground can supply slopes; independent
 * solid ledges use color/background affinity to reject their undersides.
 */
inline std::vector<Ledge> detect_ledges(
    std::span<const uint8_t> rgba, uint32_t width, uint32_t height,
    uint32_t screen_width, uint32_t screen_height, int32_t floor_top,
    uint32_t capacity = 32) {
  std::vector<Ledge> result;
  capacity = std::min<uint32_t>(capacity, 32);
  if (width < 12 || height < 10 || width > 2048 || height > 2048 ||
      !screen_width || !screen_height || screen_width > INT32_MAX ||
      screen_height > INT32_MAX || floor_top <= 0 || capacity == 0 ||
      rgba.size() < static_cast<size_t>(width) * height * 4)
    return result;

  const auto content = detail::content_rows(rgba, width, height);
  bool reliable_background = false;
  const auto background = detail::background(rgba, width, content.first, content.last, reliable_background);
  const uint32_t minimum_width = std::max<uint32_t>(12,
      reliable_background ? width / 24 : width / 10);
  const float threshold = reliable_background ? 24.0f : 48.0f;
  const int32_t floor_row = static_cast<int32_t>(std::min<int64_t>(height,
      static_cast<int64_t>(floor_top) * height / screen_height));
  const int32_t last_row = std::min<int32_t>(content.last - 5, floor_row - 2);
  const auto terrain = detail::silhouette(rgba, width, height, floor_row, capacity, content);
  const bool has_terrain = !terrain.segments.empty();
  std::vector<detail::Candidate> candidates;
  std::vector<float> strength(width);

  /* Photographs with a convincing base contour use that contour alone. A
   * second generic edge pass would turn unmodelled clouds or scenery into
   * floating floors. Clear flat-background art may also have separate ledges. */
  for (int32_t y = static_cast<int32_t>(content.first) + 3;
       y < last_row && (!has_terrain || reliable_background); ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const auto above = detail::band(rgba, width, x, y - 3);
      const auto below = detail::band(rgba, width, x, y);
      const float contrast = detail::difference(above, below);
      const float immediate = detail::difference(detail::pixel(rgba, width, x, y - 1),
                                                  detail::pixel(rgba, width, x, y));
      bool accepted = contrast >= threshold && immediate >= std::max(8.0f, contrast * 0.20f);
      if (accepted && has_terrain) {
        const float upper_distance = detail::palette_difference(above, terrain.background);
        const float lower_distance = detail::palette_difference(below, terrain.background);
        const float body_distance = detail::palette_difference(
            detail::pixel(rgba, width, x, y + 4), terrain.background);
        accepted = terrain.rows[x] >= 0 && y + 3 < terrain.rows[x] &&
                   lower_distance >= upper_distance + 12.0f &&
                   lower_distance >= 28.0f && body_distance >= 20.0f;
      } else if (accepted && reliable_background) {
        const float upper_distance = detail::difference(above, background);
        const float lower_distance = detail::difference(below, background);
        const float body_distance = detail::difference(detail::pixel(rgba, width, x, y + 4),
                                                        background);
        accepted = lower_distance >= upper_distance + 12.0f &&
                   lower_distance >= 28.0f && body_distance >= 20.0f;
      }
      strength[x] = accepted ? contrast : 0.0f;
    }

    uint32_t x = 0;
    while (x < width) {
      while (x < width && strength[x] == 0.0f) ++x;
      if (x == width) break;
      const uint32_t left = x;
      uint32_t end = x + 1, supported = 0, gap = 0;
      float sum = 0.0f;
      while (x < width) {
        if (strength[x] > 0.0f) {
          ++supported;
          sum += strength[x];
          end = x + 1;
          gap = 0;
        } else if (++gap > 2) {
          ++x;
          break;
        }
        ++x;
      }
      const uint32_t length = end - left;
      const float density = static_cast<float>(supported) / length;
      if (length >= minimum_width && density >= 0.84f) {
        candidates.push_back({static_cast<int32_t>(left), static_cast<int32_t>(end), y,
                              sum / supported, density});
      }
    }
  }

  /* A softened/resized image may describe one edge on adjacent rows. Keep
   * the strongest row and suppress overlapping duplicates before capping. */
  std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
    const float a_score = a.strength * a.density;
    const float b_score = b.strength * b.density;
    if (a_score != b_score) return a_score > b_score;
    if (a.right - a.left != b.right - b.left) return a.right - a.left > b.right - b.left;
    if (a.top != b.top) return a.top > b.top; // reachable lower ledges first
    return a.left < b.left;
  });
  std::vector<detail::Candidate> selected;
  const uint32_t remaining = capacity - static_cast<uint32_t>(terrain.segments.size());
  selected.reserve(remaining);
  for (const auto& candidate : candidates) {
    if (!remaining) break;
    const bool duplicate = std::any_of(selected.begin(), selected.end(), [&](const auto& old) {
      const int32_t overlap = std::min(old.right, candidate.right) -
                              std::max(old.left, candidate.left);
      return std::abs(old.top - candidate.top) <= 4 && overlap > 0 &&
             overlap * 5 >= std::min(old.right - old.left, candidate.right - candidate.left) * 3;
    });
    if (duplicate) continue;
    selected.push_back(candidate);
    if (selected.size() == remaining) break;
  }
  std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
    return a.top < b.top || (a.top == b.top && a.left < b.left);
  });
  const auto scale = [](int32_t value, uint32_t full, uint32_t small) {
    return static_cast<int32_t>((static_cast<int64_t>(value) * full + small / 2) / small);
  };
  const auto append = [&](const Ledge& edge) {
    const Ledge mapped{scale(edge.left, screen_width, width), scale(edge.right, screen_width, width),
                       scale(edge.top, screen_height, height), scale(edge.right_top, screen_height, height),
                       edge.confidence, edge.contour_id};
    if (mapped.left < mapped.right && mapped.top >= 0 && mapped.top < floor_top &&
        mapped.right_top >= 0 && mapped.right_top < floor_top)
      result.push_back(mapped);
  };
  for (const auto& edge : terrain.segments) append(edge);
  for (const auto& edge : selected)
    append({edge.left, edge.right, edge.top, edge.top,
            std::clamp(edge.strength * edge.density / 128.0f, 0.0f, 1.0f), 0});
  return result;
}

} // namespace aurora::desktop::wallpaper_geometry
