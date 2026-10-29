/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "desktop.h"
#include <algorithm>
#include <cstdint>
#include <span>

namespace aurora::desktop::monitor_geometry {
struct Rect { int32_t left, top, right, bottom; };
inline bool valid(const Rect& r) { return r.right > r.left && r.bottom > r.top; }
inline bool intersects(const Rect& a, const Rect& b) {
    return std::max(a.left,b.left) < std::min(a.right,b.right) &&
           std::max(a.top,b.top) < std::min(a.bottom,b.bottom);
}
inline bool make(const Rect& monitor, const Rect& work, const Rect* taskbar,
                 AuroraDesktopGeometry& out) {
    const int64_t w = int64_t(monitor.right)-monitor.left;
    const int64_t h = int64_t(monitor.bottom)-monitor.top;
    if (w < 1 || h < 4 || w > 16384 || h > 16384) return false;
    auto x = [&](int32_t v) { return int32_t(std::clamp<int64_t>(int64_t(v)-monitor.left,0,w)); };
    auto y = [&](int32_t v) { return int32_t(std::clamp<int64_t>(int64_t(v)-monitor.top,0,h)); };
    const int32_t fallback = int32_t(std::max<int64_t>(1,h-48));
    int32_t top = valid(work) && intersects(monitor,work) ? y(work.bottom) : int32_t(h);
    if (top <= 0 || top >= h-2) top = fallback;
    out = {monitor.left, monitor.top, uint32_t(w), uint32_t(h), 0, top, int32_t(w), int32_t(h)};
    // A taskbar on ANOTHER screen must never become this screen's floor.
    if (taskbar && valid(*taskbar) && intersects(monitor,*taskbar) &&
        taskbar->bottom >= monitor.bottom-2 && y(taskbar->top) >= h*3/4 &&
        y(taskbar->top) < h-2) {
        out.platform_left = x(taskbar->left);
        out.platform_right = x(taskbar->right);
        out.platform_top = y(taskbar->top);
        out.platform_bottom = y(taskbar->bottom);
    }
    return out.platform_top > 0 && out.platform_right > out.platform_left;
}
inline bool make_span(std::span<const Rect> monitors, std::span<const int32_t> floor_tops,
                      AuroraDesktopGeometry& out) {
    if (monitors.empty() || monitors.size() != floor_tops.size()) return false;
    int32_t left=monitors[0].left, top=monitors[0].top;
    int32_t right=monitors[0].right, bottom=monitors[0].bottom;
    size_t main_index=0;
    int32_t floor=floor_tops[0];
    for (size_t i=0;i<monitors.size();++i) {
        if (!valid(monitors[i])) return false;
        left=std::min(left,monitors[i].left); top=std::min(top,monitors[i].top);
        right=std::max(right,monitors[i].right); bottom=std::max(bottom,monitors[i].bottom);
        if (floor_tops[i] > floor) { floor=floor_tops[i]; main_index=i; }
    }
    const int64_t w=int64_t(right)-left, h=int64_t(bottom)-top;
    if (w < 1 || h < 4 || w > 16384 || h > 16384) return false;
    floor=std::clamp(floor,top+1,bottom-2);
    const Rect& main=monitors[main_index];
    out={left,top,uint32_t(w),uint32_t(h),
         main.left-left,floor-top,main.right-left,main.bottom-top};
    return out.platform_top > 0 && out.platform_right > out.platform_left &&
           uint32_t(out.platform_top) < out.height;
}
} // namespace aurora::desktop::monitor_geometry
