#include "desktop.hpp"
#include "desktop_pixels.hpp"
#include "desktop_wallpaper.hpp"
#include "internal.hpp"
#include "window.hpp"

#include <atomic>
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <vector>
#include <utility>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>

#if defined(_WIN32) && defined(AURORA_ENABLE_GX)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <aurora/desktop_monitor_windows.hpp>
#endif

#if defined(AURORA_ENABLE_GX) && (defined(_WIN32) || defined(AURORA_DESKTOP_X11))
#define AURORA_DESKTOP_NATIVE 1
#include "gfx/frame.hpp"
#include "webgpu/gpu.hpp"
#endif
#ifdef AURORA_DESKTOP_X11
#include "desktop_linux_x11.hpp"
#endif

namespace aurora::desktop {
namespace {
Module Log("aurora::desktop");
std::atomic_bool g_active{false};
bool g_settingsRequested = false;

#if defined(AURORA_DESKTOP_NATIVE)
#ifdef _WIN32
constexpr wchar_t WindowClass[] = L"DesktopMeleeLayeredOverlay";
constexpr int ExitHotkey = 0x4D45;
constexpr UINT EmergencyExitMessage = WM_APP + 47;
constexpr uint32_t MaxDimension = 16384;

HWND g_overlay = nullptr;
HDC g_memoryDC = nullptr;
HBITMAP g_bitmap = nullptr;
HGDIOBJ g_previousBitmap = nullptr;
uint8_t* g_bitmapBytes = nullptr;
#endif
AuroraDesktopGeometry g_geometry{};
bool g_geometryReady = false;
bool g_oldPauseOnFocusLost = false;
bool g_oldBackgroundInput = false;
bool g_overlayVisible = false;
bool g_failed = false;
std::atomic_uint64_t g_generation{0};

struct CompletedFrame {
  uint32_t width = 0, height = 0;
  uint64_t sequence = 0, generation = 0;
  std::vector<uint8_t> pixels;
};
std::mutex g_frameMutex;
CompletedFrame g_completed;
uint64_t g_consumedSequence = 0;
bool g_readbackFailed = false;

struct ReadbackSlot {
  wgpu::Buffer buffer;
  uint64_t size = 0;
  std::atomic_bool busy{false};
};
std::array<std::shared_ptr<ReadbackSlot>, 3> g_slots;
wgpu::Texture g_target;
uint64_t g_nextSequence = 0;

bool query_geometry(AuroraDesktopGeometry& out);

bool same_geometry(const AuroraDesktopGeometry& a, const AuroraDesktopGeometry& b) {
  return a.screen_x == b.screen_x && a.screen_y == b.screen_y &&
         a.width == b.width && a.height == b.height &&
         a.platform_left == b.platform_left && a.platform_top == b.platform_top &&
         a.platform_right == b.platform_right && a.platform_bottom == b.platform_bottom;
}

void request_quit() {
  SDL_Event event{};
  event.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&event);
}

#ifdef _WIN32
LRESULT CALLBACK overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_NCHITTEST:
    return HTTRANSPARENT;
  case WM_MOUSEACTIVATE:
    return MA_NOACTIVATE;
  case WM_HOTKEY:
    if (wparam == ExitHotkey) {
      request_quit();
      return 0;
    }
    break;
  case WM_CLOSE:
  case EmergencyExitMessage:
    request_quit();
    return 0;
  case WM_DISPLAYCHANGE:
    /* The collision geometry was established at match start. A monitor
     * rearrangement must not silently move a live stage under its fighters. */
    Log.warn("Display configuration changed; close and relaunch Desktop Melee to use the new layout");
    request_quit();
    return 0;
  case WM_SETTINGCHANGE:
  case WM_DPICHANGED: {
    AuroraDesktopGeometry current{};
    if (g_geometryReady && (!query_geometry(current) || !same_geometry(current, g_geometry))) {
      Log.warn("Taskbar or display scale changed; close and relaunch Desktop Melee to use the new layout");
      request_quit();
    }
    break;
  }
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* Thread DPI context keeps Win32 coordinates physical without changing the
 * process-wide SDL DPI policy. This API is present on Windows 10/11. */
class PhysicalDpiScope {
  using SetContext = HANDLE (WINAPI*)(HANDLE);
  SetContext fn = nullptr;
  HANDLE old = nullptr;
public:
  PhysicalDpiScope() {
    fn = reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                    "SetThreadDpiAwarenessContext"));
    if (fn) old = fn(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
  }
  ~PhysicalDpiScope() { if (fn && old) fn(old); }
};

