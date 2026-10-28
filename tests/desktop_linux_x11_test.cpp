/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_linux_x11.hpp"
#include "desktop_window_geometry.hpp"

#include <xcb/xcb.h>
#include <xcb/shape.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace desktop = aurora::desktop::linux_x11;
namespace geometry = aurora::desktop::window_geometry;
namespace {
struct Free { template<class T> void operator()(T* p) const { std::free(p); } };
template<class T> auto own(T* p) { return std::unique_ptr<T, Free>(p); }
unsigned checks = 0;
void check(bool condition, const char* message) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s (backend: %s)\n", message, desktop::unavailable_reason());
    std::exit(1);
  }
}

xcb_connection_t* fixture;
xcb_screen_t* screen;
xcb_atom_t atom(const char* name) {
  const auto r = own(xcb_intern_atom_reply(fixture,
      xcb_intern_atom(fixture, 0, static_cast<uint16_t>(std::strlen(name)), name), nullptr));
  check(r != nullptr, "fixture atom lookup");
  return r->atom;
}
void sync_fixture() { check(own(xcb_get_input_focus_reply(fixture, xcb_get_input_focus(fixture), nullptr)) != nullptr,
                    "fixture connection synchronized"); }
void property(xcb_window_t window, const char* name, xcb_atom_t type, std::vector<uint32_t> values) {
  xcb_change_property(fixture, XCB_PROP_MODE_REPLACE, window, atom(name), type, 32,
                      static_cast<uint32_t>(values.size()), values.data());
}
xcb_window_t window(int x, int y, uint16_t width, uint16_t height, const char* type = "_NET_WM_WINDOW_TYPE_NORMAL") {
  const auto id = xcb_generate_id(fixture);
  const uint32_t values[]{0x606060, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY};
  check(!own(xcb_request_check(fixture, xcb_create_window_checked(fixture, screen->root_depth, id,
      screen->root, static_cast<int16_t>(x), static_cast<int16_t>(y), width, height, 0,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values))),
      "fixture window created");
  property(id, "_NET_WM_WINDOW_TYPE", XCB_ATOM_ATOM, {atom(type)});
  property(id, "_NET_WM_DESKTOP", XCB_ATOM_CARDINAL, {0});
  xcb_map_window(fixture, id);
  return id;
}
std::vector<uint8_t> bytes(xcb_window_t window, const char* name) {
  const auto r = own(xcb_get_property_reply(fixture,
      xcb_get_property(fixture, 0, window, atom(name), XCB_GET_PROPERTY_TYPE_ANY, 0, 256), nullptr));
  if (!r || r->format != 8) return {};
  const auto* p = static_cast<const uint8_t*>(xcb_get_property_value(r.get()));
  return {p, p + r->value_len};
}
xcb_window_t overlay() {
  const auto tree = own(xcb_query_tree_reply(fixture, xcb_query_tree(fixture, screen->root), nullptr));
  if (!tree) return 0;
  const auto* children = xcb_query_tree_children(tree.get());
  for (unsigned i = 0; i < tree->children_len; ++i) {
    const auto title = bytes(children[i], "_NET_WM_NAME");
    if (std::string(title.begin(), title.end()) == "Desktop Melee overlay") return children[i];
  }
  return 0;
}
xcb_keycode_t key(uint32_t symbol) {
  const auto* setup = xcb_get_setup(fixture);
  const auto map = own(xcb_get_keyboard_mapping_reply(fixture,
      xcb_get_keyboard_mapping(fixture, setup->min_keycode,
          static_cast<uint8_t>(setup->max_keycode - setup->min_keycode + 1)), nullptr));
  check(map && map->keysyms_per_keycode, "fixture keyboard map");
  const auto* symbols = xcb_get_keyboard_mapping_keysyms(map.get());
  for (unsigned code = setup->min_keycode; code <= setup->max_keycode; ++code)
    for (unsigned level = 0; level < map->keysyms_per_keycode; ++level)
      if (symbols[(code - setup->min_keycode) * map->keysyms_per_keycode + level] == symbol)
        return static_cast<xcb_keycode_t>(code);
  return 0;
}
bool grab(xcb_keycode_t key) {
  return !own(xcb_request_check(fixture, xcb_grab_key_checked(fixture, 0, screen->root,
      XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_SHIFT, key, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC)));
}
const geometry::Window* find(const std::vector<geometry::Window>& windows, xcb_window_t id) {
  const auto found = std::find_if(windows.begin(), windows.end(), [id](const auto& w) { return w.id == id; });
  return found == windows.end() ? nullptr : &*found;
}
} // namespace

