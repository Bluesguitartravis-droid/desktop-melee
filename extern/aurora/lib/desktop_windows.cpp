/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <aurora/desktop.h>
#include "desktop_window_geometry.hpp"
#include "desktop_windows.hpp"
#ifdef _WIN32
#include <aurora/desktop_monitor_windows.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif
#ifdef AURORA_DESKTOP_X11
#include "desktop_linux_x11.hpp"
#endif

#if defined(_WIN32) || defined(AURORA_DESKTOP_X11)
namespace {
namespace geometry = aurora::desktop::window_geometry;
using Clock = std::chrono::steady_clock;
constexpr auto RefreshPeriod = std::chrono::milliseconds(67);
constexpr size_t MaxVisitedWindows = 1024;
constexpr size_t MaxWindowBounds = 256;
Clock::time_point last_poll;
bool have_snapshot = false;
std::vector<AuroraDesktopWindowEdge> snapshot;
std::vector<geometry::Window> window_snapshot;

#ifdef _WIN32
/* GetWindowRect is DPI-virtualized; DWM's visible bounds are already physical.
 * Keep the fallback in the same coordinate space without changing SDL's
 * process-wide DPI policy. */
class PhysicalDpiScope {
  using SetContext = HANDLE (WINAPI*)(HANDLE);
  SetContext function = nullptr;
  HANDLE previous = nullptr;
public:
  PhysicalDpiScope() {
    function = reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                           "SetThreadDpiAwarenessContext"));
    if (function) previous = function(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
  }
  ~PhysicalDpiScope() { if (function && previous) function(previous); }
};

bool shell_surface(HWND window) {
  if (window == GetDesktopWindow() || window == GetShellWindow()) return true;
  wchar_t class_name[128]{};
  if (!GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)))) return true;
  return std::wcscmp(class_name, L"Progman") == 0 ||
         std::wcscmp(class_name, L"WorkerW") == 0 ||
         std::wcscmp(class_name, L"Shell_TrayWnd") == 0 ||
         std::wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0 ||
         std::wcscmp(class_name, L"#32768") == 0; // transient native menus
}

bool read_window(HWND window, DWORD our_process, const AuroraDesktopGeometry& desktop,
                 geometry::Window& out) {
  if (!IsWindow(window) || !IsWindowVisible(window) || IsIconic(window)) return false;
  DWORD process = 0;
  if (!GetWindowThreadProcessId(window, &process) || !process || process == our_process)
    return false;
  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  const LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
  if ((style & WS_CHILD) || (extended & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT)) ||
      shell_surface(window)) return false;
  DWORD cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
    return false;
  RECT bounds{};
  if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) &&
      !GetWindowRect(window, &bounds)) return false;
  if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return false;
  out = {
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(window)),
      {bounds.left - desktop.screen_x, bounds.top - desktop.screen_y,
       bounds.right - desktop.screen_x, bounds.bottom - desktop.screen_y},
  };
  return out.bounds.right > 0 && out.bounds.bottom > 0 &&
         out.bounds.left < static_cast<int32_t>(desktop.width) &&
         out.bounds.top < static_cast<int32_t>(desktop.height);
}