bool query_geometry(AuroraDesktopGeometry& out) {
  return monitors::geometry(monitors::requested_id(), out);
}

#elif defined(AURORA_DESKTOP_X11)
bool query_geometry(AuroraDesktopGeometry& out) { return linux_x11::query_geometry(out); }
#endif

bool get_geometry(AuroraDesktopGeometry& out) {
  /* The game maps stage collision before the overlay opens. Keep that exact
   * physical geometry across settings windows and successive matches. */
  if (!g_geometryReady) {
    if (!query_geometry(g_geometry)) return false;
    g_geometryReady = true;
  }
  out = g_geometry;
  return true;
}

bool supported_format(wgpu::TextureFormat format) {
  return format == wgpu::TextureFormat::RGBA8Unorm ||
         format == wgpu::TextureFormat::RGBA8UnormSrgb ||
         format == wgpu::TextureFormat::BGRA8Unorm ||
         format == wgpu::TextureFormat::BGRA8UnormSrgb;
}

#ifdef _WIN32
void destroy_overlay() {
  if (g_overlay) {
    UnregisterHotKey(g_overlay, ExitHotkey);
    DestroyWindow(g_overlay);
    g_overlay = nullptr;
  }
  if (g_memoryDC && g_previousBitmap) SelectObject(g_memoryDC, g_previousBitmap);
  g_previousBitmap = nullptr;
  if (g_bitmap) DeleteObject(g_bitmap);
  g_bitmap = nullptr;
  g_bitmapBytes = nullptr;
  if (g_memoryDC) DeleteDC(g_memoryDC);
  g_memoryDC = nullptr;
  g_overlayVisible = false;
}

bool create_overlay() {
  PhysicalDpiScope dpi;
  WNDCLASSEXW cls{};
  cls.cbSize = sizeof(cls);
  cls.lpfnWndProc = overlay_proc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = WindowClass;
  if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
  g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                              WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              WindowClass, L"Desktop Melee", WS_POPUP,
                              g_geometry.screen_x, g_geometry.screen_y,
                              g_geometry.width, g_geometry.height,
                              nullptr, nullptr, cls.hInstance, nullptr);
  if (!g_overlay) return false;
  /* Never trap the user underneath an unverified fullscreen overlay. */
  if (!RegisterHotKey(g_overlay, ExitHotkey, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F11)) {
    Log.warn("Ctrl+Shift+F11 is already registered; retaining the normal game window");
    destroy_overlay();
    return false;
  }
  g_memoryDC = CreateCompatibleDC(nullptr);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = g_geometry.width;
  info.bmiHeader.biHeight = -static_cast<LONG>(g_geometry.height); // top-down BGRA
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  g_bitmap = CreateDIBSection(g_memoryDC, &info, DIB_RGB_COLORS,
                              reinterpret_cast<void**>(&g_bitmapBytes), nullptr, 0);
  if (!g_memoryDC || !g_bitmap || !g_bitmapBytes) {
    destroy_overlay();
    return false;
  }
  g_previousBitmap = SelectObject(g_memoryDC, g_bitmap);
  if (!g_previousBitmap || g_previousBitmap == HGDI_ERROR) {
    g_previousBitmap = nullptr;
    destroy_overlay();
    return false;
  }
  std::memset(g_bitmapBytes, 0, static_cast<size_t>(g_geometry.width) * g_geometry.height * 4);
  return true;
}

#elif defined(AURORA_DESKTOP_X11)
void destroy_overlay() { linux_x11::destroy_overlay(); g_overlayVisible = false; }
bool create_overlay() { return linux_x11::create_overlay(g_geometry); }
#endif

