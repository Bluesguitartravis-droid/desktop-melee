/* SPDX-License-Identifier: GPL-3.0-or-later
 * Linux/Steam Deck launcher. Disc paths are passed directly to posix_spawn;
 * neither user input nor downloaded release metadata is run through a shell.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "desktop_options.hpp"
#include "desktop_update.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <misc/cpp/imgui_stdlib.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

#ifndef DESKTOP_MELEE_VERSION
#define DESKTOP_MELEE_VERSION "0.1.7"
#endif
#ifndef DESKTOP_MELEE_LINUX_PREVIEW
#define DESKTOP_MELEE_LINUX_PREVIEW 1
#endif
#define DESKTOP_STRING_INNER(value) #value
#define DESKTOP_STRING(value) DESKTOP_STRING_INNER(value)

namespace {
using namespace desktop_linux;
namespace fs = std::filesystem;
constexpr const char* version = DESKTOP_MELEE_VERSION "-linux." DESKTOP_STRING(DESKTOP_MELEE_LINUX_PREVIEW);
volatile sig_atomic_t signal_close_requested = 0;
void request_close_signal(int) { signal_close_requested = 1; }

std::string environment(const char* key) {
    const char* value = std::getenv(key);
    return value ? value : "";
}

struct Dialog {
    std::mutex mutex;
    std::string selected, error;
    bool pending = false, complete = false;
};

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    fs::path folder, config, log;
    Options options;
    Session session = Session::Unknown;
    std::shared_ptr<Dialog> dialog = std::make_shared<Dialog>();
    updates::Checker updates;
    pid_t game = 0;
    bool stopping = false, closing = false;
    std::string status = "Choose your own USA 1.02 Melee disc image to begin.";
} app;

void file_dialog(void* data, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<Dialog>> holder(static_cast<std::shared_ptr<Dialog>*>(data));
    const auto state = *holder;
    std::lock_guard lock(state->mutex);
    if (!files) state->error = SDL_GetError();
    else if (files[0]) state->selected = files[0];
    state->complete = true;
    state->pending = false;
}

void browse() {
    {
        std::lock_guard lock(app.dialog->mutex);
        if (app.dialog->pending) return;
        app.dialog->pending = true;
        app.dialog->complete = false;
        app.dialog->selected.clear();
        app.dialog->error.clear();
    }
    static const SDL_DialogFileFilter filters[] = {
        {"GameCube disc images", "iso;gcm;ciso;rvz;gcz;wia"}, {"All files", "*"}
    };
    SDL_ShowOpenFileDialog(file_dialog, new std::shared_ptr<Dialog>(app.dialog), app.window,
        filters, SDL_arraysize(filters), app.options.disc.empty() ? nullptr : app.options.disc.c_str(), false);
}

void poll_dialog() {
    std::lock_guard lock(app.dialog->mutex);
    if (!app.dialog->complete) return;
    if (!app.dialog->selected.empty()) {
        app.options.disc = app.dialog->selected;
        app.status = "Disc selected. The game will validate it when you start.";
    } else if (!app.dialog->error.empty()) {
        app.status = "File picker unavailable. Paste a full path or drop your disc image onto this window.";
    }
    app.dialog->complete = false;
}

bool save_options() {
    if (app.config.empty()) return false;
    std::error_code error;
    fs::create_directories(app.config.parent_path(), error);
    if (error) return false;
    std::string temporary = app.config.string() + ".tmp.XXXXXX";
    const int file = mkstemp(temporary.data());
    if (file < 0) return false;
    const std::string data = write_options(app.options);
    size_t position = 0;
    bool good = true;
    while (position < data.size()) {
        const auto count = write(file, data.data() + position, data.size() - position);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { good = false; break; }
        position += static_cast<size_t>(count);
    }
    if (good && fsync(file) != 0) good = false;
    if (close(file) != 0) good = false;
    if (good && rename(temporary.c_str(), app.config.c_str()) != 0) good = false;
    if (!good) unlink(temporary.c_str());
    return good;
}

void stop_game() {
    if (app.game <= 0 || app.stopping) return;
    // The game handles SIGTERM through its normal save-and-shutdown path.
    // Never send a second signal: the handler intentionally restores defaults.
    if (kill(app.game, SIGTERM) == 0 || errno == ESRCH) {
        app.stopping = true;
        app.status = "Closing the match and saving...";
    } else app.status = "Could not ask the match to close: " + std::string(std::strerror(errno));
}

void poll_game() {
    if (app.game <= 0) return;
    int status = 0;
    const pid_t exited = waitpid(app.game, &status, WNOHANG);
    if (exited == 0 || (exited < 0 && errno == EINTR)) return;
    if (exited < 0 && errno != ECHILD) return;
    app.game = 0;
    app.stopping = false;
    if (exited > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0)
        app.status = "Match closed. Your settings and saves are kept in your user profile.";
    else app.status = "The game stopped. Open the log for details, then try again.";
    if (!app.closing) { SDL_RestoreWindow(app.window); SDL_RaiseWindow(app.window); }
}

void start_game(bool overlay) {
    if (app.game > 0) return;
    if (overlay && app.session != Session::X11) {
        app.status = "Desktop overlay requires an X11 desktop session. Windowed play is available below.";
        return;
    }
    std::error_code error;
    const fs::path disc = fs::absolute(app.options.disc, error);
    if (app.options.disc.empty() || error || !fs::is_regular_file(disc, error) || error) {
        app.status = "Select a disc image that is available on this device.";
        return;
    }
    const fs::path engine = app.folder / "melee";
    if (access(engine.c_str(), X_OK) != 0) {
        app.status = "The game executable is missing. Download the complete AppImage or extract the complete archive.";
        return;
    }
    app.options.disc = disc.string();
    const bool saved = save_options();
    std::map<std::string, std::string> values;
    for (char** entry = environ; *entry; ++entry) {
        const std::string variable = *entry;
        const auto split = variable.find('=');
        if (split == std::string::npos) continue;
        const auto key = variable.substr(0, split);
        // Exclude inherited test/online/boot overrides. Launcher's local match
        // and selected disc remain authoritative.
        if (!key.starts_with("MELEE_")) values[key] = variable.substr(split + 1);
    }
    for (const auto& [key, value] : match_environment(app.options, overlay)) values[key] = value;
    if (!app.log.empty()) values["MELEE_LOG_FILE"] = app.log.string();
    std::vector<std::string> environment_strings;
    for (const auto& [key, value] : values) environment_strings.push_back(key + "=" + value);
    std::vector<char*> environment_pointers;
    for (auto& entry : environment_strings) environment_pointers.push_back(entry.data());
    environment_pointers.push_back(nullptr);
    std::vector<std::string> arguments{engine.string()};
    if (overlay) arguments.emplace_back("--desktop");
    arguments.push_back(disc.string());
    std::vector<char*> argument_pointers;
    for (auto& entry : arguments) argument_pointers.push_back(entry.data());
    argument_pointers.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    int result = posix_spawn_file_actions_init(&actions);
    if (result == 0) {
        result = posix_spawn_file_actions_addchdir_np(&actions, app.folder.c_str());
        if (result == 0) result = posix_spawn(&app.game, engine.c_str(), &actions, nullptr,
                                             argument_pointers.data(), environment_pointers.data());
        posix_spawn_file_actions_destroy(&actions);
    }
    if (result != 0) {
        app.game = 0;
        app.status = "Could not start the game: " + std::string(std::strerror(result));
        return;
    }
    app.stopping = false;
    app.status = overlay ? "Desktop match running. Ctrl+Shift+F10 opens settings; Ctrl+Shift+F11 quits." :
                           "Windowed Melee running. Use its menus to choose a match and configure controls.";
    if (!saved) app.status += " Launcher settings could not be saved.";
    SDL_MinimizeWindow(app.window);
}

void fighter_combo(const char* label, int& id) {
    const char* current = "Fox";
    for (const auto& fighter : fighters) if (fighter.id == id) current = fighter.name;
    if (ImGui::BeginCombo(label, current)) {
        for (const auto& fighter : fighters) {
            const bool selected = fighter.id == id;
            if (ImGui::Selectable(fighter.name, selected)) id = fighter.id;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void open_log() {
    if (app.log.empty() || !fs::exists(app.log)) {
        app.status = "A log will be created when the game starts.";
        return;
    }
    std::string url = "file://";
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : app.log.string()) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '/' || ch == '.' || ch == '-' || ch == '_') url += ch;
        else { url += '%'; url += hex[ch >> 4]; url += hex[ch & 15]; }
    }
    if (!SDL_OpenURL(url.c_str())) app.status = "Log: " + app.log.string();
}

void draw() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Desktop Melee", nullptr, ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(0.42f, 0.85f, 0.96f, 1), "DESKTOP MELEE");
    ImGui::Text("Linux preview %s", version);
    ImGui::Spacing();
    if (app.session == Session::X11)
        ImGui::TextWrapped("Desktop overlay: X11 session detected. Your panel is the floor and visible window tops are platforms.");
    else if (app.session == Session::Wayland)
        ImGui::TextWrapped("This is a Wayland desktop. The overlay preview needs X11 Desktop Mode; windowed Melee works here. On Steam Deck, select X11 in Steam's developer settings, then enter Desktop Mode again.");
    else if (app.session == Session::GamingMode)
        ImGui::TextWrapped("Steam Gaming Mode supports windowed Melee here. For the desktop overlay, enter an X11 Desktop Mode session. This preview does not draw fighters over Steam's Gaming Mode interface.");
    else
        ImGui::TextWrapped("An X11 desktop was not detected. Use windowed Melee, or start this launcher from an X11 desktop for overlay play.");
    ImGui::Separator();
    ImGui::BeginDisabled(app.game > 0);
    ImGui::TextUnformatted("Your Melee USA 1.02 disc image");
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(width - 122.0f);
    ImGui::InputTextWithHint("##disc", "Choose a local file, paste its path, or drop it here", &app.options.disc);
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(110, 0))) browse();
    ImGui::Spacing();
    ImGui::BeginDisabled(app.session != Session::X11);
    ImGui::SetNextItemWidth(-1);
    const char* modes[] = {"Play against a CPU", "Watch two CPUs", "Two controllers"};
    ImGui::Combo("##mode", &app.options.mode, modes, SDL_arraysize(modes));
    if (ImGui::BeginTable("fighters", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1); fighter_combo("##fighter1", app.options.p1);
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("##costume1", &app.options.costume1, 0, 5, "P1 costume %d");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1); fighter_combo("##fighter2", app.options.p2);
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("##costume2", &app.options.costume2, 0, 5, "P2 costume %d");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("##cpu", &app.options.cpu, 1, 9, "CPU level %d");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1); ImGui::SliderInt("##stocks", &app.options.stocks, 1, 9, "%d stocks");
        ImGui::EndTable();
    }
    ImGui::Checkbox("Window platforms", &app.options.windows);
    ImGui::SameLine(); ImGui::Checkbox("UCF", &app.options.ucf);
    ImGui::TextDisabled("Wallpaper terrain is not included in this Linux preview.");
    ImGui::Spacing();
    if (ImGui::Button("Start desktop match", ImVec2(240, 42))) start_game(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Open windowed Melee", ImVec2(240, 42))) start_game(false);
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextWrapped("Desktop controls: controller ports 1 and 2. Keep Steam running on Steam Deck, and select a Gamepad layout for this app in Steam Input. F1 opens settings in the game window.");
    ImGui::Separator();
    ImGui::TextWrapped("%s", app.status.c_str());
    ImGui::BeginDisabled(app.game <= 0 || app.stopping);
    if (ImGui::Button("Stop match")) stop_game();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Open log")) open_log();
    ImGui::SameLine();
    if (ImGui::Button("Close")) { app.closing = true; stop_game(); }
    ImGui::Separator();
    const auto update = app.updates.poll();
    if (update.status == updates::Status::Available) {
        ImGui::Text("Linux update available: %s", update.version.c_str());
        if (ImGui::Button("Download update")) {
            if (SDL_OpenURL(update.url.c_str()))
                app.status = "Download opened. Close the app, replace your AppImage, then mark the new file executable. Your user data stays in place.";
        }
    } else {
        ImGui::TextWrapped("%s", update.detail.c_str());
        ImGui::BeginDisabled(update.status == updates::Status::Checking);
        if (ImGui::Button("Check for updates")) app.updates.start(version);
        ImGui::EndDisabled();
    }
    ImGui::End();
}
} // namespace

int main(int argc, char** argv) {
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--version") { std::printf("Desktop Melee %s\n", version); return 0; }
        if (argument == "--help") {
            std::printf("Desktop Melee %s\nRun without arguments to open the Linux launcher.\n"
                        "Desktop overlay needs X11 Desktop Mode. Wayland and Gaming Mode offer windowed play.\n"
                        "--smoke-test initializes and renders the launcher without a disc or network request.\n", version);
            return 0;
        }
        if (argument == "--smoke-test") smoke = true;
        else { std::fprintf(stderr, "Unknown launcher option: %s\n", argv[i]); return 2; }
    }
    SDL_SetAppMetadata("Desktop Melee", version, "io.github.BluesguitartravisDroid.DesktopMelee");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "Desktop Melee: %s\n", SDL_GetError()); return 1;
    }
    struct sigaction close_signal{};
    close_signal.sa_handler = request_close_signal;
    sigemptyset(&close_signal.sa_mask);
    sigaction(SIGTERM, &close_signal, nullptr);
    sigaction(SIGINT, &close_signal, nullptr);
    const char* base = SDL_GetBasePath();
    if (!base) { std::fprintf(stderr, "%s\n", SDL_GetError()); SDL_Quit(); return 1; }
    app.folder = fs::path(base);
    std::string config = environment("XDG_CONFIG_HOME");
    if (config.empty() || !fs::path(config).is_absolute()) {
        const auto home = environment("HOME");
        if (!home.empty()) config = (fs::path(home) / ".config").string();
    }
    if (!config.empty()) {
        app.config = fs::path(config) / "DesktopMelee" / "settings.conf";
        std::error_code config_error;
        const auto bytes = fs::file_size(app.config, config_error);
        if (!config_error && bytes <= 65536) {
            std::ifstream input(app.config);
            if (input) app.options = read_options(input);
        }
    }
    if (char* state = SDL_GetPrefPath(nullptr, "DesktopMelee")) {
        app.log = fs::path(state) / "desktop-melee.log";
        SDL_free(state);
    }
    app.session = identify_session(environment("XDG_SESSION_TYPE"), environment("WAYLAND_DISPLAY"),
        environment("XDG_CURRENT_DESKTOP"), environment("DISPLAY"), environment("GAMESCOPE_WAYLAND_DISPLAY"));
    const char* driver = SDL_GetCurrentVideoDriver();
    if (app.session == Session::X11 && (!driver || std::strcmp(driver, "x11") != 0)) app.session = Session::Unknown;
    app.window = SDL_CreateWindow("Desktop Melee - Linux", 850, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app.window) { std::fprintf(stderr, "%s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_SetWindowMinimumSize(app.window, 640, 480);
    app.renderer = SDL_CreateRenderer(app.window, nullptr);
    if (!app.renderer) {
        std::fprintf(stderr, "%s\n", SDL_GetError()); SDL_DestroyWindow(app.window); SDL_Quit(); return 1;
    }
    SDL_SetRenderVSync(app.renderer, 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    const auto font = app.folder / "resources" / "font.ttf";
    if (fs::is_regular_file(font)) io.Fonts->AddFontFromFileTTF(font.c_str(), 18.0f);
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(22, 18);
    style.FramePadding = ImVec2(10, 7);
    style.ItemSpacing = ImVec2(12, 10);
    style.FrameRounding = 5;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(.055f, .075f, .11f, 1);
    style.Colors[ImGuiCol_Button] = ImVec4(.10f, .29f, .38f, 1);
    ImGui_ImplSDL3_InitForSDLRenderer(app.window, app.renderer);
    ImGui_ImplSDLRenderer3_Init(app.renderer);
    if (!smoke) app.updates.start(version);
    unsigned frames = 0;
    while (!app.closing || app.game > 0) {
        if (signal_close_requested) { app.closing = true; stop_game(); }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(app.window))) {
                app.closing = true; stop_game();
            }
            if (event.type == SDL_EVENT_DROP_FILE && !app.game && event.drop.data) {
                app.options.disc = event.drop.data;
                app.status = "Disc selected. The game will validate it when you start.";
            }
        }
        poll_game(); poll_dialog();
        if (app.closing && app.game <= 0) break;
        if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED) { SDL_Delay(50); continue; }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        SDL_SetRenderDrawColor(app.renderer, 14, 19, 28, 255);
        SDL_RenderClear(app.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), app.renderer);
        SDL_RenderPresent(app.renderer);
        if (smoke && ++frames == 3) break;
        SDL_Delay(10);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
