/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AURORA_DESKTOP_H
#define AURORA_DESKTOP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Physical pixels. The platform rectangle is relative to the primary
 * monitor; screen_x/screen_y are its origin in the virtual desktop. */
typedef struct AuroraDesktopGeometry {
  int32_t screen_x, screen_y;
  uint32_t width, height;
  int32_t platform_left, platform_top, platform_right, platform_bottom;
} AuroraDesktopGeometry;

/* UI thread only. Geometry is available before enabling the overlay and is
 * held for the process lifetime so stage collision and presentation agree.
 * A changed display/taskbar layout requires relaunch. Unsupported platforms
 * return false and leave normal windowed rendering available. */
bool aurora_desktop_get_geometry(AuroraDesktopGeometry* geometry);
/* Call from the SDL/UI thread BETWEEN frames. Enabling hides the normal game
 * window; disabling restores it. False means the normal window is retained. */
bool aurora_desktop_enable(bool enabled);
bool aurora_desktop_active(void);
/* UI-thread-only host controls. Linux keeps its X11 shortcuts available while
 * the normal settings window is shown. The message is stable until another
 * desktop operation; it is intended for an initialization error dialog. */
bool aurora_desktop_take_settings_request(void);
const char* aurora_desktop_unavailable_reason(void);

#define AURORA_DESKTOP_MAX_WINDOW_EDGES 32

/* A visible horizontal portion of an application's top edge. All coordinates
 * are physical pixels relative to the primary monitor. The full window
 * rectangle is not clipped, so movement can be distinguished from resizing
 * or occlusion. window_id is opaque and stable during the window's lifetime;
 * span_index is its current left-to-right fragment index, not a stable ID. */
typedef struct AuroraDesktopWindowEdge {
  uint64_t window_id;
  uint32_t span_index;
  int32_t left, right, top;
  int32_t window_left, window_top, window_right, window_bottom;
} AuroraDesktopWindowEdge;

/* UI thread only. Returns the number of edges written, at most capacity and
 * AURORA_DESKTOP_MAX_WINDOW_EDGES. The snapshot refreshes at about 15 Hz and
 * is ordered by window_id then span_index. Hidden/minimized/cloaked windows,
 * this process, shell surfaces, tool windows and covered top-edge portions
 * are omitted. No titles or window contents are read. Available before the
 * overlay opens so collision islands can be initialized with the stage.
 * Returns zero on unsupported platforms, if MELEE_DESKTOP is not "1", or
 * if MELEE_DESKTOP_WINDOWS is explicitly "0". */
uint32_t aurora_desktop_get_window_edges(AuroraDesktopWindowEdge* edges, uint32_t capacity);

#define AURORA_DESKTOP_MAX_WALLPAPER_EDGES 32

/* Experimental floor segments inferred from the selected wallpaper image. Physical
 * primary-monitor pixels. A nonzero generation identifies one image/layout;
 * edge_id is stable only within that generation. Occlusion by app windows
 * can produce multiple fragments sharing generation/edge_id; match them by
 * horizontal overlap, never fragment ordinal. A new generation must be
 * treated as new collision rather than as motion of an existing platform.
 * top/right_top are the y coordinates at left/right. A nonzero contour_id
 * connects exactly touching segments from one continuous terrain outline;
 * zero denotes an independent ledge. source_* retains the immutable original
 * segment when app windows clip it, so clipping never becomes platform motion. */
typedef struct AuroraDesktopWallpaperEdge {
  uint64_t generation;
  uint32_t edge_id;
  int32_t left, right, top;
  int32_t right_top;
  uint32_t contour_id;
  int32_t source_left, source_right, source_top, source_right_top;
} AuroraDesktopWallpaperEdge;

/* UI thread only. Nonblocking snapshot of background image analysis. Enabled
 * only when MELEE_DESKTOP and MELEE_DESKTOP_WALLPAPER are both "1". At most
 * capacity / AURORA_DESKTOP_MAX_WALLPAPER_EDGES entries are written. The
 * selected local wallpaper file is read; screen contents are never captured.
 * Source/layout changes refresh asynchronously approximately every 2 seconds.
 * Invalid/unavailable sources and multi-monitor Tile/Span return zero edges.
 * Occlusion updates independently of the window-platform option. */
uint32_t aurora_desktop_get_wallpaper_edges(AuroraDesktopWallpaperEdge* edges, uint32_t capacity);

#ifdef __cplusplus
}
#endif
#endif
