/* SPDX-License-Identifier: GPL-3.0-or-later
 * Small native entry point. It never downloads a disc or runs a shell.
 */
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include "desktop_update.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kClass[] = L"DesktopMeleeLauncher_v1";
constexpr int kBaseWidth = 700;
constexpr int kBaseHeight = 660;
constexpr UINT_PTR kMatchTimer = 1;
constexpr UINT_PTR kUpdateTimer = 2;
#define DESKTOP_WIDEN_INNER(value) L##value
#define DESKTOP_WIDEN(value) DESKTOP_WIDEN_INNER(value)
constexpr wchar_t kVersion[] = DESKTOP_WIDEN(DESKTOP_MELEE_VERSION);
enum : int {
    IdTitle = 101, IdSubtitle, IdDiscLabel, IdDisc, IdBrowse,
    IdModeLabel, IdMode, IdP1Label, IdP1, IdP2Label, IdP2,
    IdCostume1Label, IdCostume1, IdCostume2Label, IdCostume2,
    IdLevelLabel, IdLevel, IdStocksLabel, IdStocks, IdUcf, IdWindows, IdWallpaper,
    IdInstructions, IdStatus, IdStart, IdStop, IdLogs,
    IdUpdateStatus, IdUpdate
};
struct FighterChoice { const wchar_t* name; int id; };
/* CharacterKind (external IDs), deliberately not the internal FighterKind. */
constexpr FighterChoice fighters[] = {
    {L"Bowser", 5}, {L"Captain Falcon", 0}, {L"Donkey Kong", 1},
    {L"Dr. Mario", 22}, {L"Falco", 20}, {L"Fox", 2}, {L"Ganondorf", 25},
    {L"Ice Climbers", 14}, {L"Jigglypuff", 15}, {L"Kirby", 4},
    {L"Link", 6}, {L"Luigi", 7}, {L"Mario", 8}, {L"Marth", 9},
    {L"Mewtwo", 10}, {L"Mr. Game & Watch", 3}, {L"Ness", 11},
    {L"Peach", 12}, {L"Pichu", 24}, {L"Pikachu", 13}, {L"Roy", 23},
    {L"Samus", 16}, {L"Sheik", 19}, {L"Yoshi", 17},
    {L"Young Link", 21}, {L"Zelda", 18}
};
struct State {
    HWND window{};
    HFONT font{}, heading{};
    HBRUSH background{};
    int dpi = 96;
    std::filesystem::path folder, config;
    bool settings_writable{};
    HANDLE process{};
    DWORD process_id{};
    desktop_update::UpdateChecker updates;
    desktop_update::UpdateResult update_result;
} app;

int px(int value) { return MulDiv(value, app.dpi, 96); }
HWND child(int id) { return GetDlgItem(app.window, id); }
void label(int id, const wchar_t* text) { SetWindowTextW(child(id), text); }
void status(const std::wstring& text) { label(IdStatus, text.c_str()); }

std::wstring from_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!length) return L"Update information could not be displayed.";
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length);
    return result;
}

void poll_updates() {
    using desktop_update::UpdateStatus;
    app.update_result = app.updates.poll();
    const auto& result = app.update_result;
    if (result.status == UpdateStatus::Checking) {
        label(IdUpdateStatus, L"Checking GitHub for updates...");
        label(IdUpdate, L"Checking...");
        EnableWindow(child(IdUpdate), FALSE);
        return;
    }
    KillTimer(app.window, kUpdateTimer);
    EnableWindow(child(IdUpdate), TRUE);
    if (result.status == UpdateStatus::Available) {
        const auto message = L"Version " + from_utf8(result.version) + L" is available. You have " + kVersion + L".";
        label(IdUpdateStatus, message.c_str());
        label(IdUpdate, result.direct_download ? L"Download update" : L"View release");
    } else {
        const auto message = result.status == UpdateStatus::UpToDate
            ? std::wstring(L"You're up to date (version ") + kVersion + L")."
            : from_utf8(result.detail);
        label(IdUpdateStatus, message.c_str());
        label(IdUpdate, L"Check for updates");
    }
}