bool set_enabled(bool enabled) {
  if (enabled == active()) return true;
  if (enabled && (g_failed || !window::get_sdl_window() || !webgpu::g_device)) return false;
  gfx::gpu_synchronize();
  if (enabled) {
    AuroraDesktopGeometry current{};
    if (!get_geometry(current) || !query_geometry(current)) {
      Log.error("Unable to measure the selected monitor; retaining normal game window");
      g_failed = true;
      return false;
    }
    if (!same_geometry(current, g_geometry)) {
      Log.warn("Desktop layout changed since stage setup; relaunch Desktop Melee to use the new layout");
      g_failed = true;
      request_quit();
      return false;
    }
    if (!supported_format(webgpu::g_graphicsConfig.surfaceConfiguration.format)) {
      Log.error("Desktop compositor needs an 8-bit RGBA/BGRA surface; retaining normal game window");
      g_failed = true;
      return false;
    }
    if (!create_overlay()) {
      Log.error("Unable to create desktop compositor: {}", aurora_desktop_unavailable_reason());
      g_failed = true;
      return false;
    }
    ++g_generation;
    g_oldPauseOnFocusLost = g_config.pauseOnFocusLost;
    g_oldBackgroundInput = g_config.allowJoystickBackgroundEvents;
    g_config.pauseOnFocusLost = false;
    g_config.allowJoystickBackgroundEvents = true;
    window::set_background_input(true);
    g_active.store(true, std::memory_order_release);
    SDL_HideWindow(window::get_sdl_window());
    Log.info("Desktop overlay enabled: {}x{}, taskbar y={}, Ctrl+Shift+F11 quits",
              g_geometry.width, g_geometry.height, g_geometry.platform_top);
  } else {
    g_active.store(false, std::memory_order_release);
    ++g_generation;
    destroy_overlay();
    g_config.pauseOnFocusLost = g_oldPauseOnFocusLost;
    g_config.allowJoystickBackgroundEvents = g_oldBackgroundInput;
    window::set_background_input(g_oldBackgroundInput);
    if (window::get_sdl_window()) SDL_ShowWindow(window::get_sdl_window());
    Log.info("Desktop overlay disabled; normal game window restored");
  }
  /* Callbacks retain their own slot; resetting does not invalidate an
   * in-flight map. The generation check rejects their stale pixels. */
  g_target = {};
  for (auto& slot : g_slots) slot.reset();
  {
    std::lock_guard lock(g_frameMutex);
    g_completed = {};
    g_consumedSequence = 0;
    g_readbackFailed = false;
  }
  const auto size = window::get_window_size();
  webgpu::resize_swapchain(size.fb_width, size.fb_height,
                          size.native_fb_width, size.native_fb_height, true);
  window::request_frame_buffer_resize();
  return true;
}
#endif
} // namespace

bool active() noexcept { return g_active.load(std::memory_order_acquire); }

bool needs_event_poll() noexcept {
#ifdef AURORA_DESKTOP_X11
  return linux_x11::controls_active();
#else
  return false;
#endif
}

bool window_size(AuroraWindowSize& size) noexcept {
#if defined(AURORA_DESKTOP_NATIVE)
  if (!active()) return false;
  const auto budget = pixels::render_size(g_geometry.width, g_geometry.height);
  size = {g_geometry.width, g_geometry.height,
           budget.width, budget.height, budget.width, budget.height, 1.f};
  return true;
#else
  (void)size;
  return false;
#endif
}

