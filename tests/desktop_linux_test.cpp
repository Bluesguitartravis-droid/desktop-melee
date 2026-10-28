/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../tools/linux/desktop_options.hpp"
#include "../tools/linux/desktop_update.hpp"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {
unsigned checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
std::string asset(const std::string& tag, const std::string& name,
                  const std::string& url = "", const std::string& bytes = "20") {
    const std::string endpoint = url.empty() ? "https://github.com/Bluesguitartravis-droid/desktop-melee/releases/download/" + tag + "/" + name : url;
    return "{\"name\":\"" + name + "\",\"state\":\"uploaded\",\"size\":" + bytes +
           ",\"browser_download_url\":\"" + endpoint + "\"}";
}
std::string release(const std::string& number, const std::string& assets,
                    bool preview = true, bool draft = false) {
    const std::string tag = "desktop-v" + number;
    return "{\"tag_name\":\"" + tag + "\",\"draft\":" + (draft ? "true" : "false") +
        ",\"prerelease\":" + (preview ? "true" : "false") +
        ",\"html_url\":\"https://github.com/Bluesguitartravis-droid/desktop-melee/releases/tag/" + tag +
        "\",\"assets\":[" + assets + "]}";
}
}

int main() {
    using namespace desktop_linux;
    using updates::Status;
    Options settings;
    settings.disc = "/home/deck/Games/Mêlée \"USA\" $(nothing); 'test'.iso";
    settings.mode = 2;
    settings.p1 = 19;
    settings.costume1 = 4;
    settings.windows = false;
    std::istringstream serialized(write_options(settings));
    const auto roundtrip = read_options(serialized);
    check(roundtrip.disc == settings.disc, "disc path round-trips unicode, quotes, and shell punctuation as data");
    check(roundtrip.p1 == 19 && roundtrip.mode == 2 && roundtrip.costume1 == 4 && !roundtrip.windows,
          "all saved match options round-trip");
    std::istringstream malformed("mode 99\np1 -1\ncpu 0\nstocks 999\ncostume1 garbage\nucf 42\nwindows -1\n");
    auto defaults = read_options(malformed);
    check(defaults.mode == 1 && defaults.p1 == 2 && defaults.cpu == 9 && defaults.stocks == 4 && defaults.costume1 == 0 && defaults.ucf && defaults.windows,
          "invalid settings retain safe defaults");
    auto match = match_environment(settings, true);
    check(match.at("MELEE_DESKTOP_CPU") == "0" && match.at("MELEE_DESKTOP_WATCH") == "0", "two controllers disable CPUs");
    settings.mode = 1;
    match = match_environment(settings, true);
    check(match.at("MELEE_DESKTOP_CPU") == "9" && match.at("MELEE_DESKTOP_WATCH") == "1", "watch mode enables both CPUs");
    check(match.at("MELEE_DESKTOP_P1") == "19", "external character ID preserved");
    match = match_environment(settings, false);
    check(match.at("MELEE_DESKTOP") == "0" && match.at("MELEE_BOOT_SCENE").empty(), "windowed mode leaves ordinary game menus available");
    check(identify_session("x11", "", "KDE", ":0", "") == Session::X11, "native Plasma X11 detected");
    check(identify_session("wayland", "wayland-0", "KDE", ":0", "") == Session::Wayland, "XWayland DISPLAY does not impersonate X11 session");
    check(identify_session("x11", "wayland-0", "KDE", ":0", "") == Session::Wayland, "Wayland environment rejects ambiguous X11");
    check(identify_session("", "", "gamescope", ":0", "") == Session::GamingMode, "Gaming Mode desktop marker detected");
    check(identify_session("x11", "", "KDE", ":0", "gamescope-0") == Session::GamingMode, "Gamescope marker wins over X11");
    check(identify_session("", "", "", "", "") == Session::Unknown, "headless session never claims overlay support");
    check(identify_session("", "", "KDE", ":2", "") == Session::X11, "legacy X11 session without XDG session type accepted");

    check(updates::parse_version("0.1.7-linux.1").has_value(), "Linux preview version parses");
    check(!updates::parse_version("0.1.7-linux.0"), "zero preview rejected");
    check(!updates::parse_version("0.1.7-linux.01"), "leading-zero preview rejected");
    check(!updates::parse_version("0.1.7-beta.1"), "unrelated prerelease rejected");
    check(!updates::parse_version("0.1.7-linux.4294967296"), "overflow preview rejected");
    check(!updates::parse_version("0.1.7-linux.1/extra"), "tag path injection rejected");
    check(*updates::parse_version("0.1.7-linux.2") < *updates::parse_version("0.1.7"), "stable version sorts above same-base previews");
    check(*updates::parse_version("0.1.7-linux.2") < *updates::parse_version("0.1.8-linux.1"), "numeric core version dominates preview number");
    const std::string current = "0.1.7-linux.1";
    const std::string next = "0.1.7-linux.2";
    const std::string tag = "desktop-v" + next;
    const auto next_image = asset(tag, updates::appimage);
    const auto next_archive = asset(tag, updates::archive);
    const auto next_release = release(next, next_image);
    auto chosen = updates::select("[" + next_release + "]", current);
    check(chosen.status == Status::Available && chosen.version == next, "new preview selected");
    check(chosen.url.ends_with(updates::appimage), "Linux AppImage selected");
    chosen = updates::select("[" + release(next, next_archive + "," + next_image) + "]", current);
    check(chosen.url.ends_with(updates::appimage), "AppImage preferred even when archive listed first");
    chosen = updates::select("[" + release(next, next_archive) + "]", current);
    check(chosen.status == Status::Available && chosen.url.ends_with(updates::archive), "portable archive fallback supported");
    chosen = updates::select("[" + next_release + "]", next);
    check(chosen.status == Status::UpToDate, "same version reports current");
    chosen = updates::select("[" + next_release + "]", "0.1.8-linux.1");
    check(chosen.status == Status::UpToDate, "newer installed version never downgraded");
    chosen = updates::select("[" + release(next, next_image, true, true) + "]", current);
    check(chosen.status == Status::Unavailable, "draft releases rejected");
    chosen = updates::select("[" + release(next, next_image, false) + "]", current);
    check(chosen.status == Status::Unavailable, "preview tag requires prerelease metadata");
    chosen = updates::select("[" + release("0.1.8", asset("desktop-v0.1.8", updates::appimage), false) + "]", current);
    check(chosen.status == Status::Available && chosen.version == "0.1.8", "future stable Linux release supported");
    chosen = updates::select("[" + release("99.0.0", asset("desktop-v99.0.0", "Desktop-Melee-Setup-x64.exe"), false) + "," + next_release + "]", current);
    check(chosen.status == Status::Available && chosen.version == next, "Windows-only stable release ignored");
    chosen = updates::select("[" + release(next, asset(tag, updates::appimage, "https://evil.example/malware")) + "]", current);
    check(chosen.status == Status::Unavailable, "foreign download host rejected");
    chosen = updates::select("[" + release(next, asset(tag, updates::appimage, "https://github.com/elsewhere/repo/releases/download/" + tag + "/" + updates::appimage)) + "]", current);
    check(chosen.status == Status::Unavailable, "wrong repository download rejected");
    chosen = updates::select("[" + release(next, asset(tag, updates::appimage, "", "0")) + "]", current);
    check(chosen.status == Status::Unavailable, "empty asset rejected");
    chosen = updates::select("[" + release(next, asset(tag, updates::appimage, "", "-5")) + "]", current);
    check(chosen.status == Status::Unavailable, "negative asset size rejected");
    chosen = updates::select("{\"tag_name\":\"desktop-v0.1.7-linux.2\"}", current);
    check(chosen.status == Status::Unavailable, "non-list API result rejected");
    chosen = updates::select("[{}", current);
    check(chosen.status == Status::Unavailable, "truncated API response rejected");
    chosen = updates::select("[]", current);
    check(chosen.status == Status::Unavailable, "no release is not a false up-to-date claim");
    std::printf("%u Linux launcher/update checks passed\n", checks);
}