std::vector<geometry::Window> enumerate_windows(const AuroraDesktopGeometry& desktop) {
  PhysicalDpiScope dpi;
  const DWORD our_process = GetCurrentProcessId();
  std::vector<geometry::Window> windows;
  std::vector<HWND> visited;
  windows.reserve(40);
  visited.reserve(128);
  if (aurora::desktop::monitors::all_requested()) {
    const auto displays = aurora::desktop::monitors::enumerate();
    const int32_t main_floor_abs = desktop.screen_y + desktop.platform_top;
    uint64_t synthetic = UINT64_C(0xF000000000000000);
    for (const auto& monitor : displays) {
      aurora::desktop::monitors::TaskbarQuery query;
      query.monitor = monitor.handle;
      EnumWindows(aurora::desktop::monitors::taskbar_window,
                  reinterpret_cast<LPARAM>(&query));
      const auto bar = aurora::desktop::monitors::rect(query.rect);
      AuroraDesktopGeometry local{};
      if (!aurora::desktop::monitor_geometry::make(
              aurora::desktop::monitors::rect(monitor.info.rcMonitor),
              aurora::desktop::monitors::rect(monitor.info.rcWork),
              query.found ? &bar : nullptr, local))
        continue;
      const int32_t floor_abs = local.screen_y + local.platform_top;
      const auto& r = monitor.info.rcMonitor;
      const int32_t rel_left = r.left - desktop.screen_x;
      const int32_t rel_right = r.right - desktop.screen_x;
      const bool is_main = std::abs(floor_abs - main_floor_abs) <= 2 &&
                           rel_left == desktop.platform_left &&
                           rel_right == desktop.platform_right;
      if (is_main) continue;
      /* Native dynamic floors must sit above the main floor. Parallel monitors
       * at exactly the same Y get an imperceptible one-pixel lift. */
      const int32_t floor_rel = std::min(floor_abs - desktop.screen_y,
                                         desktop.platform_top - 1);
      if (floor_rel <= 0) continue;
      windows.push_back({synthetic++,
          {rel_left, floor_rel, rel_right, r.bottom - desktop.screen_y}});
    }
  }
  /* GetWindow's order is explicit. Bound the traversal and detect repeats:
   * apps may destroy/reorder windows while this read-only snapshot is made. */
  for (HWND window = GetTopWindow(nullptr); window && visited.size() < MaxVisitedWindows &&
       windows.size() < MaxWindowBounds;) {
    if (std::find(visited.begin(), visited.end(), window) != visited.end()) break;
    visited.push_back(window);
    const HWND next = GetWindow(window, GW_HWNDNEXT);
    geometry::Window info{};
    if (read_window(window, our_process, desktop, info)) windows.push_back(info);
    window = next;
  }
  return windows;
}
#elif defined(AURORA_DESKTOP_X11)
std::vector<geometry::Window> enumerate_windows(const AuroraDesktopGeometry& desktop) {
  return aurora::desktop::linux_x11::enumerate_windows(desktop);
}
#endif
} // namespace
#endif

std::span<const aurora::desktop::window_geometry::Window>
aurora::desktop::windows::current_windows() {
#if defined(_WIN32) || defined(AURORA_DESKTOP_X11)
  const char* desktop_mode = std::getenv("MELEE_DESKTOP");
  if (!desktop_mode || std::strcmp(desktop_mode, "1") != 0) {
    snapshot.clear();
    window_snapshot.clear();
    have_snapshot = false;
    return {};
  }
  const auto now = Clock::now();
  if (!have_snapshot || now - last_poll >= RefreshPeriod) {
    AuroraDesktopGeometry desktop{};
    if (!aurora_desktop_get_geometry(&desktop)) {
      snapshot.clear();
      window_snapshot.clear();
      have_snapshot = false;
      return {};
    }
    window_snapshot = enumerate_windows(desktop);
    snapshot = geometry::visible_edges(window_snapshot, static_cast<int32_t>(desktop.width),
                                       static_cast<int32_t>(desktop.height), desktop.platform_top);
    last_poll = now;
    have_snapshot = true;
  }
  return window_snapshot;
#else
  return {};
#endif
}

extern "C" uint32_t aurora_desktop_get_window_edges(AuroraDesktopWindowEdge* edges, uint32_t capacity) {
  if (!edges || capacity == 0) return 0;
#if defined(_WIN32) || defined(AURORA_DESKTOP_X11)
  const char* setting = std::getenv("MELEE_DESKTOP_WINDOWS");
  if (setting && std::strcmp(setting, "0") == 0) return 0;
  aurora::desktop::windows::current_windows();
  const auto count = std::min<uint32_t>(capacity, static_cast<uint32_t>(snapshot.size()));
  std::copy_n(snapshot.data(), count, edges);
  return count;
#else
  return 0;
#endif
}
