/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_game.h"

#include "pc.h"
#include "widescreen.h"
#include <aurora/desktop.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_unsplit.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/it/types.h>
#include <melee/mn/types.h>
#include <melee/mp/mpcoll.h>
#include <melee/mp/mpisland.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>

static bool match_armed;
static bool collision_ready;
static bool match_active;
static AuroraDesktopGeometry geometry;
static float view_left, view_right, view_top, view_bottom;
static float floor_left, floor_right;
static unsigned simulation_ticks;
static int desktop_roster_size = 2;

/* These match mpLibLoad's existing runtime capacities. Reserve the bounded
 * pool in a private descriptor copy, so original archive data and line IDs
 * remain intact and ordinary stage initialization can still use them. */
enum {
    desktop_vertex_capacity = 2048,
    desktop_line_capacity = 1536,
    desktop_joint_capacity = 256,
    desktop_window_capacity = AURORA_DESKTOP_MAX_WINDOW_EDGES,
    desktop_wallpaper_capacity = AURORA_DESKTOP_MAX_WALLPAPER_EDGES,
    desktop_platform_capacity =
        desktop_window_capacity + desktop_wallpaper_capacity,
};
static MapCollData window_map;
static DiscVec2 window_vertices[desktop_vertex_capacity];
static MapLine window_lines[desktop_line_capacity];
static MapJoint window_joints[desktop_joint_capacity];
static int window_vertex_start, window_line_start, window_joint_start;
static bool window_pool_prepared, window_pool_loaded;
static unsigned platform_tick;
static unsigned active_window_platforms, active_wallpaper_platforms;

struct DesktopPlatform {
    AuroraDesktopWindowEdge edge;
    AuroraDesktopWallpaperEdge wallpaper;
    uint64_t generation; /* zero for windows; wallpaper snapshot generation */
    unsigned retired_tick;
    bool active;
    bool used;
};
static struct DesktopPlatform platforms[desktop_platform_capacity];

static bool window_platforms_requested(void)
{
    const char* value = getenv("MELEE_DESKTOP_WINDOWS");
    return value == NULL || strcmp(value, "0") != 0;
}

static bool wallpaper_platforms_requested(void)
{
    const char* value = getenv("MELEE_DESKTOP_WALLPAPER");
    return value != NULL && strcmp(value, "1") == 0;
}

static int option_int(const char* name, int fallback, int lo, int hi)
{
    const char* value = getenv(name);
    char* end;
    long parsed;
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno || end == value || *end != '\0') {
        pc_log_line("desktop: invalid %s; using %d", name, fallback);
        return fallback;
    }
    if (parsed < lo) {
        return lo;
    }
    if (parsed > hi) {
        return hi;
    }
    return (int) parsed;
}

bool pc_desktop_requested(void)
{
    const char* value = getenv("MELEE_DESKTOP");
    return value != NULL && strcmp(value, "1") == 0;
}

bool pc_desktop_match_active(void)
{
    return match_active;
}

int pc_desktop_player_count(void)
{
    return desktop_roster_size;
}

void pc_desktop_configure_match(StartMeleeData* start)
{
    static const char* character_keys[] = { "MELEE_DESKTOP_P1", "MELEE_DESKTOP_P2", "MELEE_DESKTOP_P3", "MELEE_DESKTOP_P4" };
    static const char* costume_keys[] = { "MELEE_DESKTOP_COSTUME1", "MELEE_DESKTOP_COSTUME2", "MELEE_DESKTOP_COSTUME3", "MELEE_DESKTOP_COSTUME4" };
    static const char* control_keys[] = { "MELEE_DESKTOP_CONTROL1", "MELEE_DESKTOP_CONTROL2", "MELEE_DESKTOP_CONTROL3", "MELEE_DESKTOP_CONTROL4" };
    static const int defaults[] = { CKind_Fox, CKind_Falco, CKind_Mario, CKind_Mars };
    int i, cpu, watch, stocks, used_ports = 0;
    if (!pc_desktop_requested()) return;
    cpu = option_int("MELEE_DESKTOP_CPU", 5, 0, 9);
    watch = option_int("MELEE_DESKTOP_WATCH", 0, 0, 1);
    stocks = option_int("MELEE_DESKTOP_STOCKS", 4, 1, 99);
    desktop_roster_size = option_int("MELEE_DESKTOP_PLAYERS", 2, 2, 4);
    gm_SetupRulesDefaults(&start->rules);
    start->rules.stkind = St_Kind_Last;
    start->rules.match_kind = MatchKind_Stock;
    start->rules.is_stock = true;
    start->rules.is_vs = true;
    start->rules.is_teams = false;
    start->rules.item_freq = -1;
    start->rules.timer_enabled = false;
    start->rules.time_limit = 0;
    start->rules.sd_penalty = -1;
    for (i = 0; i < GM_MAX_PLAYERS; ++i) gm_SetupPlayerDefaults(&start->players[i]);
    for (i = 0; i < desktop_roster_size; ++i) {
        PlayerInitData* player = &start->players[i];
        const int legacy_control = watch || (i != 0 && cpu != 0) ? 0 : i + 1;
        int control = option_int(control_keys[i], legacy_control, 0, 4);
        int colors, attempt;
        if (control && (used_ports & (1 << control))) {
            pc_log_line("desktop: P%d repeats controller port %d; using CPU", i+1, control);
            control = 0;
        }
        if (control) used_ports |= 1 << control;
        player->ckind = option_int(character_keys[i],defaults[i],0,CKind_Playable_Count-1);
        colors = gm_GetNumCostumesForCKind(player->ckind);
        player->color = option_int(costume_keys[i],0,0,colors > 0 ? colors-1 : 0);
        // Prefer distinct costumes for repeated characters, including P3/P4.
        for (attempt = 0; attempt < colors; ++attempt) {
            int j, repeated = 0;
            for (j = 0; j < i; ++j)
                if (start->players[j].ckind == player->ckind && start->players[j].color == player->color)
                    repeated = 1;
            if (!repeated) break;
            player->color = (player->color+1) % colors;
        }
        player->slot_type = control ? Gm_PKind_Human : Gm_PKind_Cpu;
        player->slot = control ? control : i+1;
        player->stocks = stocks;
        player->cpu_level = cpu ? cpu : 5;
        player->cpu_kind = 4;
        pc_log_line("desktop: P%d character=%d type=%d port=%d level=%d stocks=%d",
            i+1,player->ckind,player->slot_type,player->slot,player->cpu_level,player->stocks);
    }
    pc_log_line("desktop: %d-fighter free-for-all; stock=%u teams=%u",desktop_roster_size,
        (unsigned)start->rules.is_stock,(unsigned)start->rules.is_teams);
}

