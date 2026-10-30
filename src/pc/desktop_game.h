/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct StartMeleeData;
struct MapCollData;
struct HSD_CObj;
struct CameraTransformState;

/* The requested mode survives scene changes; active is true only after a
 * desktop match has loaded. The host uses active to manage its native window. */
bool pc_desktop_requested(void);
bool pc_desktop_match_active(void);
int pc_desktop_player_count(void);
float pc_desktop_character_scale(void);
void pc_desktop_configure_match(struct StartMeleeData* start);
void pc_desktop_begin_match(struct StartMeleeData* start);
void pc_desktop_match_loaded(void);
void pc_desktop_end_match(void);

/* Game-side integration. Geometry is sampled once per match, so an OS taskbar
 * resize cannot move collision under a fighter halfway through a frame. */
struct MapCollData* pc_desktop_prepare_collision(struct MapCollData* source);
void pc_desktop_remap_collision(struct MapCollData* data);
void pc_desktop_collision_loaded(void);
/* Once per native stage simulation tick, before fighter CPU/physics. */
void pc_desktop_update_platforms(void);
/* A drop through one segment also skips its connected wallpaper contour. */
bool pc_desktop_wallpaper_lines_connected(int first, int second);
void pc_desktop_stage_ready(void);
bool pc_desktop_collision_fixed(void);
float pc_desktop_cpu_vertical_margin(float original);
/* Called after a real simulation tick; emits a few bounded startup snapshots. */
void pc_desktop_simulation_tick(void);
bool pc_desktop_apply_camera(struct HSD_CObj* camera,
                             struct CameraTransformState* transform);
bool pc_desktop_spawn_position(int slot, bool rebirth, float* x, float* y,
                               float* z);
void pc_desktop_hud_position(int slot, int count, float* x, float* y);

#ifdef __cplusplus
}
#endif
