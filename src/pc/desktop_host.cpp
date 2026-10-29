/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_host.h"
#include "desktop_game.h"
#include "launcher.h"
#include "pc.h"
#include "android_hooks.h"

#include <aurora/desktop.h>
#include <SDL3/SDL.h>

namespace {
bool initialized;
bool presentation_failed;
}

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {
constexpr UINT kTrayMessage = WM_APP + 72;
constexpr UINT kTrayId = 1;
constexpr int kSettingsHotkey = 0x4d10;
constexpr UINT kSettingsCommand = 100;
constexpr UINT kQuitCommand = 101;
constexpr wchar_t kWindowClass[] = L"DesktopMeleeControls_v1";
HWND controls_window;
NOTIFYICONDATAW tray{};
bool registered_hotkey;
bool toggle_requested;
UINT taskbar_created;

void add_tray_icon() {
    tray = {};
    tray.cbSize = sizeof(tray);
    tray.hWnd = controls_window;
    tray.uID = kTrayId;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = kTrayMessage;
    tray.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    lstrcpynW(tray.szTip, L"Desktop Melee | Right-click for controls", ARRAYSIZE(tray.szTip));
    if (!Shell_NotifyIconW(NIM_ADD, &tray))
        pc_log_line("desktop: tray icon unavailable; Ctrl+Shift+F10 opens settings");
}

void show_controls() {
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"Desktop Melee - Windows preview 6");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kSettingsCommand,
        pc_menu_is_open() ? L"Resume desktop match\tCtrl+Shift+F10" :
                            L"Pause / settings\tCtrl+Shift+F10");
    AppendMenuW(menu, MF_STRING, kQuitCommand, L"Quit\tCtrl+Shift+F11");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(controls_window);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        cursor.x, cursor.y, 0, controls_window, nullptr);
    PostMessageW(controls_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command == kSettingsCommand)
        toggle_requested = true;
    else if (command == kQuitCommand)
        pc_exit_requested = true;
}

LRESULT CALLBACK controls_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbar_created && message == taskbar_created) {
        add_tray_icon();
        return 0;
    }
    switch (message) {
    case WM_HOTKEY:
        if (wparam == kSettingsHotkey)
            toggle_requested = true;
        return 0;
    case kTrayMessage:
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU)
            show_controls();
        else if (lparam == WM_LBUTTONDBLCLK)
            toggle_requested = true;
        return 0;
    case WM_CLOSE:
        pc_exit_requested = true;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
} // namespace
#endif

extern "C" bool pc_desktop_host_init(SDL_Window* window) {
    if (!pc_desktop_requested())
        return true;
#if defined(_WIN32)
    pc_log_line("desktop: Windows - %s - GitHub startup updates, wallpaper contours and taskbar collision correction",
        DESKTOP_MELEE_VERSION);
    AuroraDesktopGeometry geometry{};
    if (!aurora_desktop_get_geometry(&geometry) || !geometry.width || !geometry.height) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Desktop Melee",
            "Could not determine the selected display and taskbar. "
            "This preview needs a normal Windows desktop with a bottom taskbar.", window);
        return false;
    }
    WNDCLASSW cls{};
    cls.lpfnWndProc = controls_proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = kWindowClass;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    /* A hidden top-level window receives Explorer's TaskbarCreated broadcast. */
    controls_window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Desktop Melee Controls",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, cls.hInstance, nullptr);
    if (!controls_window)
        return false;
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    registered_hotkey = RegisterHotKey(controls_window, kSettingsHotkey,
        MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F10) != 0;
    if (!registered_hotkey)
        pc_log_line("desktop: Ctrl+Shift+F10 is in use; use the tray icon for settings");
    add_tray_icon();
    initialized = true;
    pc_log_line("desktop: selected monitor %ux%u at %d,%d, platform (%d,%d)-(%d,%d)",
        geometry.width, geometry.height, geometry.screen_x, geometry.screen_y,
        geometry.platform_left, geometry.platform_top,
        geometry.platform_right, geometry.platform_bottom);
    return true;
#elif defined(__ANDROID__)
    AuroraDesktopGeometry geometry{};
    if (!pc_android_get_desktop_geometry(&geometry)) {
        pc_log_line("desktop: Android arena geometry unavailable");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Desktop Melee",
            "Could not determine the Android combat area.", window);
        return false;
    }
    initialized = true;
    pc_log_line("desktop: Android full-engine preview - arena %ux%u floor (%d,%d)-(%d,%d)",
        geometry.width, geometry.height, geometry.platform_left, geometry.platform_top,
        geometry.platform_right, geometry.platform_bottom);
    return true;
#elif defined(__linux__) && !defined(__ANDROID__)
    AuroraDesktopGeometry geometry{};
    if (!aurora_desktop_get_geometry(&geometry) || !geometry.width || !geometry.height) {
        const char* reason = aurora_desktop_unavailable_reason();
        pc_log_line("desktop: Linux overlay unavailable: %s", reason);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Desktop Melee",
            reason, window);
        return false;
    }
    initialized = true;
    pc_log_line("desktop: Linux X11 preview - %s", DESKTOP_MELEE_VERSION);
    pc_log_line("desktop: primary monitor %ux%u at %d,%d, panel (%d,%d)-(%d,%d)",
        geometry.width, geometry.height, geometry.screen_x, geometry.screen_y,
        geometry.platform_left, geometry.platform_top,
        geometry.platform_right, geometry.platform_bottom);
    return true;
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Desktop Melee",
        "Desktop overlays are supported on Windows and X11 Linux desktops. "
        "Regular windowed mode remains available on this platform.", window);
    return false;
#endif
}

extern "C" void pc_desktop_host_tick(void) {
    if (!pc_desktop_requested())
        return;
    if (!initialized)
        return;
#if defined(_WIN32)
    /* SDL pumps this thread's native windows too. Consume commands here so
     * no menu or renderer mutation occurs inside a window-message callback. */
    if (toggle_requested) {
        toggle_requested = false;
        pc_menu_toggle();
    }
#elif defined(__linux__) && !defined(__ANDROID__)
    if (aurora_desktop_take_settings_request())
        pc_menu_toggle();
#elif defined(__ANDROID__)
    /* Android uses the ordinary SDL/Vulkan activity surface for this first
     * full-engine milestone. Desktop arena rules are active, but transparent
     * home-screen presentation is a separate follow-up. */
    return;
#endif
    const bool wanted = pc_desktop_match_active() && !pc_menu_is_open() && !pc_exit_requested;
    if (wanted != aurora_desktop_active() && !presentation_failed) {
        if (!aurora_desktop_enable(wanted)) {
            presentation_failed = true;
            aurora_desktop_enable(false);
            pc_log_line("desktop: transparent presentation failed; restored the ordinary game window");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Desktop Melee",
                "The desktop compositor could not start the transparent overlay. The regular game window "
                "has been restored. Close it to exit; the log contains the graphics error.", nullptr);
        }
    }
}

extern "C" void pc_desktop_host_shutdown(void) {
    if (!initialized)
        return;
    initialized = false;
    aurora_desktop_enable(false);
#if defined(__ANDROID__)
    return;
#elif defined(_WIN32)
    Shell_NotifyIconW(NIM_DELETE, &tray);
    if (registered_hotkey)
        UnregisterHotKey(controls_window, kSettingsHotkey);
    if (controls_window)
        DestroyWindow(controls_window);
    controls_window = nullptr;
#endif
}