void pc_desktop_begin_match(StartMeleeData* start)
{
    float units_per_pixel;
    pc_desktop_end_match();
    if (!pc_desktop_requested() || start->rules.stkind != St_Kind_Last) {
        return;
    }
    if (!aurora_desktop_get_geometry(&geometry) || geometry.width == 0 ||
        geometry.height == 0 || geometry.platform_left < 0 ||
        geometry.platform_right <= geometry.platform_left ||
        (uint32_t) geometry.platform_right > geometry.width ||
        geometry.platform_top <= 0 ||
        (uint32_t) geometry.platform_top >= geometry.height ||
        geometry.platform_bottom < geometry.platform_top ||
        (uint32_t) geometry.platform_bottom > geometry.height) {
        pc_log_line("desktop: no usable monitor/taskbar geometry; keeping the normal window");
        return;
    }
    /* A fixed 240 world-unit screen height keeps the fighters useful desktop
     * size without changing their models, movement speed or hitboxes. */
    units_per_pixel = 240.0f / geometry.height;
    view_left = -0.5f * geometry.width * units_per_pixel;
    view_right = -view_left;
    view_top = geometry.platform_top * units_per_pixel;
    view_bottom = view_top - 240.0f;
    floor_left = view_left + geometry.platform_left * units_per_pixel;
    floor_right = view_left + geometry.platform_right * units_per_pixel;
    match_armed = true;
    pc_log_line("desktop: monitor origin=%d,%d size=%ux%u platform=[%d,%d,%d,%d] fighters=%d",
                geometry.screen_x, geometry.screen_y, geometry.width, geometry.height,
                geometry.platform_left, geometry.platform_top, geometry.platform_right,
                geometry.platform_bottom, desktop_roster_size);
}

