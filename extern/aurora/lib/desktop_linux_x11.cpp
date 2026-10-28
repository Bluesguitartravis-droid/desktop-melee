/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_linux_x11.hpp"
#include "desktop_pixels.hpp"

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/render.h>
#include <xcb/shape.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unistd.h>

namespace aurora::desktop::linux_x11 {
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t MaxDimension = 16384;
constexpr uint64_t MaxPixels = 64ull * 1024 * 1024;
constexpr size_t MaxWindows = 256;
constexpr uint32_t MaxPropertyItems = 1024;
constexpr uint16_t ShortcutMask = XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_SHIFT;
constexpr uint32_t KeyF10 = 0xffc7, KeyF11 = 0xffc8, KeyNumLock = 0xff7f;

struct Free {
  template<class T> void operator()(T* value) const { std::free(value); }
};
template<class T> using Reply = std::unique_ptr<T, Free>;
template<class T> Reply<T> own(T* value) { return Reply<T>(value); }

struct State {
  xcb_connection_t* connection = nullptr;
  xcb_screen_t* screen = nullptr;
  int screen_index = 0;
  xcb_visualid_t visual = 0;
  xcb_window_t overlay = 0;
  xcb_colormap_t colormap = 0;
  xcb_gcontext_t gc = 0;
  xcb_keycode_t settings_key = 0, quit_key = 0;
  uint16_t num_lock_mask = 0;
  bool keys_registered = false, visible = false;
  AuroraDesktopGeometry geometry{};
  std::unordered_map<std::string, xcb_atom_t> atoms;
  std::vector<uint8_t> pixels;
  Clock::time_point last_layout_check{}, last_settings_key{};
};
State state;
std::string reason = "Desktop overlay needs an X11 desktop with a compositor. "
                     "Normal game mode remains available on Wayland and in Steam Gaming Mode.";

bool fail(const char* message) {
  reason = message;
  return false;
}

bool environment_is(const char* variable, std::string_view value) {
  const char* content = std::getenv(variable);
  return content && std::string_view(content) == value;
}

bool environment_present(const char* variable) {
  const char* content = std::getenv(variable);
  return content && *content;
}

bool supported_session() {
  if (environment_present("GAMESCOPE_WAYLAND_DISPLAY") ||
      environment_is("XDG_CURRENT_DESKTOP", "gamescope") ||
      environment_is("XDG_SESSION_DESKTOP", "gamescope") ||
      environment_is("DESKTOP_SESSION", "gamescope"))
    return fail("The desktop overlay is unavailable in Steam Gaming Mode. "
                "Switch to Desktop Mode and select an X11 desktop session, "
                "or use Normal game mode here.");
  if (environment_is("XDG_SESSION_TYPE", "wayland") || environment_present("WAYLAND_DISPLAY"))
    return fail("This Linux preview needs a full X11 desktop session for its overlay and window platforms. "
                "Wayland/XWayland cannot expose all application window positions. "
                "On Steam Deck, select X11 Desktop Mode in Steam Developer settings; "
                "Normal game mode also works without changing your desktop session.");
  if (!environment_present("DISPLAY"))
    return fail("No X11 display is available. Start Desktop Melee from an X11 desktop session, "
                "or use Normal game mode.");
  return true;
}

xcb_atom_t atom(const char* name) {
  const auto found = state.atoms.find(name);
  if (found != state.atoms.end()) return found->second;
  const auto reply = own(xcb_intern_atom_reply(state.connection,
      xcb_intern_atom(state.connection, 0, static_cast<uint16_t>(std::strlen(name)), name), nullptr));
  const xcb_atom_t value = reply ? reply->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
  state.atoms.emplace(name, value);
  return value;
}

std::vector<uint32_t> property(xcb_window_t window, const char* name, xcb_atom_t type,
                               uint32_t maximum = MaxPropertyItems) {
  const auto reply = own(xcb_get_property_reply(state.connection,
      xcb_get_property(state.connection, 0, window, atom(name), type, 0, maximum), nullptr));
  if (!reply || reply->type != type || reply->format != 32 || !reply->value_len || reply->bytes_after ||
      reply->value_len > maximum) return {};
  const auto* begin = static_cast<const uint32_t*>(xcb_get_property_value(reply.get()));
  return {begin, begin + reply->value_len};
}

bool contains(const std::vector<uint32_t>& values, const char* name) {
  return std::find(values.begin(), values.end(), atom(name)) != values.end();
}

bool checked(xcb_void_cookie_t cookie) {
  return !own(xcb_request_check(state.connection, cookie));
}

bool compositor_available() {
  const std::string selection = "_NET_WM_CM_S" + std::to_string(state.screen_index);
  const auto owner = own(xcb_get_selection_owner_reply(state.connection,
      xcb_get_selection_owner(state.connection, atom(selection.c_str())), nullptr));
  return owner && owner->owner != XCB_WINDOW_NONE;
}

bool find_visual() {
  const auto formats = own(xcb_render_query_pict_formats_reply(state.connection,
      xcb_render_query_pict_formats(state.connection), nullptr));
  if (!formats) return false;
  xcb_render_pictformat_t argb = 0;
  for (auto it = xcb_render_query_pict_formats_formats_iterator(formats.get());
       it.rem; xcb_render_pictforminfo_next(&it)) {
    const auto& f = *it.data;
    const auto& d = f.direct;
    if (f.type == XCB_RENDER_PICT_TYPE_DIRECT && f.depth == 32 &&
        d.red_shift == 16 && d.red_mask == 255 &&
        d.green_shift == 8 && d.green_mask == 255 &&
        d.blue_shift == 0 && d.blue_mask == 255 &&
        d.alpha_shift == 24 && d.alpha_mask == 255) {
      argb = f.id;
      break;
    }
  }
  if (!argb) return false;
  auto screens = xcb_render_query_pict_formats_screens_iterator(formats.get());
  for (int i = 0; i < state.screen_index && screens.rem; ++i)
    xcb_render_pictscreen_next(&screens);
  if (!screens.rem) return false;
  for (auto depth = xcb_render_pictscreen_depths_iterator(screens.data);
       depth.rem; xcb_render_pictdepth_next(&depth)) {
    if (depth.data->depth != 32) continue;
    for (auto visual = xcb_render_pictdepth_visuals_iterator(depth.data);
         visual.rem; xcb_render_pictvisual_next(&visual)) {
      if (visual.data->format == argb) {
        state.visual = visual.data->visual;
        return true;
      }
    }
  }
  return false;
}

bool connect() {
  if (state.connection) return !xcb_connection_has_error(state.connection);
  if (!supported_session()) return false;
  state.connection = xcb_connect(nullptr, &state.screen_index);
  if (!state.connection || xcb_connection_has_error(state.connection)) {
    if (state.connection) xcb_disconnect(state.connection);
    state.connection = nullptr;
    return fail("Could not connect to the X11 desktop. Check that Desktop Melee is running in your desktop session.");
  }
  const auto* setup = xcb_get_setup(state.connection);
  auto screens = xcb_setup_roots_iterator(setup);
  for (int i = 0; i < state.screen_index && screens.rem; ++i) xcb_screen_next(&screens);
  if (!screens.rem || setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST) {
    shutdown();
    return fail("The X11 display does not provide a supported 32-bit desktop pixel format.");
  }
  state.screen = screens.data;
  const auto xwayland = own(xcb_query_extension_reply(state.connection,
      xcb_query_extension(state.connection, 8, "XWAYLAND"), nullptr));
  if (!xwayland || xwayland->present) {
    shutdown();
    return fail("This display is XWayland rather than a full X11 desktop. "
                "Select an X11 desktop session for the overlay, or choose Normal game mode.");
  }
  const auto shape = own(xcb_shape_query_version_reply(state.connection,
      xcb_shape_query_version(state.connection), nullptr));
  const auto randr = own(xcb_randr_query_version_reply(state.connection,
      xcb_randr_query_version(state.connection, 1, 5), nullptr));
  if (!shape || shape->major_version < 1 ||
      (shape->major_version == 1 && shape->minor_version < 1) || !find_visual() ||
      !randr || randr->major_version < 1 ||
      (randr->major_version == 1 && randr->minor_version < 5)) {
    shutdown();
    return fail("The X11 desktop needs Shape 1.1, RandR 1.5, and a 32-bit ARGB visual for a transparent click-through overlay.");
  }
  return true;
}

uint32_t current_workspace() {
  const auto desktop = property(state.screen->root, "_NET_CURRENT_DESKTOP", XCB_ATOM_CARDINAL, 1);
  return desktop.empty() ? 0 : desktop.front();
}

bool on_workspace(xcb_window_t window, uint32_t workspace) {
  const auto desktop = property(window, "_NET_WM_DESKTOP", XCB_ATOM_CARDINAL, 1);
  return desktop.empty() || desktop.front() == workspace || desktop.front() == 0xffffffffu;
}

bool bounds(xcb_window_t window, window_geometry::Rect& out) {
  const auto rect = own(xcb_get_geometry_reply(state.connection,
      xcb_get_geometry(state.connection, window), nullptr));
  const auto position = own(xcb_translate_coordinates_reply(state.connection,
      xcb_translate_coordinates(state.connection, window, state.screen->root, 0, 0), nullptr));
  if (!rect || !position || !position->same_screen || !rect->width || !rect->height) return false;
  out = {position->dst_x, position->dst_y,
         position->dst_x + rect->width, position->dst_y + rect->height};
  const auto frame = property(window, "_NET_FRAME_EXTENTS", XCB_ATOM_CARDINAL, 4);
  bool decorated = false;
  if (frame.size() == 4 && std::all_of(frame.begin(), frame.end(), [](uint32_t v) { return v <= 1024; })) {
    out.left -= static_cast<int32_t>(frame[0]);
    out.right += static_cast<int32_t>(frame[1]);
    out.top -= static_cast<int32_t>(frame[2]);
    out.bottom += static_cast<int32_t>(frame[3]);
    decorated = std::any_of(frame.begin(), frame.end(), [](uint32_t v) { return v != 0; });
  }
  /* GTK client decorations include invisible shadows inside the client area. */
  if (!decorated) {
    const auto shadow = property(window, "_GTK_FRAME_EXTENTS", XCB_ATOM_CARDINAL, 4);
    if (shadow.size() == 4 && std::all_of(shadow.begin(), shadow.end(), [](uint32_t v) { return v <= 1024; })) {
      out.left += static_cast<int32_t>(shadow[0]);
      out.right -= static_cast<int32_t>(shadow[1]);
      out.top += static_cast<int32_t>(shadow[2]);
      out.bottom -= static_cast<int32_t>(shadow[3]);
    }
  }
  return window_geometry::valid(out);
}

bool same_geometry(const AuroraDesktopGeometry& a, const AuroraDesktopGeometry& b) {
  return a.screen_x == b.screen_x && a.screen_y == b.screen_y && a.width == b.width && a.height == b.height &&
         a.platform_left == b.platform_left && a.platform_top == b.platform_top &&
         a.platform_right == b.platform_right && a.platform_bottom == b.platform_bottom;
}

bool register_keys() {
  if (state.keys_registered) return true;
  const auto* setup = xcb_get_setup(state.connection);
  const auto count = static_cast<uint8_t>(setup->max_keycode - setup->min_keycode + 1);
  const auto mapping = own(xcb_get_keyboard_mapping_reply(state.connection,
      xcb_get_keyboard_mapping(state.connection, setup->min_keycode, count), nullptr));
  const auto modifiers = own(xcb_get_modifier_mapping_reply(state.connection,
      xcb_get_modifier_mapping(state.connection), nullptr));
  if (!mapping || !mapping->keysyms_per_keycode || !modifiers) return false;
  const auto* symbols = xcb_get_keyboard_mapping_keysyms(mapping.get());
  std::vector<xcb_keycode_t> num_lock_keys;
  for (unsigned key = setup->min_keycode; key <= setup->max_keycode; ++key) {
    for (unsigned level = 0; level < mapping->keysyms_per_keycode; ++level) {
      const auto symbol = symbols[(key - setup->min_keycode) * mapping->keysyms_per_keycode + level];
      if (symbol == KeyF10 && !state.settings_key) state.settings_key = static_cast<xcb_keycode_t>(key);
      if (symbol == KeyF11 && !state.quit_key) state.quit_key = static_cast<xcb_keycode_t>(key);
      if (symbol == KeyNumLock) num_lock_keys.push_back(static_cast<xcb_keycode_t>(key));
    }
  }
  const auto* modifier_keys = xcb_get_modifier_mapping_keycodes(modifiers.get());
  for (unsigned modifier = 0; modifier < 8; ++modifier) {
    for (unsigned i = 0; i < modifiers->keycodes_per_modifier; ++i) {
      const auto key = modifier_keys[modifier * modifiers->keycodes_per_modifier + i];
      if (key && std::find(num_lock_keys.begin(), num_lock_keys.end(), key) != num_lock_keys.end())
        state.num_lock_mask |= static_cast<uint16_t>(1u << modifier);
    }
  }
  if (!state.quit_key) return false;
  std::array<uint16_t, 4> masks{ShortcutMask, static_cast<uint16_t>(ShortcutMask | XCB_MOD_MASK_LOCK),
      static_cast<uint16_t>(ShortcutMask | state.num_lock_mask),
      static_cast<uint16_t>(ShortcutMask | XCB_MOD_MASK_LOCK | state.num_lock_mask)};
  std::sort(masks.begin(), masks.end());
  const auto end = std::unique(masks.begin(), masks.end());
  for (auto it = masks.begin(); it != end; ++it) {
    if (!checked(xcb_grab_key_checked(state.connection, 0, state.screen->root, *it, state.quit_key,
                                     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC))) {
      xcb_ungrab_key(state.connection, state.quit_key, state.screen->root, XCB_MOD_MASK_ANY);
      return false;
    }
  }
  if (state.settings_key) {
    for (auto it = masks.begin(); it != end; ++it) {
      if (!checked(xcb_grab_key_checked(state.connection, 0, state.screen->root, *it, state.settings_key,
                                       XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC))) {
        xcb_ungrab_key(state.connection, state.settings_key, state.screen->root, XCB_MOD_MASK_ANY);
        state.settings_key = 0;
        break;
      }
    }
  }
  state.keys_registered = true;
  return true;
}
} // namespace

