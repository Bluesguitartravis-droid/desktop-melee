/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "desktop_monitor_geometry.hpp"
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>
#include <utility>

namespace aurora::desktop::monitors {
class PhysicalPixels {
    DPI_AWARENESS_CONTEXT previous;
public:
    PhysicalPixels() : previous(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PhysicalPixels() { if (previous) SetThreadDpiAwarenessContext(previous); }
    PhysicalPixels(const PhysicalPixels&) = delete;
    PhysicalPixels& operator=(const PhysicalPixels&) = delete;
};
struct Monitor {
    HMONITOR handle{};
    MONITORINFOEXW info{};
    std::wstring id, label;
};
inline BOOL CALLBACK collect(HMONITOR handle, HDC, LPRECT, LPARAM opaque) {
    auto& list = *reinterpret_cast<std::vector<Monitor>*>(opaque);
    Monitor m; m.handle=handle; m.info.cbSize=sizeof(m.info);
    if (!GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO*>(&m.info))) return TRUE;
    const auto& r=m.info.rcMonitor;
    if (r.right <= r.left || r.bottom <= r.top) return TRUE;
    m.id=m.info.szDevice;
    DISPLAY_DEVICEW device{}; device.cb=sizeof(device);
    std::wstring model;
    if (EnumDisplayDevicesW(m.info.szDevice,0,&device,EDD_GET_DEVICE_INTERFACE_NAME)) {
        // Persist the display-interface path rather than an enumeration index.
        if (*device.DeviceID) m.id=device.DeviceID;
        model=device.DeviceString;
    }
    m.label=std::wstring(m.info.szDevice)+(model.empty()?L"":L" - "+model)+L" ("+
        std::to_wstring(r.right-r.left)+L" x "+std::to_wstring(r.bottom-r.top)+L")";
    if (m.info.dwFlags & MONITORINFOF_PRIMARY) m.label+=L" [primary]";
    try { list.push_back(std::move(m)); } catch (...) { return FALSE; }
    return TRUE;
}
inline std::vector<Monitor> enumerate() {
    PhysicalPixels scope;
    std::vector<Monitor> list;
    if (!EnumDisplayMonitors(nullptr,nullptr,collect,reinterpret_cast<LPARAM>(&list))) return {};
    std::stable_sort(list.begin(),list.end(),[](const Monitor& a,const Monitor& b) {
        return std::wcscmp(a.info.szDevice,b.info.szDevice)<0;
    });
    return list;
}
inline bool same_id(const std::wstring& a,const std::wstring& b) { return _wcsicmp(a.c_str(),b.c_str())==0; }
inline bool all_requested() {
    wchar_t value[8]{};
    return GetEnvironmentVariableW(L"MELEE_DESKTOP_ALL_MONITORS",value,8)>0 &&
           std::wcscmp(value,L"1")==0;
}
inline std::wstring requested_id() {
    wchar_t value[1024]{};
    const DWORD n=GetEnvironmentVariableW(L"MELEE_DESKTOP_MONITOR",value,1024);
    if (n>=1024) return L"<invalid monitor identifier>";
    return n?value:L"";
}
struct TaskbarQuery { HMONITOR monitor{}; RECT rect{}; bool found=false; };
inline BOOL CALLBACK taskbar_window(HWND hwnd,LPARAM opaque) {
    auto& query=*reinterpret_cast<TaskbarQuery*>(opaque);
    wchar_t kind[64]{};
    if (!IsWindowVisible(hwnd) || !GetClassNameW(hwnd,kind,64)) return TRUE;
    if (std::wcscmp(kind,L"Shell_TrayWnd") && std::wcscmp(kind,L"Shell_SecondaryTrayWnd")) return TRUE;
    if (MonitorFromWindow(hwnd,MONITOR_DEFAULTTONULL)!=query.monitor) return TRUE;
    RECT r{};
    if (GetWindowRect(hwnd,&r)) { query.rect=r; query.found=true; }
    return TRUE;
}
inline monitor_geometry::Rect rect(const RECT& r) { return {r.left,r.top,r.right,r.bottom}; }
inline bool geometry(const std::wstring& id,AuroraDesktopGeometry& out) {
    PhysicalPixels scope;
    const auto list=enumerate();
    const Monitor* chosen=nullptr;
    for (const auto& m:list) {
        if ((id.empty() && (m.info.dwFlags&MONITORINFOF_PRIMARY)) || (!id.empty() && same_id(id,m.id))) {
            chosen=&m; break;
        }
    }
    // Do not silently move a requested arena to an unrelated monitor.
    if (!chosen) return false;
    TaskbarQuery query; query.monitor=chosen->handle;
    EnumWindows(taskbar_window,reinterpret_cast<LPARAM>(&query));
    const auto bar=rect(query.rect);
    return monitor_geometry::make(rect(chosen->info.rcMonitor),rect(chosen->info.rcWork),
                                  query.found?&bar:nullptr,out);
}
inline bool geometry_all(AuroraDesktopGeometry& out) {
    PhysicalPixels scope;
    const auto list=enumerate();
    if (list.empty()) return false;
    std::vector<monitor_geometry::Rect> rects;
    std::vector<int32_t> floors;
    rects.reserve(list.size()); floors.reserve(list.size());
    for (const auto& m:list) {
        TaskbarQuery query; query.monitor=m.handle;
        EnumWindows(taskbar_window,reinterpret_cast<LPARAM>(&query));
        const auto bar=rect(query.rect);
        AuroraDesktopGeometry local{};
        if (!monitor_geometry::make(rect(m.info.rcMonitor),rect(m.info.rcWork),
                                    query.found?&bar:nullptr,local)) return false;
        rects.push_back(rect(m.info.rcMonitor));
        floors.push_back(local.screen_y + local.platform_top);
    }
    return monitor_geometry::make_span(rects,floors,out);
}
inline bool requested_geometry(AuroraDesktopGeometry& out) {
    return all_requested() ? geometry_all(out) : geometry(requested_id(),out);
}
} // namespace aurora::desktop::monitors
#endif