MapCollData* pc_desktop_prepare_collision(MapCollData* source)
{
    int i, dynamic_start, dynamic_count;
    if (!match_armed || stage_info.grkind != Gr_Kind_Last || source == NULL ||
        (!window_platforms_requested() && !wallpaper_platforms_requested())) {
        return source;
    }
    dynamic_start = source->ranges[MapLineGroup_Dynamic].start;
    dynamic_count = source->ranges[MapLineGroup_Dynamic].count;
    if (source->vert_count <= 0 || source->line_count <= 0 ||
        source->joint_count <= 0 ||
        source->vert_count > desktop_vertex_capacity - 2 * desktop_platform_capacity ||
        source->line_count > desktop_line_capacity - desktop_platform_capacity ||
        source->joint_count > desktop_joint_capacity - desktop_platform_capacity ||
        dynamic_count < 0 || (dynamic_count != 0 &&
        (dynamic_start < 0 || dynamic_start + dynamic_count != source->line_count)) ||
        DP(DiscVec2, source->verts) == NULL || DP(MapLine, source->lines) == NULL ||
        DP(MapJoint, source->joints) == NULL) {
        pc_log_line("desktop: stage has no safe room for desktop platforms; using taskbar only");
        return source;
    }
    window_map = *source;
    window_vertex_start = source->vert_count;
    window_line_start = source->line_count;
    window_joint_start = source->joint_count;
    memcpy(window_vertices, DP(DiscVec2, source->verts),
           sizeof(window_vertices[0]) * source->vert_count);
    memcpy(window_lines, DP(MapLine, source->lines),
           sizeof(window_lines[0]) * source->line_count);
    memcpy(window_joints, DP(MapJoint, source->joints),
           sizeof(window_joints[0]) * source->joint_count);
    for (i = 0; i < desktop_platform_capacity; ++i) {
        int vertex = window_vertex_start + 2 * i;
        int line = window_line_start + i;
        MapLine* desc = &window_lines[line];
        MapJoint* joint = &window_joints[window_joint_start + i];
        /* Nondegenerate placeholders survive the normal empty-line prune.
         * The whole joint is disabled before any fighters are created. */
        window_vertices[vertex] = (DiscVec2) { -1.0f, 0.0f };
        window_vertices[vertex + 1] = (DiscVec2) { 1.0f, 0.0f };
        memset(desc, 0, sizeof(*desc));
        desc->v0_idx = vertex;
        desc->v1_idx = vertex + 1;
        desc->prev_id0 = desc->next_id0 = -1;
        desc->prev_id1 = desc->next_id1 = -1;
        /* 0x10 is the native dynamic-island marker, separate from the
         * one-way platform bit in lo_flags. */
        desc->hi_flags = CollLine_Floor | 0x10;
        desc->lo_flags = LINE_FLAG_PLATFORM | mp_Terrain_LightMetal;
        memset(joint, 0, sizeof(*joint));
        joint->ranges[MapLineGroup_Dynamic].start = line;
        joint->ranges[MapLineGroup_Dynamic].count = 1;
        joint->vtx_start = vertex;
        joint->vtx_count = 2;
        joint->left_bound = -1.0f;
        joint->right_bound = 1.0f;
        joint->bottom_bound = -1.0f;
        joint->top_bound = 1.0f;
    }
    /* Wallpaper segments form one terrain mesh. A native dynamic island
     * follows adjacent lines, so assigning one joint per segment would
     * duplicate the entire contour in the CPU navigation list. Keep the
     * reserved IDs but give every wallpaper line to one shared joint. */
    window_joints[window_joint_start + desktop_window_capacity]
        .ranges[MapLineGroup_Dynamic].count = desktop_wallpaper_capacity;
    window_joints[window_joint_start + desktop_window_capacity].vtx_count =
        2 * desktop_wallpaper_capacity;
    for (i = desktop_window_capacity + 1; i < desktop_platform_capacity; ++i) {
        window_joints[window_joint_start + i]
            .ranges[MapLineGroup_Dynamic].count = 0;
        window_joints[window_joint_start + i].vtx_count = 0;
    }
    window_map.vert_count += 2 * desktop_platform_capacity;
    window_map.line_count += desktop_platform_capacity;
    window_map.joint_count += desktop_platform_capacity;
    window_map.ranges[MapLineGroup_Dynamic].start =
        dynamic_count != 0 ? dynamic_start : window_line_start;
    window_map.ranges[MapLineGroup_Dynamic].count =
        dynamic_count + desktop_platform_capacity;
    DP_SET(window_map.verts, window_vertices);
    DP_SET(window_map.lines, window_lines);
    DP_SET(window_map.joints, window_joints);
    window_pool_prepared = true;
    return &window_map;
}

static bool platform_line_referenced(int line)
{
    HSD_GObj* object;
    if (HSD_GObjPLinkHead == NULL) {
        return false;
    }
    /* A paused/hitlagged actor may retain its old floor for several ticks.
     * Never recycle that line into an unrelated window underneath it. Even
     * airborne references are retained until normal collision clears them. */
    for (object = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; object != NULL;
         object = object->next) {
        Fighter* fighter = object->user_data;
        if (fighter != NULL && fighter->coll_data.floor.index == line) {
            return true;
        }
    }
    for (object = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; object != NULL;
         object = object->next) {
        Item* item = object->user_data;
        if (item != NULL && item->x378_itemColl.floor.index == line) {
            return true;
        }
    }
    return false;
}

static int64_t window_horizontal_motion(const AuroraDesktopWindowEdge* old,
                                         const AuroraDesktopWindowEdge* edge)
{
    int64_t old_width = (int64_t) old->window_right - old->window_left;
    int64_t new_width = (int64_t) edge->window_right - edge->window_left;
    /* Resizing changes the edge's extent; it does not stretch a rider's
     * position. A translated window of unchanged width carries normally. */
    return old_width == new_width ?
        (int64_t) edge->window_left - old->window_left : 0;
}

static bool valid_window_edge(const AuroraDesktopWindowEdge* edge)
{
    return edge->window_id != 0 && edge->left >= 0 && edge->left < edge->right &&
        (uint32_t) edge->right <= geometry.width && edge->top >= 0 &&
        edge->top < geometry.platform_top &&
        edge->window_left < edge->window_right &&
        edge->window_top < edge->window_bottom && edge->top == edge->window_top;
}

static double wallpaper_height(const AuroraDesktopWallpaperEdge* edge, int x)
{
    return edge->source_top +
        ((double) edge->source_right_top - edge->source_top) *
        ((double) x - edge->source_left) /
        ((double) edge->source_right - edge->source_left);
}