int main(int argc, char** argv) {
  AuroraDesktopGeometry desktop_geometry{};
  if (argc == 2 && std::strcmp(argv[1], "--without-compositor") == 0) {
    check(!desktop::query_geometry(desktop_geometry), "overlay rejects a desktop without a compositor");
    check(std::strstr(desktop::unavailable_reason(), "compositing") != nullptr,
          "missing compositor has an actionable explanation");
    desktop::shutdown();
    std::puts("Linux X11: missing-compositor refusal passed");
    return 0;
  }
  setenv("XDG_SESSION_TYPE", "wayland", 1);
  check(!desktop::query_geometry(desktop_geometry), "Wayland is explicitly rejected");
  check(std::strstr(desktop::unavailable_reason(), "Wayland") != nullptr, "Wayland refusal explains platform limitation");
  setenv("XDG_SESSION_TYPE", "x11", 1);
  setenv("GAMESCOPE_WAYLAND_DISPLAY", "gamescope-test", 1);
  check(!desktop::query_geometry(desktop_geometry), "Gaming Mode is explicitly rejected");
  unsetenv("GAMESCOPE_WAYLAND_DISPLAY");

  fixture = xcb_connect(nullptr, nullptr);
  check(fixture && !xcb_connection_has_error(fixture), "fixture connected to Xvfb");
  screen = xcb_setup_roots_iterator(xcb_get_setup(fixture)).data;
  check(screen && screen->width_in_pixels == 1280 && screen->height_in_pixels == 800,
        "fixture display is the Steam Deck logical resolution");
  const auto back = window(100, 160, 500, 260);
  const auto front = window(250, 100, 200, 200);
  const auto hidden = window(10, 60, 100, 100);
  const auto other_desktop = window(20, 70, 100, 100);
  const auto tool = window(30, 80, 100, 100, "_NET_WM_WINDOW_TYPE_TOOLTIP");
  const auto ours = window(40, 90, 100, 100);
  const auto stale = window(50, 100, 100, 100);
  const auto panel = window(0, 752, 1280, 48, "_NET_WM_WINDOW_TYPE_DOCK");
  property(hidden, "_NET_WM_STATE", XCB_ATOM_ATOM, {atom("_NET_WM_STATE_HIDDEN")});
  property(other_desktop, "_NET_WM_DESKTOP", XCB_ATOM_CARDINAL, {1});
  property(ours, "_NET_WM_PID", XCB_ATOM_CARDINAL, {static_cast<uint32_t>(getpid())});
  property(screen->root, "_NET_CURRENT_DESKTOP", XCB_ATOM_CARDINAL, {0});
  property(screen->root, "_NET_WORKAREA", XCB_ATOM_CARDINAL, {0, 0, 1280, 752});
  property(screen->root, "_NET_CLIENT_LIST_STACKING", XCB_ATOM_WINDOW,
      {back, front, hidden, other_desktop, tool, ours, stale, panel});
  xcb_destroy_window(fixture, stale); // Disappears after its EWMH snapshot was published.
  sync_fixture();
  check(desktop::query_geometry(desktop_geometry), "native primary display and panel discovered");
  check(desktop_geometry.width == 1280 && desktop_geometry.height == 800 &&
        desktop_geometry.platform_top == 752 && desktop_geometry.platform_right == 1280,
        "panel is the floor in actual desktop coordinates");
  auto windows = desktop::enumerate_windows(desktop_geometry);
  check(windows.size() == 2 && windows[0].id == front && windows[1].id == back,
        "window discovery excludes hidden, foreign-workspace, shell, tool, own and stale windows");
  const auto edges = geometry::visible_edges(windows, 1280, 800, 752);
  check(edges.size() == 3, "overlapping top edge splits into visible platforms");
  check(std::count_if(edges.begin(), edges.end(), [back](const auto& e) {
          return e.window_id == back && e.top == 160 &&
                 ((e.left == 100 && e.right == 250) || (e.left == 450 && e.right == 600));
        }) == 2, "covered top edge is omitted precisely");
  const uint32_t move[]{300, 200};
  xcb_configure_window(fixture, front, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, move);
  property(front, "_NET_FRAME_EXTENTS", XCB_ATOM_CARDINAL, {5, 5, 24, 5});
  sync_fixture();
  windows = desktop::enumerate_windows(desktop_geometry);
  const auto* moved = find(windows, front);
  check(moved && moved->bounds.left == 295 && moved->bounds.top == 176 && moved->bounds.right == 505,
        "moving window geometry includes its visible title bar and frame");
  property(front, "_NET_FRAME_EXTENTS", XCB_ATOM_CARDINAL, {0, 0, 0, 0});
  property(front, "_GTK_FRAME_EXTENTS", XCB_ATOM_CARDINAL, {10, 10, 8, 8});
  sync_fixture();
  windows = desktop::enumerate_windows(desktop_geometry);
  moved = find(windows, front);
  check(moved && moved->bounds.left == 310 && moved->bounds.top == 208 && moved->bounds.right == 490,
        "GTK invisible client shadows do not become platforms");

  const auto quit_key = key(0xffc8);
  check(quit_key && grab(quit_key), "fixture reserves emergency shortcut for conflict test");
  check(!desktop::create_overlay(desktop_geometry), "overlay refuses to start without an emergency exit shortcut");
  check(!overlay(), "shortcut conflict never creates a blocking overlay");
  xcb_ungrab_key(fixture, quit_key, screen->root, XCB_MOD_MASK_ANY);
  sync_fixture();
  xcb_set_input_focus(fixture, XCB_INPUT_FOCUS_PARENT, back, XCB_CURRENT_TIME);
  sync_fixture();
  check(desktop::create_overlay(desktop_geometry), "ARGB click-through overlay created");
  const std::vector<uint8_t> pixels{10, 20, 30, 128, 0, 0, 0, 0, 255, 0, 0, 255, 0, 120, 0, 120};
  check(desktop::present(pixels, 2, 2), "premultiplied image presented and scaled");
  const auto overlay_window = overlay();
  check(overlay_window != 0, "actual overlay is an X11 window");
  const auto region = own(xcb_shape_get_rectangles_reply(fixture,
      xcb_shape_get_rectangles(fixture, overlay_window, XCB_SHAPE_SK_INPUT), nullptr));
  check(region && region->rectangles_len == 0, "overlay's actual X input region is empty");
  const auto image = own(xcb_get_image_reply(fixture,
      xcb_get_image(fixture, XCB_IMAGE_FORMAT_Z_PIXMAP, overlay_window, 1, 1, 1, 1, ~0u), nullptr));
  check(image && image->depth == 32 && xcb_get_image_data_length(image.get()) == 4,
        "overlay stores a 32-bit ARGB image");
  check(std::memcmp(xcb_get_image_data(image.get()), pixels.data(), 4) == 0,
        "ARGB alpha and premultiplied colors reach the X server unchanged");
  const auto focus = own(xcb_get_input_focus_reply(fixture, xcb_get_input_focus(fixture), nullptr));
  check(focus && focus->focus == back, "showing overlay does not steal keyboard focus");
  xcb_warp_pointer(fixture, XCB_NONE, screen->root, 0, 0, 0, 0, 150, 200);
  const auto pointer = own(xcb_query_pointer_reply(fixture, xcb_query_pointer(fixture, screen->root), nullptr));
  check(pointer && pointer->child == back, "pointer targets underlying app through the visible overlay");
  check(!grab(quit_key), "emergency shortcut is reserved while overlay runs");
  check((desktop::poll() & desktop::Failed) == 0, "disappearing app windows and presentation produce no X errors");

  const uint32_t panel_move[]{744};
  xcb_configure_window(fixture, panel, XCB_CONFIG_WINDOW_Y, panel_move);
  sync_fixture();
  std::this_thread::sleep_for(std::chrono::milliseconds(550));
  check((desktop::poll() & desktop::LayoutChanged) != 0,
        "panel layout change requests restart rather than desynchronizing floor collision");
  desktop::destroy_overlay();
  check(!overlay(), "pausing destroys the overlay window");
  check(!grab(quit_key), "emergency shortcut remains usable while settings are open");
  desktop::shutdown();
  check(grab(quit_key), "shutting down releases global shortcuts");
  xcb_disconnect(fixture);
  std::printf("Linux X11 integration: %u checks passed (Xvfb + xcompmgr, no disc/GPU required)\n", checks);
  return 0;
}