void check_updates() {
    if (app.updates.start()) SetTimer(app.window, kUpdateTimer, 250, nullptr);
    poll_updates();
}

void update_action() {
    if (app.update_result.status != desktop_update::UpdateStatus::Available) {
        check_updates();
        return;
    }
    // The update module only returns URLs from this project's GitHub releases.
    // Open the Windows installer/package in the browser. Installation is explicit.
    const auto url = from_utf8(app.update_result.url);
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(app.window, L"open",
        url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    const wchar_t* message = L"Could not open your browser. Try the update button again.";
    if (result > 32) {
        if (app.update_result.installer)
            message = L"Download opened. Close Desktop Melee, then run the installer.";
        else if (app.update_result.direct_download)
            message = L"Stop the match, then extract the new portable ZIP to a new folder.";
        else
            message = L"Release page opened. Choose the Windows installer to update.";
    }
    label(IdUpdateStatus, message);
}

std::wstring text(int id) {
    int length = GetWindowTextLengthW(child(id));
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(child(id), value.data(), length + 1);
    value.resize(length);
    return value;
}

std::wstring error_text(DWORD code) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
        reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = message ? message : L"Unknown Windows error.";
    if (message) LocalFree(message);
    return result;
}

/* The profile APIs keep an existing INI's encoding. Without a UTF-16LE BOM,
 * even WritePrivateProfileStringW can lose a non-ASCII disc path. Seed new
 * files and migrate old ANSI files through a temporary file, so a failed
 * conversion/write never truncates the previous settings. */