static bool valid_wallpaper_edge(const AuroraDesktopWallpaperEdge* edge)
{
    if (edge->generation == 0 || edge->source_left < 0 ||
        edge->source_left >= edge->source_right ||
        (uint32_t) edge->source_right > geometry.width ||
        edge->source_top < 0 || edge->source_right_top < 0 ||
        edge->source_top >= geometry.platform_top ||
        edge->source_right_top >= geometry.platform_top ||
        edge->left < edge->source_left || edge->right > edge->source_right ||
        edge->left >= edge->right || edge->top < 0 || edge->right_top < 0 ||
        edge->top >= geometry.platform_top ||
        edge->right_top >= geometry.platform_top) {
        return false;
    }
    /* The public pixel endpoints are rounded for occlusion. Use the immutable
     * source line for collision so repeated clipping never tilts the floor. */
    return fabs(wallpaper_height(edge, edge->left) - edge->top) <= 0.501 &&
        fabs(wallpaper_height(edge, edge->right) - edge->right_top) <= 0.501;
}

static bool same_wallpaper_geometry(const AuroraDesktopWallpaperEdge* a,
                                    const AuroraDesktopWallpaperEdge* b)
{
    return a->source_left == b->source_left &&
        a->source_right == b->source_right && a->source_top == b->source_top &&
        a->source_right_top == b->source_right_top &&
        a->contour_id == b->contour_id;
}

static void update_platform_slot(int index, const AuroraDesktopWindowEdge* edge,
                                 const AuroraDesktopWallpaperEdge* wallpaper)
{
    struct DesktopPlatform* platform = &platforms[index];
    int joint_id = window_joint_start + index;
    CollJoint* joint = &mpGetGroundCollJoint()[joint_id];
    CollVtx* vertices = &mpGetGroundCollVtx()[window_vertex_start + 2 * index];
    float units = 240.0f / geometry.height;
    float dx = 0.0f, dy = 0.0f;
    float left = view_left + edge->left * units;
    float right = view_left + edge->right * units;
    float top = view_top - edge->top * units;
    float right_top = top;
    float scale = Ground_801C0498();
    if (wallpaper != NULL) {
        top = view_top - wallpaper_height(wallpaper, edge->left) * units;
        right_top = view_top - wallpaper_height(wallpaper, edge->right) * units;
    } else if (platform->active) {
        dx = window_horizontal_motion(&platform->edge, edge) * units;
        dy = ((int64_t) platform->edge.window_top - edge->window_top) * units;
    }
    vertices[0].pos = (Vec2) { left, top };
    vertices[1].pos = (Vec2) { right, right_top };
    /* Native mpGetSpeed and swept floor collision now see only the real
     * window translation, never a fake displacement caused by clipping or
     * width changes. Stationary snapshots produce zero carry each tick. */
    vertices[0].prev_pos = (Vec2) { left - dx, top - dy };
    vertices[1].prev_pos = (Vec2) { right - dx, right_top - dy };
    if (scale > 0.0f) {
        vertices[0].base_pos = (Vec2) { left / scale, top / scale };
        vertices[1].base_pos = (Vec2) { right / scale, right_top / scale };
    }
    if (wallpaper != NULL) {
        /* The shared mesh's bounds, adjacency and native CPU islands are
         * rebuilt after every slot has been assigned, never mid-update. */
        platform->wallpaper = *wallpaper;
    } else {
        joint->bounding_min = (Vec2) {
            fminf(left, left - dx) - 30.0f, fminf(top, top - dy) - 30.0f,
        };
        joint->bounding_max = (Vec2) {
            fmaxf(right, right - dx) + 30.0f, fmaxf(top, top - dy) + 30.0f,
        };
        joint->flags |= CollJoint_B8;
        joint->xE = true;
        if (!platform->active) {
            mpGetGroundCollLine()[window_line_start + index].flags =
                CollLine_Floor | 0x10;
            mpJointListAdd(joint_id);
        } else {
            mpLib_8005667C(joint_id);
        }
    }
    platform->edge = *edge;
    platform->generation = wallpaper != NULL ? wallpaper->generation : 0;
    platform->active = true;
    platform->used = true;
}

