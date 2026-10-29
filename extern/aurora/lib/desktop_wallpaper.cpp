/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <aurora/desktop.h>
#include "desktop_wallpaper.hpp"
#include "desktop_wallpaper_geometry.hpp"
#include "desktop_wallpaper_mapping.hpp"
#include "desktop_windows.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <wincodec.h>
#include "logging.hpp"

namespace {
namespace mapping = aurora::desktop::wallpaper_mapping;
namespace geometry = aurora::desktop::wallpaper_geometry;
constexpr auto RefreshPeriod = std::chrono::seconds(2);
constexpr uint32_t MaxImageDimension = 32768;
constexpr uint64_t MaxImagePixels = 64ull * 1024 * 1024;
constexpr uint64_t MaxFileBytes = 256ull * 1024 * 1024;
aurora::Module Log("aurora::desktop::wallpaper");

template <class T> class ComPtr {
  T* pointer = nullptr;
public:
  ComPtr() = default;
  ~ComPtr() { reset(); }
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  T* get() const { return pointer; }
  T* operator->() const { return pointer; }
  explicit operator bool() const { return pointer != nullptr; }
  void reset() { if (pointer) { pointer->Release(); pointer = nullptr; } }
  T** put() { reset(); return &pointer; }
  void assign(T* next) { if (next) next->AddRef(); reset(); pointer = next; }
};

struct TaskString {
  wchar_t* value = nullptr;
  ~TaskString() { CoTaskMemFree(value); }
};

class ComApartment {
  HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
public:
  ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
  bool valid() const { return SUCCEEDED(result); }
};

enum class Status {
  Ready, ComUnavailable, DesktopUnavailable, PrimaryUnavailable, NoImage,
  NonlocalImage, ImageUnavailable, PlacementUnsupported, ImageTooLarge,
  DecodeFailed, ChangedDuringRead, WorkerFailed,
};

const char* status_message(Status status) {
  switch (status) {
  case Status::Ready: return "ready";
  case Status::ComUnavailable: return "Windows image services are unavailable";
  case Status::DesktopUnavailable: return "Windows wallpaper settings are unavailable";
  case Status::PrimaryUnavailable: return "selected-monitor wallpaper geometry changed; relaunch required";
  case Status::NoImage: return "no static wallpaper image is selected";
  case Status::NonlocalImage: return "wallpaper is not an available local file";
  case Status::ImageUnavailable: return "selected wallpaper file is unavailable";
  case Status::PlacementUnsupported: return "multi-monitor Tile/Span is unsupported; choose Center, Fit, Fill or Stretch";
  case Status::ImageTooLarge: return "wallpaper exceeds the image analysis resource limit";
  case Status::DecodeFailed: return "selected wallpaper image could not be decoded";
  case Status::ChangedDuringRead: return "wallpaper changed during analysis; retrying";
  case Status::WorkerFailed: return "image analysis failed; retrying";
  }
  return "unavailable";
}

struct Source {
  std::wstring path;
  uint64_t modified = 0, bytes = 0;
  DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
  COLORREF background = 0;
  UINT monitor_count = 0;
  bool operator==(const Source&) const = default;
};

/* Refuse network paths, mapped network drives and files that would require
 * fetching cloud data. Only the selected, already-local file is decoded. */
bool local_path(const std::wstring& path) {
  size_t offset = 0;
  if (path.starts_with(L"\\\\?\\")) offset = 4;
  if (path.size() < offset + 3 || path[offset + 1] != L':' ||
      (path[offset + 2] != L'\\' && path[offset + 2] != L'/') ||
      !((path[offset] >= L'A' && path[offset] <= L'Z') ||
        (path[offset] >= L'a' && path[offset] <= L'z'))) return false;
  const wchar_t root[]{path[offset], L':', L'\\', L'\0'};
  const UINT drive = GetDriveTypeW(root);
  return drive == DRIVE_FIXED || drive == DRIVE_REMOVABLE ||
         drive == DRIVE_CDROM || drive == DRIVE_RAMDISK;
}

Status query_source(IDesktopWallpaper* desktop, const AuroraDesktopGeometry& screen, Source& source) {
  UINT count = 0;
  if (!desktop || FAILED(desktop->GetMonitorDevicePathCount(&count)) || !count || count > 64)
    return Status::DesktopUnavailable;
  TaskString monitor;
  for (UINT i = 0; i < count; ++i) {
    TaskString candidate;
    RECT bounds{};
    if (FAILED(desktop->GetMonitorDevicePathAt(i, &candidate.value)) || !candidate.value ||
        FAILED(desktop->GetMonitorRECT(candidate.value, &bounds))) continue;
    if (bounds.left == screen.screen_x && bounds.top == screen.screen_y &&
        static_cast<int64_t>(bounds.right) - bounds.left == screen.width &&
        static_cast<int64_t>(bounds.bottom) - bounds.top == screen.height) {
      monitor.value = std::exchange(candidate.value, nullptr);
      break;
    }
  }
  if (!monitor.value) return Status::PrimaryUnavailable;
  if (FAILED(desktop->GetPosition(&source.position)) ||
      FAILED(desktop->GetBackgroundColor(&source.background))) return Status::DesktopUnavailable;
  source.monitor_count = count;
  if ((source.position == DWPOS_SPAN || source.position == DWPOS_TILE) && count != 1)
    return Status::PlacementUnsupported;
  if (source.position < DWPOS_CENTER || source.position > DWPOS_SPAN)
    return Status::DesktopUnavailable;
  TaskString filename;
  if (FAILED(desktop->GetWallpaper(monitor.value, &filename.value))) return Status::DesktopUnavailable;
  if (!filename.value || !*filename.value) return Status::NoImage;
  source.path = filename.value;
  if (!local_path(source.path)) return Status::NonlocalImage;
  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  if (!GetFileAttributesExW(source.path.c_str(), GetFileExInfoStandard, &attributes) ||
      (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return Status::ImageUnavailable;
  /* RECALL_ON_OPEN and RECALL_ON_DATA_ACCESS are not defined by every SDK. */
  if (attributes.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | 0x00040000u | 0x00400000u))
    return Status::NonlocalImage;
  source.modified = (static_cast<uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
                     attributes.ftLastWriteTime.dwLowDateTime;
  source.bytes = (static_cast<uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
  if (!source.bytes) return Status::ImageUnavailable;
  if (source.bytes > MaxFileBytes) return Status::ImageTooLarge;
  return Status::Ready;
}

mapping::Position position_for(DESKTOP_WALLPAPER_POSITION position) {
  switch (position) {
  case DWPOS_CENTER: return mapping::Position::Center;
  case DWPOS_TILE: return mapping::Position::Tile;
  case DWPOS_STRETCH: return mapping::Position::Stretch;
  case DWPOS_FIT: return mapping::Position::Fit;
  default: return mapping::Position::Fill; // Fill and single-monitor Span
  }
}

unsigned image_orientation(IWICBitmapFrameDecode* frame) {
  ComPtr<IWICMetadataQueryReader> metadata;
  if (FAILED(frame->GetMetadataQueryReader(metadata.put()))) return 1;
  for (const auto* query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
    PROPVARIANT value{};
    const HRESULT result = metadata->GetMetadataByName(query, &value);
    unsigned orientation = 0;
    if (SUCCEEDED(result)) {
      if (value.vt == VT_UI2) orientation = value.uiVal;
      else if (value.vt == VT_UI4) orientation = value.ulVal;
    }
    PropVariantClear(&value);
    if (orientation >= 1 && orientation <= 8) return orientation;
  }
  return 1;
}

bool transform(IWICImagingFactory* factory, ComPtr<IWICBitmapSource>& source,
               WICBitmapTransformOptions operation) {
  ComPtr<IWICBitmapFlipRotator> rotator;
  if (FAILED(factory->CreateBitmapFlipRotator(rotator.put())) ||
      FAILED(rotator->Initialize(source.get(), operation))) return false;
  source.assign(rotator.get());
  return true;
}

Status analyze(IWICImagingFactory* factory, const Source& source,
               const AuroraDesktopGeometry& screen, uint64_t generation,
               std::vector<AuroraDesktopWallpaperEdge>& edges) {
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(factory->CreateDecoderFromFilename(source.path.c_str(), nullptr, GENERIC_READ,
              WICDecodeMetadataCacheOnDemand, decoder.put())) ||
      FAILED(decoder->GetFrame(0, frame.put()))) return Status::DecodeFailed;
  UINT width = 0, height = 0;
  if (FAILED(frame->GetSize(&width, &height)) || !width || !height) return Status::DecodeFailed;
  if (width > MaxImageDimension || height > MaxImageDimension ||
      static_cast<uint64_t>(width) * height > MaxImagePixels) return Status::ImageTooLarge;
  ComPtr<IWICBitmapSource> oriented;
  oriented.assign(frame.get());
  const unsigned orientation = image_orientation(frame.get());
  WICBitmapTransformOptions rotation = WICBitmapTransformRotate0;
  if (orientation == 3) rotation = WICBitmapTransformRotate180;
  if (orientation == 5 || orientation == 6 || orientation == 7) rotation = WICBitmapTransformRotate90;
  if (orientation == 8) rotation = WICBitmapTransformRotate270;
  if (rotation != WICBitmapTransformRotate0 && !transform(factory, oriented, rotation))
    return Status::DecodeFailed;
  if ((orientation == 2 || orientation == 5) &&
      !transform(factory, oriented, WICBitmapTransformFlipHorizontal)) return Status::DecodeFailed;
  if ((orientation == 4 || orientation == 7) &&
      !transform(factory, oriented, WICBitmapTransformFlipVertical)) return Status::DecodeFailed;
  if (FAILED(oriented->GetSize(&width, &height))) return Status::DecodeFailed;

  const auto analysis = mapping::analysis_size({screen.width, screen.height});
  const auto layout = mapping::make_layout({width, height}, {screen.width, screen.height},
                                          position_for(source.position));
  const auto crop = mapping::source_crop(layout);
  if (!analysis.width || !analysis.height || !crop.width || !crop.height) return Status::DecodeFailed;
  ComPtr<IWICBitmapClipper> clipper;
  WICRect rect{static_cast<INT>(crop.x), static_cast<INT>(crop.y),
               static_cast<INT>(crop.width), static_cast<INT>(crop.height)};
  if (FAILED(factory->CreateBitmapClipper(clipper.put())) ||
      FAILED(clipper->Initialize(oriented.get(), &rect))) return Status::DecodeFailed;
  ComPtr<IWICBitmapSource> resized;
  resized.assign(clipper.get());
  /* Downsample before requesting pixel storage. The visible crop is at most
   * 640x360 RGBA, even for a huge source or a high-DPI primary monitor. */
  const mapping::Size decoded_size{std::min(crop.width, analysis.width),
                                    std::min(crop.height, analysis.height)};
  if (decoded_size.width != crop.width || decoded_size.height != crop.height) {
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(resized.get(), decoded_size.width, decoded_size.height,
                                  WICBitmapInterpolationModeFant))) return Status::DecodeFailed;
    resized.assign(scaler.get());
  }
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(factory->CreateFormatConverter(converter.put())) ||
      FAILED(converter->Initialize(resized.get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom))) return Status::DecodeFailed;
  const UINT stride = decoded_size.width * 4;
  const UINT bytes = stride * decoded_size.height;
  std::vector<uint8_t> decoded(bytes);
  if (FAILED(converter->CopyPixels(nullptr, stride, bytes, decoded.data()))) return Status::DecodeFailed;
  const auto rgba = mapping::compose(decoded, decoded_size, crop, layout, analysis,
      {GetRValue(source.background), GetGValue(source.background), GetBValue(source.background)});
  if (rgba.empty()) return Status::DecodeFailed;
  const auto ledges = geometry::detect_ledges(rgba, analysis.width, analysis.height,
      screen.width, screen.height, screen.platform_top, AURORA_DESKTOP_MAX_WALLPAPER_EDGES);
  edges.reserve(ledges.size());
  uint32_t id = 1;
  for (const auto& ledge : ledges) {
    edges.push_back({generation, id++, ledge.left, ledge.right, ledge.top,
                     ledge.right_top, ledge.contour_id,
                     ledge.left, ledge.right, ledge.top, ledge.right_top});
  }
  return Status::Ready;
}