void pump() noexcept {
#if defined(AURORA_DESKTOP_NATIVE)
#ifdef AURORA_DESKTOP_X11
  const auto events = linux_x11::poll();
  if (events & linux_x11::Settings) g_settingsRequested = true;
  if (events & (linux_x11::Quit | linux_x11::LayoutChanged)) request_quit();
  if ((events & linux_x11::Failed) && active()) {
    Log.error("X11 desktop overlay failed: {}", linux_x11::unavailable_reason());
    g_failed = true;
    set_enabled(false);
    return;
  }
#endif
  if (!active()) return;
  /* GPU progress belongs to the render worker. Its end_frame/idle paths
   * already process Dawn events. Pumping here raced D3D12 completion cleanup
   * against that worker. The UI must only consume completed CPU pixels. */
  CompletedFrame frame;
  bool readbackFailed = false;
  {
    std::lock_guard lock(g_frameMutex);
    readbackFailed = g_readbackFailed;
    if (!readbackFailed) {
      if (g_completed.sequence <= g_consumedSequence ||
          g_completed.generation != g_generation.load(std::memory_order_acquire)) return;
      std::swap(frame, g_completed);
      g_consumedSequence = frame.sequence;
    }
  }
  if (readbackFailed) {
    Log.error("Desktop GPU readback failed; restoring normal game window");
    g_failed = true;
    set_enabled(false);
    return;
  }
  if (frame.pixels.empty()) return;
#ifdef _WIN32
  pixels::scale_bgra(g_bitmapBytes, g_geometry.width, g_geometry.height,
                     frame.pixels.data(), frame.width, frame.height);
  POINT dst{g_geometry.screen_x, g_geometry.screen_y};
  POINT src{};
  SIZE size{static_cast<LONG>(g_geometry.width), static_cast<LONG>(g_geometry.height)};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  PhysicalDpiScope dpi;
  if (!UpdateLayeredWindow(g_overlay, nullptr, &dst, &size, g_memoryDC, &src, 0, &blend, ULW_ALPHA)) {
    Log.error("UpdateLayeredWindow failed ({}); restoring normal game window", GetLastError());
    g_failed = true;
    set_enabled(false);
    return;
  }
#elif defined(AURORA_DESKTOP_X11)
  if (!linux_x11::present(frame.pixels, frame.width, frame.height)) {
    Log.error("X11 desktop presentation failed; restoring normal game window");
    g_failed = true;
    set_enabled(false);
    return;
  }
#endif
  if (!g_overlayVisible) {
#ifdef _WIN32
    ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
#endif
    g_overlayVisible = true;
    Log.info("Desktop first frame presented: {}x{} pixels", g_geometry.width, g_geometry.height);
  }
#endif
}

void shutdown() noexcept {
  wallpaper::shutdown();
#if defined(AURORA_DESKTOP_NATIVE)
  if (active()) set_enabled(false);
  g_target = {};
  for (auto& slot : g_slots) slot.reset();
#ifdef AURORA_DESKTOP_X11
  linux_x11::shutdown();
#endif
#endif
}

#ifdef AURORA_ENABLE_GX
wgpu::Texture frame_target() {
#if defined(AURORA_DESKTOP_NATIVE)
  if (!active()) return {};
  const auto& config = webgpu::g_graphicsConfig.surfaceConfiguration;
  if (!g_target || g_target.GetWidth() != config.width || g_target.GetHeight() != config.height ||
      g_target.GetFormat() != config.format) {
    const wgpu::TextureDescriptor desc{
        .label = "Desktop Melee transparent output",
        .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc,
        .dimension = wgpu::TextureDimension::e2D,
        .size = {config.width, config.height, 1},
        .format = config.format,
    };
    g_target = webgpu::g_device.CreateTexture(&desc);
  }
  return g_target;
#else
  return {};
#endif
}