static void refresh_wallpaper_mesh(void)
{
    int i, j;
    int joint_id = window_joint_start + desktop_window_capacity;
    CollJoint* joint = &mpGetGroundCollJoint()[joint_id];
    CollLine* lines = mpGetGroundCollLine();
    CollVtx* vertices = mpGetGroundCollVtx();
    float min_x = F32_MAX, min_y = F32_MAX;
    float max_x = -F32_MAX, max_y = -F32_MAX;
    bool any = false;
    for (i = desktop_window_capacity; i < desktop_platform_capacity; ++i) {
        CollLine* line = &lines[window_line_start + i];
        line->inner->prev_id0 = line->inner->next_id0 = -1;
        line->inner->prev_id1 = line->inner->next_id1 = -1;
        line->flags = platforms[i].active ?
            CollLine_Floor | 0x10 | LINE_FLAG_ENABLED : 0;
        if (platforms[i].active) {
            const CollVtx* v = &vertices[window_vertex_start + 2 * i];
            any = true;
            min_x = fminf(min_x, v[0].pos.x);
            max_x = fmaxf(max_x, v[1].pos.x);
            min_y = fminf(min_y, fminf(v[0].pos.y, v[1].pos.y));
            max_y = fmaxf(max_y, fmaxf(v[0].pos.y, v[1].pos.y));
        }
    }
    for (i = desktop_window_capacity; i < desktop_platform_capacity; ++i) {
        const struct DesktopPlatform* left = &platforms[i];
        if (!left->active || left->wallpaper.contour_id == 0) {
            continue;
        }
        for (j = desktop_window_capacity; j < desktop_platform_capacity; ++j) {
            const struct DesktopPlatform* right = &platforms[j];
            if (!right->active || left->generation != right->generation ||
                left->wallpaper.contour_id != right->wallpaper.contour_id ||
                left->edge.right != right->edge.left) {
                continue;
            }
            /* Only exact endpoints in the same original contour join. A
             * covered gap, unrelated ledge or retired floor cannot bridge. */
            const CollVtx* a = &vertices[window_vertex_start + 2 * i + 1];
            const CollVtx* b = &vertices[window_vertex_start + 2 * j];
            if (a->pos.x == b->pos.x && a->pos.y == b->pos.y) {
                lines[window_line_start + i].inner->next_id0 =
                    window_line_start + j;
                lines[window_line_start + j].inner->prev_id0 =
                    window_line_start + i;
                break;
            }
        }
    }
    joint->flags |= CollJoint_B8;
    joint->xE = true;
    if (any) {
        joint->bounding_min = (Vec2) { min_x - 30.0f, min_y - 30.0f };
        joint->bounding_max = (Vec2) { max_x + 30.0f, max_y + 30.0f };
        mpJointListAdd(joint_id);
        /* Re-enabling a joint sets every member's enabled bit. Clear the
         * unused/retired lines before publishing the updated mesh. */
        for (i = desktop_window_capacity; i < desktop_platform_capacity; ++i) {
            if (!platforms[i].active) {
                lines[window_line_start + i].flags = 0;
            }
        }
        mpLib_8005667C(joint_id);
    } else {
        mpLib_80057BC0(joint_id);
    }
}

bool pc_desktop_wallpaper_lines_connected(int first, int second)
{
    int a, b;
    if (!window_pool_loaded || !pc_desktop_collision_fixed() ||
        first < window_line_start + desktop_window_capacity ||
        first >= window_line_start + desktop_platform_capacity ||
        second < window_line_start + desktop_window_capacity ||
        second >= window_line_start + desktop_platform_capacity) {
        return false;
    }
    a = first - window_line_start;
    b = second - window_line_start;
    if (!platforms[a].active || !platforms[b].active ||
        platforms[a].generation != platforms[b].generation ||
        platforms[a].wallpaper.contour_id == 0 ||
        platforms[a].wallpaper.contour_id != platforms[b].wallpaper.contour_id) {
        return false;
    }
    /* A covering window may split one source contour into separate native
     * islands. Do not skip a disconnected island on the far side of a gap. */
    return mpLinesConnected(first, second);
}