const char* unavailable_reason() noexcept { return reason.c_str(); }
bool controls_active() noexcept { return state.keys_registered; }

bool query_geometry(AuroraDesktopGeometry& out) {
  if (!connect()) return false;
  if (!compositor_available())
    return fail("X11 desktop compositing is disabled or unavailable. Enable the desktop compositor "
                "(KDE: System Settings > Display and Monitor > Compositor), or choose Normal game mode.");
  const auto monitors = own(xcb_randr_get_monitors_reply(state.connection,
      xcb_randr_get_monitors(state.connection, state.screen->root, 1), nullptr));
  if (!monitors || !monitors->nMonitors)
    return fail("Could not determine the primary X11 monitor. Check the desktop display configuration.");
  const xcb_randr_monitor_info_t* selected = nullptr;
  for (auto it = xcb_randr_get_monitors_monitors_iterator(monitors.get());
       it.rem; xcb_randr_monitor_info_next(&it)) {
    if (!it.data->width || !it.data->height) continue;
    if (!selected || it.data->primary) selected = it.data;
    if (it.data->primary) break;
  }
  if (!selected || selected->width > MaxDimension || selected->height > MaxDimension ||
      static_cast<uint64_t>(selected->width) * selected->height > MaxPixels)
    return fail("The primary X11 monitor dimensions exceed the desktop overlay limit.");
  out = {selected->x, selected->y, selected->width, selected->height, 0,
         std::max(0, static_cast<int>(selected->height) - 48), selected->width, selected->height};
  const uint32_t desktop = current_workspace();
  /* Prefer the actual visible bottom panel, including KDE's floating panel.
   * _NET_WORKAREA is often the bounding box of multiple monitors, so it is
   * only a fallback. No desktop/taskbar settings are changed. */
  const auto work = property(state.screen->root, "_NET_WORKAREA", XCB_ATOM_CARDINAL);
  if (desktop < work.size() / 4) {
    const uint64_t bottom = static_cast<uint64_t>(work[desktop * 4 + 1]) + work[desktop * 4 + 3];
    const int64_t local = static_cast<int64_t>(bottom) - out.screen_y;
    if (local > static_cast<int64_t>(out.height) * 3 / 5 && local < static_cast<int64_t>(out.height) - 2)
      out.platform_top = static_cast<int32_t>(local);
  }
  uint32_t best_width = 0;
  for (const auto window : property(state.screen->root, "_NET_CLIENT_LIST_STACKING", XCB_ATOM_WINDOW)) {
    if (!contains(property(window, "_NET_WM_WINDOW_TYPE", XCB_ATOM_ATOM, 16), "_NET_WM_WINDOW_TYPE_DOCK") ||
        !on_workspace(window, desktop)) continue;
    const auto attrs = own(xcb_get_window_attributes_reply(state.connection,
        xcb_get_window_attributes(state.connection, window), nullptr));
    window_geometry::Rect rect{};
    if (!attrs || attrs->map_state != XCB_MAP_STATE_VIEWABLE || !bounds(window, rect)) continue;
    rect.left -= out.screen_x; rect.right -= out.screen_x;
    rect.top -= out.screen_y; rect.bottom -= out.screen_y;
    rect.left = std::clamp(rect.left, 0, static_cast<int>(out.width));
    rect.right = std::clamp(rect.right, 0, static_cast<int>(out.width));
    if (rect.right <= rect.left || rect.top < static_cast<int>(out.height) * 3 / 5 ||
        rect.top >= static_cast<int>(out.height) - 2 ||
        rect.bottom < static_cast<int>(out.height) - 96 || rect.bottom <= rect.top ||
        rect.bottom - rect.top > static_cast<int>(out.height) / 4) continue;
    const auto width = static_cast<uint32_t>(rect.right - rect.left);
    if (width < out.width / 4 || width <= best_width) continue;
    best_width = width;
    out.platform_left = rect.left;
    out.platform_right = rect.right;
    out.platform_top = rect.top;
    out.platform_bottom = std::min(rect.bottom, static_cast<int>(out.height));
  }
  return true;
}