std::function<void()> record_readback(const wgpu::CommandEncoder& encoder,
                                      const wgpu::Texture& texture) {
#if defined(AURORA_DESKTOP_NATIVE)
  if (!active() || !texture) return {};
  std::shared_ptr<ReadbackSlot> selected;
  for (auto& slot : g_slots) {
    if (!slot) slot = std::make_shared<ReadbackSlot>();
    bool expected = false;
    if (slot->busy.compare_exchange_strong(expected, true)) {
      selected = slot;
      break;
    }
  }
  if (!selected) return {}; // Drop presentation, never block game simulation.
  const uint32_t width = texture.GetWidth();
  const uint32_t height = texture.GetHeight();
  const uint32_t stride = (width * 4 + 255) & ~255u;
  const uint64_t bytes = static_cast<uint64_t>(stride) * height;
  if (!selected->buffer || selected->size != bytes) {
    const wgpu::BufferDescriptor desc{
        .label = "Desktop Melee asynchronous BGRA readback",
        .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead,
        .size = bytes,
    };
    selected->buffer = webgpu::g_device.CreateBuffer(&desc);
    selected->size = bytes;
  }
  const wgpu::TexelCopyTextureInfo source{.texture = texture};
  const wgpu::TexelCopyBufferInfo dest{
      .layout = {.offset = 0, .bytesPerRow = stride, .rowsPerImage = height},
      .buffer = selected->buffer,
  };
  const wgpu::Extent3D extent{width, height, 1};
  encoder.CopyTextureToBuffer(&source, &dest, &extent);
  const uint64_t generation = g_generation.load(std::memory_order_acquire);
  const uint64_t sequence = ++g_nextSequence;
  const bool bgra = texture.GetFormat() == wgpu::TextureFormat::BGRA8Unorm ||
                    texture.GetFormat() == wgpu::TextureFormat::BGRA8UnormSrgb;
  return [selected, width, height, stride, bytes, generation, sequence, bgra]() {
    /* This callback reads and unmaps a WebGPU buffer. Those API calls must
     * run from the worker's explicit event pump, never spontaneously from
     * inside an arbitrary WebGPU call. */
    selected->buffer.MapAsync(wgpu::MapMode::Read, 0, bytes, wgpu::CallbackMode::AllowProcessEvents,
        [selected, width, height, stride, bytes, generation, sequence, bgra]
        (wgpu::MapAsyncStatus status, wgpu::StringView) {
          bool failed = status != wgpu::MapAsyncStatus::Success;
          if (!failed) {
            const auto* mapped = static_cast<const uint8_t*>(selected->buffer.GetConstMappedRange(0, bytes));
            failed = mapped == nullptr;
            if (mapped && active() && generation == g_generation.load(std::memory_order_acquire)) {
              CompletedFrame frame{width, height, sequence, generation, {}};
              frame.pixels.resize(static_cast<size_t>(width) * height * 4);
              pixels::copy_bgra(frame.pixels.data(), mapped, width, height, stride, bgra);
              bool firstFrame = false;
              {
                std::lock_guard lock(g_frameMutex);
                if (sequence > g_completed.sequence && sequence > g_consumedSequence &&
                    generation == g_generation.load(std::memory_order_acquire)) {
                  firstFrame = g_completed.sequence == 0 && g_consumedSequence == 0;
                  g_completed = std::move(frame);
                }
              }
              if (firstFrame) Log.info("Desktop first GPU frame mapped: {}x{} pixels", width, height);
            }
            selected->buffer.Unmap();
          }
          if (failed) {
            std::lock_guard lock(g_frameMutex);
            if (active() && generation == g_generation.load(std::memory_order_acquire))
              g_readbackFailed = true;
          }
          selected->busy.store(false, std::memory_order_release);
        });
  };
#else
  (void)encoder;
  (void)texture;
  return {};
#endif
}
#endif
} // namespace aurora::desktop

extern "C" bool aurora_desktop_get_geometry(AuroraDesktopGeometry* geometry) {
  if (!geometry) return false;
#if defined(AURORA_DESKTOP_NATIVE)
  return aurora::desktop::get_geometry(*geometry);
#else
  *geometry = {};
  return false;
#endif
}

extern "C" bool aurora_desktop_enable(bool enabled) {
#if defined(AURORA_DESKTOP_NATIVE)
  return aurora::desktop::set_enabled(enabled);
#else
  return !enabled;
#endif
}

extern "C" bool aurora_desktop_active(void) { return aurora::desktop::active(); }

extern "C" bool aurora_desktop_take_settings_request(void) {
  return std::exchange(aurora::desktop::g_settingsRequested, false);
}

extern "C" const char* aurora_desktop_unavailable_reason(void) {
#ifdef AURORA_DESKTOP_X11
  return aurora::desktop::linux_x11::unavailable_reason();
#else
  return "Could not initialize a transparent desktop overlay on this display.";
#endif
}