void pc_desktop_update_platforms(void)
{
    AuroraDesktopWindowEdge edges[desktop_platform_capacity];
    AuroraDesktopWallpaperEdge wallpaper[desktop_wallpaper_capacity];
    uint64_t generations[desktop_platform_capacity] = { 0 };
    int assigned[desktop_platform_capacity];
    bool matched[desktop_platform_capacity] = { false };
    unsigned count, window_count, wallpaper_count, i, j;
    if (!window_pool_loaded || !pc_desktop_collision_fixed()) {
        return;
    }
    ++platform_tick;
    window_count = window_platforms_requested() ?
        aurora_desktop_get_window_edges(edges, desktop_window_capacity) : 0;
    if (window_count > desktop_window_capacity) {
        window_count = desktop_window_capacity;
    }
    for (i = 0; i < window_count; ++i) {
        assigned[i] = -1;
        if (!valid_window_edge(&edges[i])) {
            edges[i].window_id = 0;
        }
    }
    /* The provider enforces opt-in itself. Call it even after an opt-out so
     * an already-running worker is disabled and clears its published data. */
    wallpaper_count =
        aurora_desktop_get_wallpaper_edges(wallpaper, desktop_wallpaper_capacity);
    if (wallpaper_count > desktop_wallpaper_capacity) {
        wallpaper_count = desktop_wallpaper_capacity;
    }
    for (i = 0; i < wallpaper_count; ++i) {
        const AuroraDesktopWallpaperEdge* edge = &wallpaper[i];
        unsigned index = window_count + i;
        assigned[index] = -1;
        generations[index] = edge->generation;
        edges[index] = (AuroraDesktopWindowEdge) {
            .window_id = (uint64_t) edge->edge_id + 1,
            .left = edge->left, .right = edge->right, .top = edge->top,
            .window_left = 0, .window_top = 0,
            .window_right = geometry.width, .window_bottom = geometry.height,
        };
        if (!valid_wallpaper_edge(edge)) {
            edges[index].window_id = 0;
        }
    }
    count = window_count + wallpaper_count;
    /* Resolve maximum-overlap pairs first. Fragment ordinals can shift when
     * a covering window splits/merges an edge, so they are never identities. */
    for (;;) {
        int best_slot = -1, best_edge = -1;
        int64_t best_overlap = 0;
        for (i = 0; i < desktop_platform_capacity; ++i) {
            const struct DesktopPlatform* platform = &platforms[i];
            if (!platform->active || matched[i]) {
                continue;
            }
            for (j = 0; j < count; ++j) {
                int64_t left, right, overlap, dx;
                if (assigned[j] != -1 || edges[j].window_id == 0 ||
                    edges[j].window_id != platform->edge.window_id ||
                    (i >= desktop_window_capacity) != (j >= window_count) ||
                    platform->generation != generations[j]) {
                    continue;
                }
                /* Wallpaper has no motion. Occlusion may split or shorten
                 * an edge within one generation, but a changed height must
                 * retire the old floor rather than snap a rider vertically. */
                if (j >= window_count && !same_wallpaper_geometry(
                        &platform->wallpaper, &wallpaper[j - window_count])) {
                    continue;
                }
                dx = window_horizontal_motion(&platform->edge, &edges[j]);
                left = platform->edge.left + dx;
                right = platform->edge.right + dx;
                if (left < edges[j].left) left = edges[j].left;
                if (right > edges[j].right) right = edges[j].right;
                overlap = right - left;
                if (overlap > best_overlap) {
                    best_overlap = overlap;
                    best_slot = i;
                    best_edge = j;
                }
            }
        }
        if (best_slot < 0) {
            break;
        }
        matched[best_slot] = true;
        assigned[best_edge] = best_slot;
    }
    for (i = 0; i < desktop_platform_capacity; ++i) {
        if (platforms[i].active && !matched[i]) {
            /* Some native CPU island scans do not inspect hidden flags.
             * Remove our dynamic-kind marker so native island refresh drops
             * the inactive segment entirely instead of leaving a ghost. */
            mpGetGroundCollLine()[window_line_start + i].flags &= ~0x1F;
            if (i < desktop_window_capacity) {
                mpLib_80057BC0(window_joint_start + i);
            } else {
                mpGetGroundCollLine()[window_line_start + i].flags &= ~LINE_FLAG_ENABLED;
            }
            platforms[i].active = false;
            platforms[i].retired_tick = platform_tick;
        }
    }
    active_window_platforms = 0;
    active_wallpaper_platforms = 0;
    for (j = 0; j < count; ++j) {
        int slot = assigned[j];
        if (edges[j].window_id == 0) {
            continue;
        }
        if (slot < 0) {
            for (i = 0; i < desktop_platform_capacity; ++i) {
                if ((i >= desktop_window_capacity) != (j >= window_count) ||
                    platforms[i].active || matched[i] ||
                    (platforms[i].used &&
                     (unsigned) (platform_tick - platforms[i].retired_tick) < 2) ||
                    platform_line_referenced(window_line_start + i)) {
                    continue;
                }
                slot = i;
                matched[i] = true;
                break;
            }
        }
        if (slot >= 0) {
            update_platform_slot(slot, &edges[j],
                j < window_count ? NULL : &wallpaper[j - window_count]);
            if (j < window_count) {
                ++active_window_platforms;
            } else {
                ++active_wallpaper_platforms;
            }
        }
    }
    refresh_wallpaper_mesh();
    /* Force swept collision to use the moving-floor path, and clear any
     * previous broad-phase query before fighter/item collision begins. */
    ++mpColl_804D64AC;
    mpUncheckBounding();
}

void pc_desktop_collision_loaded(void)
{
    int i;
    if (!window_pool_prepared) {
        return;
    }
    for (i = 0; i < desktop_platform_capacity; ++i) {
        mpGetGroundCollLine()[window_line_start + i].flags &= ~0x1F;
    }
    for (i = 0; i < desktop_platform_capacity; ++i) {
        mpGetGroundCollJoint()[window_joint_start + i].flags |= CollJoint_B8;
        mpLib_80057BC0(window_joint_start + i);
    }
    window_pool_loaded = true;
    pc_desktop_update_platforms();
    pc_log_line("desktop: platform pool=%d windows=%u wallpaper=%u",
                desktop_platform_capacity, active_window_platforms,
                active_wallpaper_platforms);
}