std::vector<window_geometry::Window> enumerate_windows(const AuroraDesktopGeometry& desktop) {
  std::vector<window_geometry::Window> result;
  if (!connect()) return result;
  const uint32_t workspace = current_workspace();
  const auto clients = property(state.screen->root, "_NET_CLIENT_LIST_STACKING", XCB_ATOM_WINDOW);
  result.reserve(std::min(clients.size(), MaxWindows));
  /* EWMH publishes bottom-to-top; the clipping algorithm consumes front-to-back. */
  for (auto it = clients.rbegin(); it != clients.rend() && result.size() < MaxWindows; ++it) {
    const auto window = *it;
    if (!window || window == state.overlay || !on_workspace(window, workspace)) continue;
    const auto pid = property(window, "_NET_WM_PID", XCB_ATOM_CARDINAL, 1);
    if (!pid.empty() && pid.front() == static_cast<uint32_t>(getpid())) continue;
    const auto attrs = own(xcb_get_window_attributes_reply(state.connection,
        xcb_get_window_attributes(state.connection, window), nullptr));
    if (!attrs || attrs->override_redirect || attrs->map_state != XCB_MAP_STATE_VIEWABLE) continue;
    const auto type = property(window, "_NET_WM_WINDOW_TYPE", XCB_ATOM_ATOM, 16);
    bool excluded = false;
    for (const auto* name : {"_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_DOCK",
         "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_MENU", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
         "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
         "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_DND", "_NET_WM_WINDOW_TYPE_UTILITY"})
      excluded |= contains(type, name);
    if (excluded || contains(property(window, "_NET_WM_STATE", XCB_ATOM_ATOM, 64), "_NET_WM_STATE_HIDDEN")) continue;
    window_geometry::Rect rect{};
    if (!bounds(window, rect)) continue;
    rect.left -= desktop.screen_x; rect.right -= desktop.screen_x;
    rect.top -= desktop.screen_y; rect.bottom -= desktop.screen_y;
    if (rect.right <= 0 || rect.bottom <= 0 || rect.left >= static_cast<int32_t>(desktop.width) ||
        rect.top >= static_cast<int32_t>(desktop.height)) continue;
    result.push_back({window, rect});
  }
  return result;
}