bool prepare_unicode_config() {
    static_assert(sizeof(wchar_t) == 2);
    std::vector<char> bytes;
    HANDLE input = CreateFileW(app.config.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(input, &size) && size.QuadPart >= 0 &&
                  size.QuadPart <= 1024 * 1024;
        DWORD read = 0;
        if (ok) {
            bytes.resize(static_cast<size_t>(size.QuadPart));
            ok = bytes.empty() || (ReadFile(input, bytes.data(),
                static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
        }
        CloseHandle(input);
        if (!ok) return false;
        if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
            static_cast<unsigned char>(bytes[1]) == 0xfe)
            return true;
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        return false;
    }

    UINT codepage = CP_ACP;
    size_t offset = 0;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb &&
        static_cast<unsigned char>(bytes[2]) == 0xbf) {
        codepage = CP_UTF8;
        offset = 3;
    }
    std::wstring unicode(1, L'\ufeff');
    if (bytes.size() > offset) {
        const int count = static_cast<int>(bytes.size() - offset);
        int length = MultiByteToWideChar(codepage, 0, bytes.data() + offset,
            count, nullptr, 0);
        if (!length) return false;
        unicode.resize(static_cast<size_t>(length) + 1);
        if (MultiByteToWideChar(codepage, 0, bytes.data() + offset, count,
                unicode.data() + 1, length) != length)
            return false;
    }
    const auto temporary = app.config.wstring() + L".unicode-" +
        std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return false;
    const DWORD size = static_cast<DWORD>(unicode.size() * sizeof(wchar_t));
    DWORD written = 0;
    bool ok = WriteFile(output, unicode.data(), size, &written, nullptr) &&
              written == size && FlushFileBuffers(output);
    CloseHandle(output);
    if (ok)
        ok = MoveFileExW(temporary.c_str(), app.config.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

/* CommandLineToArgvW-compatible quoting. lpApplicationName is also explicit. */
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(c);
        } else {
            result.append(slashes, L'\\');
            result.push_back(c);
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

HWND make(int id, const wchar_t* kind, const wchar_t* value, DWORD style = 0,
          DWORD exstyle = 0) {
    HWND hwnd = CreateWindowExW(exstyle, kind, value, WS_CHILD | WS_VISIBLE | style,
        0, 0, 10, 10, app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    return hwnd;
}

void place(int id, int x, int y, int width, int height) {
    MoveWindow(child(id), px(x), px(y), px(width), px(height), TRUE);
}

void layout() {
    if (!child(IdStart)) return;
    place(IdTitle, 28, 22, 640, 40);
    place(IdSubtitle, 30, 67, 640, 24);
    place(IdDiscLabel, 30, 112, 640, 22);
    place(IdDisc, 30, 140, 522, 30);
    place(IdBrowse, 562, 139, 108, 32);
    place(IdModeLabel, 30, 192, 175, 22);
    place(IdMode, 210, 189, 460, 140);
    place(IdP1Label, 30, 237, 305, 22);
    place(IdP2Label, 365, 237, 305, 22);
    place(IdP1, 30, 263, 305, 320);
    place(IdP2, 365, 263, 305, 320);
    place(IdCostume1Label, 30, 306, 100, 22);
    place(IdCostume1, 132, 302, 203, 220);
    place(IdCostume2Label, 365, 306, 100, 22);
    place(IdCostume2, 467, 302, 203, 220);
    place(IdLevelLabel, 30, 350, 100, 22);
    place(IdLevel, 132, 346, 96, 240);
    place(IdStocksLabel, 365, 350, 100, 22);
    place(IdStocks, 467, 346, 96, 240);
    place(IdUcf, 30, 390, 305, 24);
    place(IdWindows, 365, 390, 305, 24);
    place(IdWallpaper, 30, 420, 640, 24);
    place(IdInstructions, 30, 458, 640, 48);
    place(IdStatus, 30, 512, 640, 32);
    place(IdLogs, 30, 553, 140, 34);
    place(IdStop, 365, 553, 130, 34);
    place(IdStart, 505, 551, 165, 38);
    place(IdUpdateStatus, 30, 608, 465, 40);
    place(IdUpdate, 510, 606, 160, 34);
}

void set_fonts() {
    if (app.font) DeleteObject(app.font);
    if (app.heading) DeleteObject(app.heading);
    app.font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    app.heading = CreateFontW(-px(30), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    EnumChildWindows(app.window, [](HWND hwnd, LPARAM) -> BOOL {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
        return TRUE;
    }, 0);
    if (child(IdTitle))
        SendMessageW(child(IdTitle), WM_SETFONT, reinterpret_cast<WPARAM>(app.heading), TRUE);
}

int selection(int id) {
    LRESULT index = SendMessageW(child(id), CB_GETCURSEL, 0, 0);
    return index == CB_ERR ? 0 : static_cast<int>(index);
}
void select(int id, int index) { SendMessageW(child(id), CB_SETCURSEL, index, 0); }
void item(int id, const wchar_t* value) {
    SendMessageW(child(id), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
}
int config_int(const wchar_t* key, int fallback, int low, int high) {
    UINT value = GetPrivateProfileIntW(L"Match", key, fallback, app.config.c_str());
    return std::clamp(static_cast<int>(value), low, high);
}
void save_int(const wchar_t* key, int value) {
    std::wstring string = std::to_wstring(value);
    WritePrivateProfileStringW(L"Match", key, string.c_str(), app.config.c_str());
}
int fighter_index(int id) {
    for (size_t i = 0; i < std::size(fighters); ++i)
        if (fighters[i].id == id) return static_cast<int>(i);
    return 5;
}
int fighter_id(int control) { return fighters[selection(control)].id; }
void update_mode() {
    int mode = selection(IdMode);
    EnableWindow(child(IdLevel), mode != 2);
    label(IdP1Label, mode == 1 ? L"CPU 1" : L"Player 1 / controller port 1");
    label(IdP2Label, mode == 2 ? L"Player 2 / controller port 2" : L"CPU opponent");
}

void make_form() {
    set_fonts();
    make(IdTitle, L"STATIC", L"Desktop Melee");
    SendMessageW(child(IdTitle), WM_SETFONT, reinterpret_cast<WPARAM>(app.heading), TRUE);
    make(IdSubtitle, L"STATIC", L"WINDOWS PREVIEW 6  /  VERSION " DESKTOP_WIDEN(DESKTOP_MELEE_VERSION));
    make(IdDiscLabel, L"STATIC", L"Your Melee USA 1.02 disc image");
    make(IdDisc, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
    make(IdBrowse, L"BUTTON", L"Browse...", WS_TABSTOP | BS_PUSHBUTTON);
    make(IdModeLabel, L"STATIC", L"Match mode");
    make(IdMode, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
    item(IdMode, L"Play against a CPU");
    item(IdMode, L"Watch two CPUs");
    item(IdMode, L"Two local players");
    make(IdP1Label, L"STATIC", L"Player 1 / controller port 1");
    make(IdP2Label, L"STATIC", L"CPU opponent");
    for (int id : {IdP1, IdP2}) {
        make(id, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        for (const auto& fighter : fighters) item(id, fighter.name);
    }
    make(IdCostume1Label, L"STATIC", L"Costume");
    make(IdCostume2Label, L"STATIC", L"Costume");
    for (int id : {IdCostume1, IdCostume2}) {
        make(id, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        item(id, L"Default");
        for (int i = 1; i < 6; ++i) item(id, (L"Alternate " + std::to_wstring(i)).c_str());
    }
    make(IdLevelLabel, L"STATIC", L"CPU level");
    make(IdLevel, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
    for (int i = 1; i <= 9; ++i) item(IdLevel, std::to_wstring(i).c_str());
    make(IdStocksLabel, L"STATIC", L"Stocks");
    make(IdStocks, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
    for (int i = 1; i <= 9; ++i) item(IdStocks, std::to_wstring(i).c_str());
    make(IdUcf, L"BUTTON", L"Enable UCF controller fixes", WS_TABSTOP | BS_AUTOCHECKBOX);
    make(IdWindows, L"BUTTON", L"Use open window tops as platforms", WS_TABSTOP | BS_AUTOCHECKBOX);
    make(IdWallpaper, L"BUTTON", L"Generate platforms from wallpaper (experimental)",
        WS_TABSTOP | BS_AUTOCHECKBOX);
    make(IdInstructions, L"STATIC",
        L"Use a controller while your desktop stays clickable. Right-click the tray icon for controls.\n"
        L"Ctrl+Shift+F10: pause / settings     Ctrl+Shift+F11: quit");
    make(IdStatus, L"STATIC", L"Experimental build. Start with a bottom taskbar on your primary display.");
    make(IdLogs, L"BUTTON", L"Open game folder", WS_TABSTOP | BS_PUSHBUTTON);
    make(IdStop, L"BUTTON", L"Stop match", WS_TABSTOP | BS_PUSHBUTTON);
    make(IdStart, L"BUTTON", L"Start desktop match", WS_TABSTOP | BS_DEFPUSHBUTTON);
    make(IdUpdateStatus, L"STATIC", L"Checking GitHub for updates...");
    make(IdUpdate, L"BUTTON", L"Checking...", WS_TABSTOP | BS_PUSHBUTTON);
    EnableWindow(child(IdUpdate), FALSE);
    EnableWindow(child(IdStop), FALSE);
    SendMessageW(child(IdDisc), EM_SETLIMITTEXT, 32760, 0);
    std::array<wchar_t, 32768> disc{};
    GetPrivateProfileStringW(L"Match", L"Disc", L"", disc.data(), disc.size(), app.config.c_str());
    SetWindowTextW(child(IdDisc), disc.data());
    select(IdMode, config_int(L"Mode", 0, 0, 2));
    select(IdP1, fighter_index(config_int(L"P1", 2, 0, 25)));
    select(IdP2, fighter_index(config_int(L"P2", 20, 0, 25)));
    select(IdCostume1, config_int(L"Costume1", 0, 0, 5));
    select(IdCostume2, config_int(L"Costume2", 0, 0, 5));
    select(IdLevel, config_int(L"CPU", 5, 1, 9) - 1);
    select(IdStocks, config_int(L"Stocks", 4, 1, 9) - 1);
    SendMessageW(child(IdUcf), BM_SETCHECK, config_int(L"UCF", 1, 0, 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(child(IdWindows), BM_SETCHECK,
        config_int(L"WindowPlatforms", 1, 0, 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(child(IdWallpaper), BM_SETCHECK,
        config_int(L"WallpaperPlatforms", 0, 0, 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    update_mode();
    layout();
    if (!app.settings_writable)
        status(L"Settings could not be saved here. You can still select a disc and start a match.");
}

void browse() {
    std::array<wchar_t, 32768> path{};
    std::wstring current = text(IdDisc);
    if (current.size() < path.size()) std::copy(current.begin(), current.end(), path.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFilter = L"GameCube disc images\0*.iso;*.gcm;*.ciso;*.rvz;*.gcz;*.wia\0All files\0*.*\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = path.size();
    dialog.lpstrTitle = L"Select your Melee USA 1.02 disc image";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog)) {
        SetWindowTextW(child(IdDisc), path.data());
        status(L"Disc selected. The game will check the image when it starts.");
    }
}

void env(const wchar_t* key, int value) {
    std::wstring str = std::to_wstring(value);
    SetEnvironmentVariableW(key, str.c_str());
}

BOOL CALLBACK close_game_window(HWND hwnd, LPARAM pid) {
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner == static_cast<DWORD>(pid)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}
void stop() {
    if (app.process_id) {
        EnumWindows(close_game_window, static_cast<LPARAM>(app.process_id));
        status(L"Closing the match...");
    }
}

void start() {
    if (app.process) return;
    const std::wstring disc = text(IdDisc);
    if (disc.empty()) { status(L"Choose your own USA 1.02 Melee disc image first."); browse(); return; }
    DWORD attributes = GetFileAttributesW(disc.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        status(L"That disc file could not be opened. Choose a file that is available on this PC.");
        return;
    }
    const auto engine = app.folder / L"melee.exe";
    if (GetFileAttributesW(engine.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(app.window,
            L"melee.exe is missing. Reinstall Desktop Melee, or re-extract the entire portable ZIP.",
            L"Desktop Melee", MB_OK | MB_ICONERROR);
        return;
    }
    const int mode = selection(IdMode);
    const int level = selection(IdLevel) + 1;
    const int stocks = selection(IdStocks) + 1;
    const int ucf = SendMessageW(child(IdUcf), BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int windows = SendMessageW(child(IdWindows), BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int wallpaper = SendMessageW(child(IdWallpaper), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (app.settings_writable) {
        WritePrivateProfileStringW(L"Match", L"Disc", disc.c_str(), app.config.c_str());
        save_int(L"Mode", mode);
        save_int(L"P1", fighter_id(IdP1)); save_int(L"P2", fighter_id(IdP2));
        save_int(L"Costume1", selection(IdCostume1)); save_int(L"Costume2", selection(IdCostume2));
        save_int(L"CPU", level); save_int(L"Stocks", stocks); save_int(L"UCF", ucf);
        save_int(L"WindowPlatforms", windows);
        save_int(L"WallpaperPlatforms", wallpaper);
    }
    env(L"MELEE_DESKTOP", 1);
    env(L"MELEE_DESKTOP_P1", fighter_id(IdP1)); env(L"MELEE_DESKTOP_P2", fighter_id(IdP2));
    env(L"MELEE_DESKTOP_COSTUME1", selection(IdCostume1));
    env(L"MELEE_DESKTOP_COSTUME2", selection(IdCostume2));
    env(L"MELEE_DESKTOP_CPU", mode == 2 ? 0 : level);
    env(L"MELEE_DESKTOP_WATCH", mode == 1 ? 1 : 0);
    env(L"MELEE_DESKTOP_STOCKS", stocks);
    env(L"MELEE_DESKTOP_WINDOWS", windows);
    env(L"MELEE_DESKTOP_WALLPAPER", wallpaper);
    env(L"MELEE_UCF", ucf);
    SetEnvironmentVariableW(L"MELEE_BOOT_SCENE", L"vs");
    SetEnvironmentVariableW(L"MELEE_WINDOW_TITLE", L"Desktop Melee - Windows Preview 6");
    SetEnvironmentVariableW(L"MELEE_LOG_FILE", (app.folder / L"desktop-melee.log").c_str());
    /* Keep inherited automated-run fixtures from selecting online play or
     * changing this launcher's explicit two-player local match. */
    for (const wchar_t* key : {L"MELEE_NET", L"MELEE_NET_TEST", L"MELEE_VS_SWEEP",
             L"MELEE_EXIT_AFTER_FRAMES", L"MELEE_LAN_TEST"})
        SetEnvironmentVariableW(key, nullptr);
    std::wstring command = quote(engine.wstring()) + L" --desktop " + quote(disc);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(engine.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, app.folder.c_str(), &startup, &process)) {
        const std::wstring message = L"Could not start Melee.\n\n" + error_text(GetLastError());
        MessageBoxW(app.window, message.c_str(), L"Desktop Melee", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    app.process = process.hProcess;
    app.process_id = process.dwProcessId;
    EnableWindow(child(IdStart), FALSE);
    EnableWindow(child(IdStop), TRUE);
    status(L"Match running. Use the tray icon or Ctrl+Shift+F10 for settings.");
    SetTimer(app.window, kMatchTimer, 500, nullptr);
    ShowWindow(app.window, SW_MINIMIZE);
}

void poll() {
    if (!app.process || WaitForSingleObject(app.process, 0) != WAIT_OBJECT_0) return;
    DWORD code = 0;
    GetExitCodeProcess(app.process, &code);
    CloseHandle(app.process);
    app.process = nullptr;
    app.process_id = 0;
    KillTimer(app.window, kMatchTimer);
    EnableWindow(child(IdStart), TRUE);
    EnableWindow(child(IdStop), FALSE);
    status(code == 0 ? L"Match closed. Choose your next matchup." :
        L"The game exited with an error. Check desktop-melee.log in the game folder.");
    ShowWindow(app.window, SW_RESTORE);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        app.window = hwnd;
        app.dpi = static_cast<int>(GetDpiForWindow(hwnd));
        if (!app.dpi) app.dpi = 96;
        make_form();
        check_updates();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IdBrowse: browse(); break;
        case IdStart: start(); break;
        case IdStop: stop(); break;
        case IdUpdate: update_action(); break;
        case IdMode: if (HIWORD(wparam) == CBN_SELCHANGE) update_mode(); break;
        case IdLogs: ShellExecuteW(hwnd, L"open", app.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        }
        return 0;
    case WM_TIMER:
        if (wparam == kMatchTimer) poll();
        else if (wparam == kUpdateTimer) poll_updates();
        return 0;
    case WM_DPICHANGED: {
        app.dpi = HIWORD(wparam);
        RECT* bounds = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(hwnd, nullptr, bounds->left, bounds->top,
            bounds->right - bounds->left, bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
        set_fonts(); layout(); return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(wparam), RGB(32, 42, 60));
        return reinterpret_cast<LRESULT>(app.background);
    case WM_ERASEBKGND: {
        RECT bounds{}; GetClientRect(hwnd, &bounds);
        FillRect(reinterpret_cast<HDC>(wparam), &bounds, app.background);
        return 1;
    }
    case WM_CLOSE:
        app.updates.cancel();
        stop();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        app.updates.cancel();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    std::array<wchar_t, 32768> executable{};
    DWORD length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
    if (!length || length >= executable.size()) return 1;
    app.folder = std::filesystem::path(executable.data()).parent_path();
    std::array<wchar_t, MAX_PATH> local{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
            nullptr, SHGFP_TYPE_CURRENT, local.data()))) {
        auto folder = std::filesystem::path(local.data()) / L"DesktopMelee";
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        app.config = folder / L"settings.ini";
    } else app.config = app.folder / L"settings.ini";
    app.settings_writable = prepare_unicode_config();
    app.background = CreateSolidBrush(RGB(248, 250, 253));
    WNDCLASSW cls{};
    cls.lpfnWndProc = window_proc;
    cls.hInstance = instance;
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = app.background;
    cls.lpszClassName = kClass;
    if (!RegisterClassW(&cls)) return 1;
    app.dpi = static_cast<int>(GetDpiForSystem());
    if (!app.dpi) app.dpi = 96;
    RECT bounds{0, 0, px(kBaseWidth), px(kBaseHeight)};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, app.dpi);
    HWND window = CreateWindowExW(0, kClass, L"Desktop Melee - " DESKTOP_WIDEN(DESKTOP_MELEE_VERSION), style,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (app.process) CloseHandle(app.process);
    if (app.font) DeleteObject(app.font);
    if (app.heading) DeleteObject(app.heading);
    if (app.background) DeleteObject(app.background);
    return static_cast<int>(message.wParam);
}