void pc_desktop_remap_collision(MapCollData* data)
{
    CollVtx* vertices;
    CollJoint* joints;
    CollLine* lines;
    float min_x = F32_MAX, max_x = -F32_MAX, top_y = -F32_MAX;
    float scale_x, source_center, target_center, scale;
    int i, first, last;
    if (!match_armed || stage_info.grkind != Gr_Kind_Last || data == NULL) {
        return;
    }
    vertices = mpGetGroundCollVtx();
    joints = mpGetGroundCollJoint();
    lines = mpGetGroundCollLine();
    first = data->ranges[MapLineGroup_Floor].start;
    last = first + data->ranges[MapLineGroup_Floor].count;
    if (first < 0 || last > data->line_count) {
        return;
    }
    for (i = first; i < last; ++i) {
        MapLine* line = lines[i].inner;
        int ids[2] = { line->v0_idx, line->v1_idx };
        int j;
        for (j = 0; j < 2; ++j) {
            if (ids[j] >= data->vert_count) {
                return;
            }
            min_x = fminf(min_x, vertices[ids[j]].pos.x);
            max_x = fmaxf(max_x, vertices[ids[j]].pos.x);
            top_y = fmaxf(top_y, vertices[ids[j]].pos.y);
        }
    }
    if (!(max_x > min_x) || !isfinite(top_y)) {
        pc_log_line("desktop: Final Destination collision was not recognized");
        return;
    }
    scale = Ground_801C0498();
    if (!(scale > 0.0f)) {
        return;
    }
    scale_x = (floor_right - floor_left) / (max_x - min_x);
    source_center = 0.5f * (max_x + min_x);
    target_center = 0.5f * (floor_left + floor_right);
    for (i = 0; i < data->vert_count; ++i) {
        CollVtx* vertex = &vertices[i];
        vertex->pos.x = (vertex->pos.x - source_center) * scale_x + target_center;
        vertex->pos.y -= top_y;
        vertex->prev_pos = vertex->pos;
        vertex->base_pos.x = vertex->pos.x / scale;
        vertex->base_pos.y = vertex->pos.y / scale;
    }
    for (i = 0; i < data->joint_count; ++i) {
        joints[i].bounding_min.x =
            (joints[i].bounding_min.x - source_center) * scale_x + target_center - 30.0f;
        joints[i].bounding_max.x =
            (joints[i].bounding_max.x - source_center) * scale_x + target_center + 30.0f;
        joints[i].bounding_min.y -= top_y + 30.0f;
        joints[i].bounding_max.y -= top_y - 30.0f;
    }
    /* This runs before mpIsland_8005A728, so CPU navigation, ledge grabs and
     * floor queries all use the remapped vertices from the outset. No disc
     * data is modified, and the next match starts from original geometry. */
    collision_ready = true;
}

void pc_desktop_stage_ready(void)
{
    if (!pc_desktop_collision_fixed()) {
        return;
    }
    /* Stage on_init restores the authored bounds after mpLibLoad. Apply our
     * bounds only once that initializer has finished, before fighter spawn. */
    stage_info.cam_info.cam_x_offset = 0.0f;
    stage_info.cam_info.cam_y_offset = 0.0f;
    stage_info.blast_zone.left = view_left;
    stage_info.blast_zone.right = view_right;
    stage_info.blast_zone.top = view_top;
    stage_info.blast_zone.bottom = view_bottom;
    stage_info.cam_info.cam_bounds = stage_info.blast_zone;
}

bool pc_desktop_collision_fixed(void)
{
    return match_armed && collision_ready;
}

float pc_desktop_cpu_vertical_margin(float original)
{
    float clearance;
    if (!pc_desktop_collision_fixed()) {
        return original;
    }
    /* Normal CPU targeting and floor selection exclude a ten-unit band
     * inside the blastzones. A short taskbar can put our floor (y=0) inside
     * that band, making every standing opponent invalid. Keep a safety band
     * within the available clearance without moving collision or KO bounds.
     * This is applied by the ordinary CPU initializer, including respawns. */
    clearance = fminf(-view_bottom, view_top);
    return fminf(original, fmaxf(0.0f, 0.5f * clearance));
}

void pc_desktop_simulation_tick(void)
{
    VsSceneState* state;
    Fighter* fighters[4];
    int i;
    if (!match_active || simulation_ticks >= 300) {
        return;
    }
    ++simulation_ticks;
    if (simulation_ticks != 1 && simulation_ticks != 120 &&
        simulation_ticks != 300) {
        return;
    }
    state = &gmVs_GetSceneController()->state;
    pc_log_line("desktop: simulation tick=%u match_frame=%u hud=%u "
                "pause=%d/%d bottom=%.3f cpu_vertical_margin=%.3f "
                "window_platforms=%u wallpaper_platforms=%u",
                simulation_ticks, (unsigned) state->frame_count,
                (unsigned) state->hud_enabled,
                gm_GetDbPauseFlag(0), gm_GetDbPauseFlag(1),
                view_bottom, pc_desktop_cpu_vertical_margin(10.0f),
                active_window_platforms, active_wallpaper_platforms);
    for (i = 0; i < desktop_roster_size; ++i) {
        HSD_GObj* entity = Player_GetEntity(i);
        fighters[i] = entity != NULL ? entity->user_data : NULL;
    }
    for (i = 0; i < desktop_roster_size; ++i) {
        Fighter* fighter = fighters[i];
        int target = -1;
        if (fighter == NULL) {
            pc_log_line("desktop: P%d entity=none type=%d stocks=%d",
                        i + 1, Player_GetPlayerSlotType(i), Player_GetStocks(i));
            continue;
        }
        /* Compare target identities instead of dereferencing a target that
         * may have been removed later in this simulation tick. */
        if (fighter->cpu.x44 != NULL) {
            int j;
            target = -2;
            for (j = 0; j < desktop_roster_size; ++j)
                if (fighter->cpu.x44 == fighters[j]) target = j+1;
        }
        pc_log_line("desktop: P%d type=%d stocks=%d cpu=%d level=%d "
                    "cpu_tick=%d target=%d input_disabled=%u sleeping=%u "
                    "motion=%d position=%.2f,%.2f stick=%d,%d buttons=%x "
                    "vertical_margin=%.3f",
                    i + 1, Player_GetPlayerSlotType(i), Player_GetStocks(i),
                    fighter->cpu.kind, fighter->cpu.level, fighter->cpu.x7C,
                    target, (unsigned) fighter->input_disabled,
                    (unsigned) fighter->is_sleeping, fighter->motion_id,
                    fighter->cur_pos.x, fighter->cur_pos.y,
                    fighter->cpu.lstick.x, fighter->cpu.lstick.y,
                    (unsigned) fighter->cpu.buttons, fighter->cpu.half_height);
    }
}