bool create_overlay(const AuroraDesktopGeometry& geometry) {
  if (!connect()) return false;
  if (state.overlay) return same_geometry(geometry, state.geometry);
  if (!register_keys())
    return fail("Could not reserve Ctrl+Shift+F11 to close the desktop overlay. "
                "Free that desktop shortcut and try again, or choose Normal game mode.");
  state.geometry = geometry;
  state.colormap = xcb_generate_id(state.connection);
  if (!checked(xcb_create_colormap_checked(state.connection, XCB_COLORMAP_ALLOC_NONE,
      state.colormap, state.screen->root, state.visual))) {
    state.colormap = 0;
    return fail("Could not create the transparent X11 overlay colormap.");
  }
  state.overlay = xcb_generate_id(state.connection);
  const uint32_t values[]{0, 0, 1, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY, state.colormap};
  if (!checked(xcb_create_window_checked(state.connection, 32, state.overlay, state.screen->root,
      static_cast<int16_t>(geometry.screen_x), static_cast<int16_t>(geometry.screen_y),
      static_cast<uint16_t>(geometry.width), static_cast<uint16_t>(geometry.height), 0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, state.visual,
      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
      values))) {
    state.overlay = 0;
    destroy_overlay();
    return fail("Could not create the transparent X11 overlay window.");
  }
  state.gc = xcb_generate_id(state.connection);
  if (!checked(xcb_create_gc_checked(state.connection, state.gc, state.overlay, 0, nullptr)) ||
      !checked(xcb_shape_rectangles_checked(state.connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
          XCB_CLIP_ORDERING_UNSORTED, state.overlay, 0, 0, 0, nullptr))) {
    destroy_overlay();
    return fail("Could not establish click-through input on the X11 overlay. Normal window retained.");
  }
  const uint32_t states[]{atom("_NET_WM_STATE_ABOVE"), atom("_NET_WM_STATE_STICKY"),
      atom("_NET_WM_STATE_SKIP_TASKBAR"), atom("_NET_WM_STATE_SKIP_PAGER")};
  xcb_change_property(state.connection, XCB_PROP_MODE_REPLACE, state.overlay,
      atom("_NET_WM_STATE"), XCB_ATOM_ATOM, 32, std::size(states), states);
  const uint32_t type = atom("_NET_WM_WINDOW_TYPE_NOTIFICATION");
  xcb_change_property(state.connection, XCB_PROP_MODE_REPLACE, state.overlay,
      atom("_NET_WM_WINDOW_TYPE"), XCB_ATOM_ATOM, 32, 1, &type);
  const uint32_t desktop = 0xffffffffu, bypass = 2, pid = static_cast<uint32_t>(getpid());
  for (const auto& [name, value] : {std::pair{"_NET_WM_DESKTOP", desktop},
      std::pair{"_NET_WM_BYPASS_COMPOSITOR", bypass}, std::pair{"_NET_WM_PID", pid}})
    xcb_change_property(state.connection, XCB_PROP_MODE_REPLACE, state.overlay,
        atom(name), XCB_ATOM_CARDINAL, 32, 1, &value);
  const char title[] = "Desktop Melee overlay";
  xcb_change_property(state.connection, XCB_PROP_MODE_REPLACE, state.overlay,
      atom("_NET_WM_NAME"), atom("UTF8_STRING"), 8, sizeof(title) - 1, title);
  const char wmclass[] = "DesktopMelee\0DesktopMelee";
  xcb_change_property(state.connection, XCB_PROP_MODE_REPLACE, state.overlay,
      XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wmclass), wmclass);
  const uint32_t hints[]{1, 0, 0, 0, 0, 0, 0, 0, 0}; // InputHint: never acquire keyboard focus.
  xcb_change_property(state.connection, XCB_PROP_MODE_REPLACE, state.overlay,
      XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 32, std::size(hints), hints);
  state.pixels.resize(static_cast<size_t>(geometry.width) * geometry.height * 4);
  state.visible = false;
  state.last_layout_check = Clock::now();
  return xcb_flush(state.connection) > 0;
}