bool same_screen(const AuroraDesktopGeometry& a, const AuroraDesktopGeometry& b) {
  return a.screen_x == b.screen_x && a.screen_y == b.screen_y &&
         a.width == b.width && a.height == b.height && a.platform_top == b.platform_top;
}

struct Shared {
  std::mutex mutex;
  std::condition_variable changed;
  std::thread worker;
  bool stopping = false, enabled = false;
  uint64_t epoch = 0;
  AuroraDesktopGeometry screen{};
  std::vector<AuroraDesktopWallpaperEdge> edges;
  void stop() noexcept {
    {
      std::lock_guard lock(mutex);
      stopping = true;
      enabled = false;
      ++epoch;
      edges.clear();
    }
    changed.notify_one();
    if (worker.joinable()) worker.join();
  }
  ~Shared() { stop(); }
};
std::unique_ptr<Shared> state;
std::chrono::steady_clock::time_point next_start_attempt;

bool current(Shared& shared, uint64_t epoch) {
  std::lock_guard lock(shared.mutex);
  return !shared.stopping && shared.enabled && shared.epoch == epoch;
}

void clear_result(Shared& shared, uint64_t epoch) {
  std::lock_guard lock(shared.mutex);
  if (shared.epoch == epoch) shared.edges.clear();
}