bool pc_desktop_apply_camera(HSD_CObj* camera, CameraTransformState* transform)
{
    Vec3 eye, interest;
    float center_y;
    if (!pc_desktop_collision_fixed() || camera == NULL) {
        return false;
    }
    center_y = 0.5f * (view_top + view_bottom);
    interest = (Vec3) { 0.0f, center_y, 0.0f };
    eye = (Vec3) { 0.0f, center_y, 1000.0f };
    HSD_CObjSetOrtho(camera, 120.0f, -120.0f, view_left, view_right);
    HSD_CObjSetFlags(camera, PC_COBJ_FILL_FRAME);
    HSD_CObjSetInterest(camera, &interest);
    HSD_CObjSetEyePosition(camera, &eye);
    HSD_CObjSetNear(camera, 1.0f);
    HSD_CObjSetFar(camera, 30000.0f);
    if (transform != NULL) {
        transform->interest = transform->target_interest = interest;
        transform->position = transform->target_position = eye;
        /* Keep camera clients that inspect the perspective metadata finite. */
        transform->fov = transform->target_fov = 13.685546f;
    }
    return true;
}

bool pc_desktop_spawn_position(int slot, bool rebirth, float* x, float* y,
                               float* z)
{
    float center, span, spacing;
    if (!pc_desktop_collision_fixed() || slot < 0 || slot >= desktop_roster_size) return false;
    center = 0.5f * (floor_left + floor_right);
    span = fminf(80.0f * (desktop_roster_size-1), (floor_right-floor_left)*0.4f);
    spacing = span / (desktop_roster_size-1);
    *x = center + (slot - 0.5f*(desktop_roster_size-1))*spacing;
    *y = rebirth ? 60.0f : 0.0f;
    *z = 0.0f;
    return true;
}

void pc_desktop_hud_position(int slot, int count, float* x, float* y)
{
    if (!pc_desktop_collision_fixed() || slot < 0 || count < 1) {
        return;
    }
    /* The existing HUD camera is independent of the arena camera. Its
     * authored vertical range is about +/-24 world units. */
    *x = ((float) slot - 0.5f * (count - 1)) * 18.0f;
    *y = 16.5f;
}

bool pc_desktop_apply_hud_camera(HSD_CObj* camera)
{
    float scale;
    if (!pc_desktop_collision_fixed() || camera == NULL ||
        geometry.width == 0 || geometry.height == 0) {
        return false;
    }
    scale = fminf(1.0f, fminf(1920.0f / (float) geometry.width,
                              1080.0f / (float) geometry.height));
    if (!(scale > 0.0f) || scale >= 0.999f) {
        return true;
    }
    switch (HSD_CObjGetProjectionType(camera)) {
    case PROJ_ORTHO: {
        float top, bottom, left, right;
        float cx, cy, hx, hy;
        HSD_CObjGetOrtho(camera, &top, &bottom, &left, &right);
        cx = 0.5f * (left + right);
        cy = 0.5f * (top + bottom);
        hx = 0.5f * (right - left) / scale;
        hy = 0.5f * (top - bottom) / scale;
        HSD_CObjSetOrtho(camera, cy + hy, cy - hy, cx - hx, cx + hx);
        return true;
    }
    case PROJ_FRUSTUM: {
        float top = camera->projection_param.frustum.top;
        float bottom = camera->projection_param.frustum.bottom;
        float left = camera->projection_param.frustum.left;
        float right = camera->projection_param.frustum.right;
        float cx = 0.5f * (left + right);
        float cy = 0.5f * (top + bottom);
        float hx = 0.5f * (right - left) / scale;
        float hy = 0.5f * (top - bottom) / scale;
        HSD_CObjSetFrustum(camera, cy + hy, cy - hy, cx - hx, cx + hx);
        return true;
    }
    case PROJ_PERSPECTIVE: {
        float fov = camera->projection_param.perspective.fov / scale;
        if (fov > 150.0f) {
            fov = 150.0f;
        }
        HSD_CObjSetFov(camera, fov);
        return true;
    }
    default:
        return false;
    }
}

void pc_desktop_match_loaded(void)
{
    if (pc_desktop_collision_fixed()) {
        Camera_SetStageVisible(false);
        Camera_SetQuakeScale(0.0f);
        match_active = true;
    }
}

void pc_desktop_end_match(void)
{
    match_active = false;
    collision_ready = false;
    match_armed = false;
    simulation_ticks = 0;
    window_pool_prepared = false;
    window_pool_loaded = false;
    platform_tick = 0;
    active_window_platforms = 0;
    active_wallpaper_platforms = 0;
    memset(platforms, 0, sizeof(platforms));
}