void destroy_overlay() {
  if (!state.connection) return;
  if (state.gc) xcb_free_gc(state.connection, state.gc);
  if (state.overlay) xcb_destroy_window(state.connection, state.overlay);
  if (state.colormap) xcb_free_colormap(state.connection, state.colormap);
  state.gc = state.overlay = state.colormap = 0;
  state.visible = false;
  state.pixels.clear();
  xcb_flush(state.connection);
}

bool present(std::span<const uint8_t> pixels, uint32_t width, uint32_t height) {
  if (!state.overlay || !width || !height ||
      static_cast<uint64_t>(width) * height * 4 != pixels.size()) return false;
  const auto& g = state.geometry;
  pixels::scale_bgra(state.pixels.data(), g.width, g.height, pixels.data(), width, height);
  /* PutImage requests are bounded by the server's advertised request length.
   * At most one frame is queued by the UI; GPU readback remains nonblocking. */
  const uint64_t maximum = static_cast<uint64_t>(xcb_get_maximum_request_length(state.connection)) * 4;
  const uint64_t row_bytes = static_cast<uint64_t>(g.width) * 4;
  if (maximum <= sizeof(xcb_put_image_request_t) + 4 + row_bytes) return false;
  const uint32_t rows = static_cast<uint32_t>(std::min<uint64_t>(g.height,
      (maximum - sizeof(xcb_put_image_request_t) - 4) / row_bytes));
  if (!state.visible) {
    /* Mapping clears an X window to its background. Map the already
     * input-empty, transparent window before uploading the first frame. */
    xcb_map_window(state.connection, state.overlay);
    state.visible = true;
  }
  for (uint32_t y = 0; y < g.height; y += rows) {
    const uint32_t count = std::min(rows, g.height - y);
    xcb_put_image(state.connection, XCB_IMAGE_FORMAT_Z_PIXMAP, state.overlay, state.gc,
        static_cast<uint16_t>(g.width), static_cast<uint16_t>(count), 0, static_cast<int16_t>(y), 0, 32,
        static_cast<uint32_t>(row_bytes * count), state.pixels.data() + row_bytes * y);
  }
  return xcb_flush(state.connection) > 0 && !xcb_connection_has_error(state.connection);
}

