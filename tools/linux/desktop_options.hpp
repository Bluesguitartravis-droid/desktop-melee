/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace desktop_linux {

struct Fighter { const char* name; int id; };
inline constexpr Fighter fighters[] = {
    {"Bowser", 5}, {"Captain Falcon", 0}, {"Donkey Kong", 1},
    {"Dr. Mario", 22}, {"Falco", 20}, {"Fox", 2}, {"Ganondorf", 25},
    {"Ice Climbers", 14}, {"Jigglypuff", 15}, {"Kirby", 4}, {"Link", 6},
    {"Luigi", 7}, {"Mario", 8}, {"Marth", 9}, {"Mewtwo", 10},
    {"Mr. Game & Watch", 3}, {"Ness", 11}, {"Peach", 12}, {"Pichu", 24},
    {"Pikachu", 13}, {"Roy", 23}, {"Samus", 16}, {"Sheik", 19},
    {"Yoshi", 17}, {"Young Link", 21}, {"Zelda", 18}
};

inline bool valid_fighter(int id) {
    for (const auto& fighter : fighters) if (fighter.id == id) return true;
    return false;
}

struct Options {
    std::string disc;
    int mode = 1; // 0: human/CPU; 1: watch two CPUs; 2: two controllers
    int p1 = 2, p2 = 20;
    int costume1 = 0, costume2 = 0;
    int cpu = 9, stocks = 4;
    bool ucf = true, windows = true;
};

inline bool parse_integer(std::string_view value, int& out, int low, int high) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed < low || parsed > high) return false;
    out = parsed;
    return true;
}

inline Options read_options(std::istream& input) {
    Options result;
    std::string line;
    size_t bytes = 0;
    while (std::getline(input, line)) {
        bytes += line.size() + 1;
        if (bytes > 65536) break;
        const auto separator = line.find(' ');
        if (separator == std::string::npos) continue;
        const auto key = std::string_view(line).substr(0, separator);
        const auto value = std::string_view(line).substr(separator + 1);
        if (key == "disc") {
            std::istringstream quoted{std::string(value)};
            std::string path;
            if (quoted >> std::quoted(path) && path.size() < 8192 &&
                path.find('\0') == std::string::npos && path.find('\n') == std::string::npos)
                result.disc = std::move(path);
        } else if (key == "mode") parse_integer(value, result.mode, 0, 2);
        else if (key == "p1") parse_integer(value, result.p1, 0, 25);
        else if (key == "p2") parse_integer(value, result.p2, 0, 25);
        else if (key == "costume1") parse_integer(value, result.costume1, 0, 5);
        else if (key == "costume2") parse_integer(value, result.costume2, 0, 5);
        else if (key == "cpu") parse_integer(value, result.cpu, 1, 9);
        else if (key == "stocks") parse_integer(value, result.stocks, 1, 9);
        else if (key == "ucf") {
            int enabled = 1;
            if (parse_integer(value, enabled, 0, 1)) result.ucf = enabled != 0;
        } else if (key == "windows") {
            int enabled = 1;
            if (parse_integer(value, enabled, 0, 1)) result.windows = enabled != 0;
        }
    }
    return result;
}

inline std::string write_options(const Options& options) {
    std::ostringstream out;
    out << "disc " << std::quoted(options.disc) << '\n'
        << "mode " << options.mode << '\n'
        << "p1 " << options.p1 << '\n' << "p2 " << options.p2 << '\n'
        << "costume1 " << options.costume1 << '\n' << "costume2 " << options.costume2 << '\n'
        << "cpu " << options.cpu << '\n' << "stocks " << options.stocks << '\n'
        << "ucf " << options.ucf << '\n' << "windows " << options.windows << '\n';
    return out.str();
}

inline std::map<std::string, std::string> match_environment(const Options& options,
                                                          bool overlay) {
    const auto number = [](int value) { return std::to_string(value); };
    return {
        {"MELEE_DESKTOP", overlay ? "1" : "0"},
        {"MELEE_DESKTOP_P1", number(options.p1)}, {"MELEE_DESKTOP_P2", number(options.p2)},
        {"MELEE_DESKTOP_COSTUME1", number(options.costume1)},
        {"MELEE_DESKTOP_COSTUME2", number(options.costume2)},
        {"MELEE_DESKTOP_CPU", number(options.mode == 2 ? 0 : options.cpu)},
        {"MELEE_DESKTOP_WATCH", options.mode == 1 ? "1" : "0"},
        {"MELEE_DESKTOP_STOCKS", number(options.stocks)},
        {"MELEE_DESKTOP_WINDOWS", options.windows ? "1" : "0"},
        {"MELEE_DESKTOP_WALLPAPER", "0"},
        {"MELEE_UCF", options.ucf ? "1" : "0"},
        {"MELEE_BOOT_SCENE", overlay ? "vs" : ""},
        {"MELEE_WINDOW_TITLE", "Desktop Melee"},
    };
}

enum class Session { X11, Wayland, GamingMode, Unknown };

inline Session identify_session(std::string_view type, std::string_view wayland,
                                std::string_view desktop, std::string_view display,
                                std::string_view gamescope) {
    std::string folded(desktop);
    for (char& ch : folded) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (!gamescope.empty() || folded.find("gamescope") != std::string::npos)
        return Session::GamingMode;
    if (type == "wayland" || !wayland.empty()) return Session::Wayland;
    if ((type.empty() || type == "x11") && !display.empty()) return Session::X11;
    return Session::Unknown;
}

} // namespace desktop_linux