void worker_main(Shared& shared) noexcept {
  ComApartment apartment;
  ComPtr<IDesktopWallpaper> desktop;
  ComPtr<IWICImagingFactory> imaging;
  std::optional<Source> previous;
  uint64_t previous_epoch = 0, generation = 0;
  std::optional<Status> last_status;
  auto report = [&](Status status, size_t count = 0, bool publish = false) {
    if (status == Status::Ready) {
      if (publish) Log.info("Wallpaper analysis ready: {} standable ledges", count);
    } else if (!last_status || status != *last_status) {
      Log.info("Wallpaper platforms cleared: {}", status_message(status));
    }
    last_status = status;
  };
  for (;;) {
    AuroraDesktopGeometry screen{};
    uint64_t epoch;
    {
      std::unique_lock lock(shared.mutex);
      shared.changed.wait(lock, [&] { return shared.stopping || shared.enabled; });
      if (shared.stopping) return;
      screen = shared.screen;
      epoch = shared.epoch;
    }
    if (epoch != previous_epoch) { previous.reset(); previous_epoch = epoch; }
    try {
      Status status = apartment.valid() ? Status::Ready : Status::ComUnavailable;
      if (status == Status::Ready && !desktop &&
          FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
                  IID_IDesktopWallpaper, reinterpret_cast<void**>(desktop.put()))))
        status = Status::DesktopUnavailable;
      if (status == Status::Ready && !imaging &&
          FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                  IID_IWICImagingFactory, reinterpret_cast<void**>(imaging.put()))))
        status = Status::ComUnavailable;
      Source source;
      if (status == Status::Ready) status = query_source(desktop.get(), screen, source);
      if (status != Status::Ready) {
        clear_result(shared, epoch);
        previous.reset();
        // Reconnect if Explorer or its wallpaper COM service restarted.
        if (status == Status::DesktopUnavailable) desktop.reset();
        report(status);
      } else if (!previous || *previous != source) {
        /* Retire old collision as soon as a changed image is observed. A
         * slow/failed decode must never leave terrain from the old image. */
        clear_result(shared, epoch);
        if (++generation == 0) ++generation;
        std::vector<AuroraDesktopWallpaperEdge> edges;
        if (current(shared, epoch)) status = analyze(imaging.get(), source, screen, generation, edges);
        if (status == Status::Ready && current(shared, epoch)) {
          Source after;
          const auto after_status = query_source(desktop.get(), screen, after);
          if (after_status != Status::Ready) status = after_status;
          else if (after != source) status = Status::ChangedDuringRead;
        }
        if (current(shared, epoch)) {
          if (status == Status::Ready) {
            const auto count = edges.size();
            {
              std::lock_guard lock(shared.mutex);
              if (!shared.stopping && shared.enabled && shared.epoch == epoch)
                shared.edges = std::move(edges);
            }
            previous = std::move(source);
            report(Status::Ready, count, true);
          } else {
            previous.reset();
            if (status == Status::DesktopUnavailable) desktop.reset();
            report(status);
          }
        }
      }
    } catch (...) {
      clear_result(shared, epoch);
      previous.reset();
      report(Status::WorkerFailed);
    }
    std::unique_lock lock(shared.mutex);
    shared.changed.wait_for(lock, RefreshPeriod, [&] {
      return shared.stopping || !shared.enabled || shared.epoch != epoch;
    });
    if (shared.stopping) return;
  }
}