uint32_t poll() {
  if (!state.connection) return 0;
  if (xcb_connection_has_error(state.connection)) {
    fail("The X11 display connection was lost.");
    return Failed;
  }
  uint32_t result = 0;
  while (const auto event = own(xcb_poll_for_event(state.connection))) {
    const auto kind = event->response_type & 0x7f;
    if (kind == 0) {
      /* Unchecked requests are only used for our own live overlay. Query
       * errors from disappearing application windows are consumed in replies. */
      fail("The X11 server rejected an overlay presentation request.");
      result |= Failed;
    } else if (kind == XCB_KEY_PRESS) {
      const auto* key = reinterpret_cast<const xcb_key_press_event_t*>(event.get());
      const auto modifiers = (key->state & 0xffu) & ~(XCB_MOD_MASK_LOCK | state.num_lock_mask);
      if (modifiers != ShortcutMask) continue;
      if (key->detail == state.quit_key) result |= Quit;
      else if (key->detail == state.settings_key &&
               Clock::now() - state.last_settings_key >= std::chrono::milliseconds(350)) {
        state.last_settings_key = Clock::now();
        result |= Settings;
      }
    } else if (kind == XCB_DESTROY_NOTIFY) {
      const auto* destroyed = reinterpret_cast<const xcb_destroy_notify_event_t*>(event.get());
      if (destroyed->window == state.overlay) result |= Failed;
    }
  }
  const auto now = Clock::now();
  if (state.overlay && now - state.last_layout_check >= std::chrono::milliseconds(500)) {
    state.last_layout_check = now;
    AuroraDesktopGeometry current{};
    if (!query_geometry(current)) result |= Failed;
    else if (!same_geometry(current, state.geometry)) {
      fail("The display or panel layout changed. Relaunch Desktop Melee to align the stage with the new layout.");
      result |= LayoutChanged;
    }
    const uint32_t above = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(state.connection, state.overlay, XCB_CONFIG_WINDOW_STACK_MODE, &above);
    xcb_flush(state.connection);
  }
  return result;
}

void shutdown() {
  if (!state.connection) return;
  destroy_overlay();
  /* Disconnect also releases our passive key grabs, including partial grabs
   * from a failed initialization. No desktop configuration is persisted. */
  xcb_disconnect(state.connection);
  state = {};
}
} // namespace aurora::desktop::linux_x11
