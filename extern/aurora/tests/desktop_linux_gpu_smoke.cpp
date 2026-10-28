/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Disc-free smoke test for the complete SDL -> Aurora -> Vulkan -> XCB path.
 * Run under an X11 compositor, optionally with Mesa's software Vulkan ICD.
 * This validates presentation/lifecycle, not real-game or Steam Deck speed. */
#include <aurora/aurora.h>
#include <aurora/desktop.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace {
std::atomic_uint mapped_frames{0}, presented_frames{0}, errors{0};
bool exit_requested = false;

void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned length) {
  std::fprintf(level >= LOG_ERROR ? stderr : stdout, "[%s] %.*s\n", module, static_cast<int>(length), message);
  std::fflush(level >= LOG_ERROR ? stderr : stdout);
  const std::string_view text(message, length);
  if (text.find("Desktop first GPU frame mapped:") != std::string_view::npos) ++mapped_frames;
  if (text.find("Desktop first frame presented:") != std::string_view::npos) ++presented_frames;
  if (level >= LOG_ERROR) ++errors;
  if (level == LOG_FATAL) std::abort();
}

void update() {
  for (auto* event = aurora_update(); event && event->type != AURORA_NONE; ++event)
    if (event->type == AURORA_EXIT) exit_requested = true;
}

bool render_until(unsigned required_generations, unsigned minimum_frames) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  unsigned frames = 0;
  while (!exit_requested && !errors.load() && std::chrono::steady_clock::now() < deadline) {
    update();
    if (aurora_begin_frame()) {
      /* Aurora records a clear and presentation blit even without game draw
       * commands. No ROM, extracted assets, or emulated game state is loaded. */
      aurora_end_frame();
      ++frames;
    }
    if (frames >= minimum_frames && mapped_frames.load() >= required_generations &&
        presented_frames.load() >= required_generations) return true;
    SDL_Delay(8);
  }
  return false;
}
} // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "Usage: desktop_linux_gpu_smoke RESOURCES_DIRECTORY TEMP_STATE_DIRECTORY\n");
    return 2;
  }
  std::filesystem::create_directories(argv[2]);
  setenv("MELEE_DESKTOP", "1", 1);
  setenv("MELEE_DESKTOP_WALLPAPER", "0", 1);
  AuroraConfig config{};
  config.appName = "Desktop Melee Linux GPU smoke";
  config.userPath = argv[2];
  config.cachePath = argv[2];
  config.resourcesPath = argv[1];
  config.desiredBackend = BACKEND_VULKAN;
  config.allowCpuAdapter = true;
  config.allowJoystickBackgroundEvents = true;
  config.pauseOnFocusLost = false;
  config.windowWidth = 640;
  config.windowHeight = 480;
  config.logCallback = log_callback;
  config.logLevel = LOG_INFO;
  const AuroraInfo info = aurora_initialize(argc, argv, &config);
  bool success = info.window && info.backend == BACKEND_VULKAN;
  AuroraDesktopGeometry geometry{};
  success = success && aurora_desktop_get_geometry(&geometry) && geometry.width && geometry.height;
  success = success && aurora_desktop_enable(true);
  success = success && (SDL_GetWindowFlags(info.window) & SDL_WINDOW_HIDDEN);
  success = success && render_until(1, 20);
  success = success && aurora_desktop_enable(false) && !aurora_desktop_active();
  success = success && !(SDL_GetWindowFlags(info.window) & SDL_WINDOW_HIDDEN);
  success = success && render_until(1, 12);
  if (success) {
    /* A minimized/unfocused settings window must not trap the separate XCB
     * control connection in an indefinite SDL event wait. The outer test
     * runner also applies a timeout to catch a regression here. */
    SDL_HideWindow(info.window);
    const auto began = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 6; ++i) update();
    success = std::chrono::steady_clock::now() - began < std::chrono::seconds(2);
    SDL_ShowWindow(info.window);
  }
  success = success && aurora_desktop_enable(true);
  success = success && render_until(2, 20);
  success = success && aurora_desktop_enable(false);
  aurora_shutdown();
  success = success && !exit_requested && errors.load() == 0 &&
            mapped_frames.load() >= 2 && presented_frames.load() >= 2;
  std::fprintf(success ? stdout : stderr,
      "Linux GPU smoke %s: %u overlay generations mapped, %u presented, %u errors; clean shutdown reached.\n",
      success ? "PASS" : "FAIL", mapped_frames.load(), presented_frames.load(), errors.load());
  return success ? 0 : 1;
}