bool opted_in() {
  const char* desktop = std::getenv("MELEE_DESKTOP");
  const char* wallpaper = std::getenv("MELEE_DESKTOP_WALLPAPER");
  return desktop && wallpaper && std::strcmp(desktop, "1") == 0 && std::strcmp(wallpaper, "1") == 0;
}

void disable() {
  if (!state) return;
  {
    std::lock_guard lock(state->mutex);
    if (!state->enabled) return;
    state->enabled = false;
    ++state->epoch;
    state->edges.clear();
  }
  state->changed.notify_one();
}
} // namespace
#endif

extern "C" uint32_t aurora_desktop_get_wallpaper_edges(AuroraDesktopWallpaperEdge* edges, uint32_t capacity) {
  if (!edges || !capacity) return 0;
#ifdef _WIN32
  if (!opted_in()) { disable(); return 0; }
  AuroraDesktopGeometry screen{};
  if (!aurora_desktop_get_geometry(&screen)) { disable(); return 0; }
  try {
    if (!state) {
      const auto now = std::chrono::steady_clock::now();
      if (now < next_start_attempt) return 0;
      next_start_attempt = now + RefreshPeriod;
      // Publish only after thread creation succeeds; a failed start must
      // not leave a nonnull state that can never produce a snapshot.
      auto starting = std::make_unique<Shared>();
      starting->worker = std::thread(worker_main, std::ref(*starting));
      state = std::move(starting);
    }
    std::vector<AuroraDesktopWallpaperEdge> snapshot;
    bool changed = false;
    {
      std::lock_guard lock(state->mutex);
      if (state->stopping) return 0;
      if (!state->enabled || !same_screen(state->screen, screen)) {
        state->enabled = true;
        state->screen = screen;
        ++state->epoch;
        state->edges.clear();
        changed = true;
      }
      snapshot = state->edges;
    }
    if (changed) state->changed.notify_one();
    /* Window masks refresh independently of whether window platforms are
     * enabled. Moving an app never causes image decoding on the UI thread. */
    const auto visible = mapping::clip_edges(snapshot, aurora::desktop::windows::current_windows(), capacity);
    std::copy(visible.begin(), visible.end(), edges);
    return static_cast<uint32_t>(visible.size());
  } catch (...) {
    disable();
    Log.info("Wallpaper platforms cleared: image worker could not start or publish");
    return 0;
  }
#else
  return 0;
#endif
}

void aurora::desktop::wallpaper::shutdown() noexcept {
#ifdef _WIN32
  if (state) state->stop();
#endif
}
