/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ROM-free integration tests for the real Melee collision implementation.
 * mpLibLoad, floor intersection, grounded follow, movement deltas, platform
 * drop-through and navigation islands are linked from production sources.
 * Only allocation, disc-pointer registration and hardware/scene boundaries
 * are supplied by this fixture. No collision result is mocked. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aurora/desktop.h>
#include <melee/db/db.h>
#include <melee/ft/types.h>
#include <melee/gm/types.h>
#include <melee/gr/grdynamicattr.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/types.h>
#include <melee/lb/types.h>
#include <melee/mp/mpcoll.h>
#include <melee/mp/mpisland.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <pc/desktop_game.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/memory.h>

/* GameCube headers declare a different __assert from libc assert.h. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        abort(); \
    } \
} while (0)

DbLKind DbLevel;
StageInfo stage_info;
uintptr_t OSBaseAddress;
static const void* ext_ptrs[1024];
static size_t ext_ptr_count;
static void* allocations[1024];
static size_t allocation_count;
static HSD_GObj* fixture_heads[64];
HSD_GObj** HSD_GObjPLinkHead = fixture_heads;
static AuroraDesktopGeometry supplied_geometry = {
    0, 0, 1920, 1080, 0, 1040, 1920, 1080
};
static AuroraDesktopWindowEdge supplied_edges[AURORA_DESKTOP_MAX_WINDOW_EDGES];
static uint32_t supplied_edge_count;
static AuroraDesktopWallpaperEdge supplied_wallpaper[AURORA_DESKTOP_MAX_WALLPAPER_EDGES];
static uint32_t supplied_wallpaper_count;

bool aurora_desktop_get_geometry(AuroraDesktopGeometry* result)
{
    *result = supplied_geometry;
    return true;
}

uint32_t aurora_desktop_get_window_edges(AuroraDesktopWindowEdge* result,
                                        uint32_t capacity)
{
    const char* setting = getenv("MELEE_DESKTOP_WINDOWS");
    if (setting != NULL && strcmp(setting, "0") == 0) {
        return 0;
    }
    uint32_t count = supplied_edge_count < capacity ? supplied_edge_count : capacity;
    memcpy(result, supplied_edges, count * sizeof(*result));
    return count;
}

uint32_t aurora_desktop_get_wallpaper_edges(AuroraDesktopWallpaperEdge* result,
                                           uint32_t capacity)
{
    const char* setting = getenv("MELEE_DESKTOP_WALLPAPER");
    if (setting == NULL || strcmp(setting, "1") != 0) {
        return 0;
    }
    uint32_t count = supplied_wallpaper_count < capacity ?
                         supplied_wallpaper_count : capacity;
    memcpy(result, supplied_wallpaper, count * sizeof(*result));
    return count;
}

void pc_log_line(const char* format, ...) { (void) format; }

uint32_t pc_register_ext_ptr(const void* pointer)
{
    for (size_t i = 0; i < ext_ptr_count; ++i) {
        if (ext_ptrs[i] == pointer) {
            return (uint32_t) i + 1;
        }
    }
    CHECK(ext_ptr_count < sizeof(ext_ptrs) / sizeof(ext_ptrs[0]));
    ext_ptrs[ext_ptr_count++] = pointer;
    return (uint32_t) ext_ptr_count;
}

void* pc_resolve_ext_ptr(uint32_t id)
{
    CHECK(id > 0 && id <= ext_ptr_count);
    return (void*) ext_ptrs[id - 1];
}

void* HSD_MemAlloc(ssize_t size)
{
    CHECK(size > 0);
    CHECK(allocation_count < sizeof(allocations) / sizeof(allocations[0]));
    void* result = calloc(1, (size_t) size);
    CHECK(result != NULL);
    allocations[allocation_count++] = result;
    return result;
}

void __assert(const char* file, u32 line, const char* condition)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, condition);
    abort();
}

void OSReport(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void memzero(void* memory, ssize_t size)
{
    memset(memory, 0, (size_t) size);
}

void PSVECNormalize(const Vec* input, Vec* output)
{
    const float length = sqrtf(input->x * input->x + input->y * input->y +
                               input->z * input->z);
    CHECK(length > 0.0f);
    *output = (Vec) { input->x / length, input->y / length, input->z / length };
}

float Ground_801C0498(void) { return 1.0f; }
void grDynamicAttr_801CA0B4(void) {}

static MapCollData map;
static DiscVec2 vertices[4];
static MapLine lines[2];
static MapJoint joints[2];

static void near_value(float actual, float expected)
{
    if (!isfinite(actual) || fabsf(actual - expected) > 0.002f) {
        fprintf(stderr, "expected %.8g, got %.8g\n", expected, actual);
        abort();
    }
}

static void load_fixture(void)
{
    memset(&map, 0, sizeof(map));
    memset(vertices, 0, sizeof(vertices));
    memset(lines, 0, sizeof(lines));
    memset(joints, 0, sizeof(joints));
    vertices[0].x = -100.0f;
    vertices[1].x = 100.0f;
    vertices[2].x = -20.0f;
    vertices[2].y = 30.0f;
    vertices[3].x = 20.0f;
    vertices[3].y = 30.0f;
    for (int i = 0; i < 2; ++i) {
        lines[i].v0_idx = 2 * i;
        lines[i].v1_idx = 2 * i + 1;
        lines[i].prev_id0 = lines[i].next_id0 = -1;
        lines[i].prev_id1 = lines[i].next_id1 = -1;
        lines[i].hi_flags = CollLine_Floor | (i == 1 ? 0x10 : 0);
        lines[i].lo_flags = i == 1 ? LINE_FLAG_PLATFORM : 0;
        joints[i].vtx_start = 2 * i;
        joints[i].vtx_count = 2;
        joints[i].left_bound = vertices[2 * i].x - 30.0f;
        joints[i].right_bound = vertices[2 * i + 1].x + 30.0f;
        joints[i].bottom_bound = vertices[2 * i].y - 30.0f;
        joints[i].top_bound = vertices[2 * i].y + 30.0f;
    }
    joints[0].ranges[MapLineGroup_Floor].start = 0;
    joints[0].ranges[MapLineGroup_Floor].count = 1;
    joints[1].ranges[MapLineGroup_Dynamic].start = 1;
    joints[1].ranges[MapLineGroup_Dynamic].count = 1;
    map.vert_count = 4;
    map.line_count = 2;
    map.joint_count = 2;
    map.ranges[MapLineGroup_Floor].start = 0;
    map.ranges[MapLineGroup_Floor].count = 1;
    map.ranges[MapLineGroup_Dynamic].start = 1;
    map.ranges[MapLineGroup_Dynamic].count = 1;
    DP_SET(map.verts, vertices);
    DP_SET(map.lines, lines);
    DP_SET(map.joints, joints);
    MapCollData original_map = map;
    unsigned char original_vertices[sizeof(vertices)];
    unsigned char original_lines[sizeof(lines)];
    unsigned char original_joints[sizeof(joints)];
    memcpy(original_vertices, vertices, sizeof(vertices));
    memcpy(original_lines, lines, sizeof(lines));
    memcpy(original_joints, joints, sizeof(joints));
    mpLibLoad(&map);
    CHECK(memcmp(&map, &original_map, sizeof(map)) == 0);
    CHECK(memcmp(vertices, original_vertices, sizeof(vertices)) == 0);
    CHECK(memcmp(lines, original_lines, sizeof(lines)) == 0);
    CHECK(memcmp(joints, original_joints, sizeof(joints)) == 0);
    mpGetGroundCollJoint()[1].flags |= CollJoint_B8;
    mpLib_80055E24(1);
    CHECK(mpIsland_8005AB54(0) != NULL);
    CHECK(mpIsland_8005AB54(1) != NULL);
}

static CollData sweep(float x, float from_y, float to_y)
{
    CollData result = { 0 };
    result.prev_pos = (Vec3) { x, from_y, 0.0f };
    result.cur_pos = (Vec3) { x, to_y, 0.0f };
    result.floor.index = -1;
    result.floor_skip = -1;
    result.joint_id_skip = -1;
    result.joint_id_only = -1;
    result.left_facing_wall.index = -1;
    result.right_facing_wall.index = -1;
    result.x38 = mpColl_804D64AC;
    return result;
}

static void test_one_way_and_drop_through(void)
{
    CollData landing = sweep(0.0f, 35.0f, 25.0f);
    CHECK(mpColl_80044628_Floor(&landing, NULL, NULL, 0));
    CHECK(landing.floor.index == 1);
    CHECK(mpColl_IsOnPlatform(&landing));
    near_value(landing.contact.y, 30.0f);

    CollData upward = sweep(0.0f, 25.0f, 35.0f);
    CHECK(!mpColl_80044628_Floor(&upward, NULL, NULL, 0));

    mpUpdateFloorSkip(&landing);
    CHECK(landing.floor_skip == 1);
    CHECK(!mpColl_80044628_Floor(&landing, NULL, NULL, 0));
    mpClearFloorSkip(&landing);
    CHECK(mpColl_80044628_Floor(&landing, NULL, NULL, 0));

    CollData floor = sweep(70.0f, 5.0f, -5.0f);
    CHECK(mpColl_80044628_Floor(&floor, NULL, NULL, 0));
    CHECK(floor.floor.index == 0);
    CHECK(!mpColl_IsOnPlatform(&floor));
    puts("PASS native one-way landing, upward pass-through and drop-through");
}

static void move_fixture(float left, float right, float top)
{
    /* This mirrors the native joint update contract, not a collision result.
     * Production window update integration is checked separately below. */
    mpLib_80057424(1);
    mpLineSetPos(1, left, top, right, top);
    CollJoint* joint = &mpGetGroundCollJoint()[1];
    joint->bounding_min = (Vec2) { left - 30.0f, top - 30.0f };
    joint->bounding_max = (Vec2) { right + 30.0f, top + 30.0f };
    joint->xE = true;
    ++mpColl_804D64AC;
    mpLib_80055E24(1);
    mpUncheckBounding();
}

static void test_motion_and_removal(void)
{
    Vec3 rider = { 0.0f, 30.0f, 0.0f };
    Vec3 velocity;
    move_fixture(-5.0f, 35.0f, 37.0f);
    CHECK(mpGetSpeed(1, &rider, &velocity));
    near_value(velocity.x, 15.0f);
    near_value(velocity.y, 7.0f);
    rider.x += velocity.x;
    rider.y += velocity.y;
    mpIsland* island = mpIsland_8005AB54(1);
    CHECK(island != NULL);
    near_value(island->pos0.x, -5.0f);
    near_value(island->pos1.x, 35.0f);
    near_value(island->pos0.y, 37.0f);

    move_fixture(-5.0f, 35.0f, 37.0f);
    CHECK(mpGetSpeed(1, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);

    CollData standing = sweep(rider.x, rider.y, rider.y);
    standing.floor.index = 1;
    CHECK(mpColl_800488F4(&standing));
    mpLib_80057BC0(1);
    CHECK(!mpLib_80054ED8(1));
    CHECK(!mpGetSpeed(1, &rider, &velocity));
    CHECK(!mpColl_800488F4(&standing));
    CHECK(mpIsland_8005AB54(1) == NULL);
    CollData removed = sweep(15.0f, 40.0f, 34.0f);
    CHECK(!mpColl_80044628_Floor(&removed, NULL, NULL, 0));

    /* Re-enabling intentionally preserves identity in this native fixture. */
    mpJointListAdd(1);
    CHECK(mpLib_80054ED8(1));
    CHECK(mpIsland_8005AB54(1) != NULL);
    CHECK(mpGetSpeed(1, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);
    puts("PASS native rider delta, stationary tick, island update and removed floor");
}

static void test_moving_sweep_and_resize(void)
{
    CollData rising_platform = sweep(15.0f, 40.0f, 40.0f);
    move_fixture(-5.0f, 35.0f, 45.0f);
    CHECK(mpColl_80044628_Floor(&rising_platform, NULL, NULL, 0));
    CHECK(rising_platform.floor.index == 1);
    near_value(rising_platform.contact.y, 45.0f);

    Vec3 rider = { 15.0f, 45.0f, 0.0f };
    Vec3 velocity;
    move_fixture(-5.0f, 75.0f, 45.0f);
    CHECK(mpGetSpeed(1, &rider, &velocity));
    near_value(velocity.x, 20.0f);
    near_value(velocity.y, 0.0f);

    const size_t allocations_before_updates = allocation_count;
    for (int i = 0; i < 100; ++i) {
        move_fixture(300.0f + i, 380.0f + i, 80.0f);
    }
    CHECK(allocation_count == allocations_before_updates);
    CollData moved = sweep(430.0f, 85.0f, 75.0f);
    CHECK(mpColl_80044628_Floor(&moved, NULL, NULL, 0));
    CHECK(moved.floor.index == 1);
    CollData old_position = sweep(15.0f, 50.0f, 40.0f);
    CHECK(!mpColl_80044628_Floor(&old_position, NULL, NULL, 0));
    puts("PASS native moving sweep, width resize, broadphase and island allocation reuse");
}

static float world_x(int pixel)
{
    return (pixel - 0.5f * supplied_geometry.width) *
           240.0f / supplied_geometry.height;
}

static float world_y(int pixel)
{
    return (supplied_geometry.platform_top - pixel) *
           240.0f / supplied_geometry.height;
}

static AuroraDesktopWindowEdge window_edge(uint64_t id, int left, int right,
                                            int top)
{
    AuroraDesktopWindowEdge edge = {
        .window_id = id, .span_index = 0,
        .left = left, .right = right, .top = top,
        .window_left = left, .window_right = right,
        .window_top = top, .window_bottom = top + 300,
    };
    return edge;
}

static AuroraDesktopWallpaperEdge wallpaper_edge(uint64_t generation,
    uint32_t id, int left, int right, int top, int right_top, uint32_t contour)
{
    return (AuroraDesktopWallpaperEdge) {
        .generation = generation, .edge_id = id,
        .left = left, .right = right, .top = top, .right_top = right_top,
        .contour_id = contour,
        .source_left = left, .source_right = right,
        .source_top = top, .source_right_top = right_top,
    };
}

static int window_line_at(int pixel_x, int top)
{
    CollData probe = sweep(world_x(pixel_x), world_y(top) + 1.0f,
                           world_y(top) - 1.0f);
    if (!mpColl_80044628_Floor(&probe, NULL, NULL, 0)) {
        return -1;
    }
    CHECK(probe.floor.index >= 2);
    CHECK(mpColl_IsOnPlatform(&probe));
    near_value(probe.contact.y, world_y(top));
    return probe.floor.index;
}

static void begin_platform_fixture(bool use_windows, bool use_wallpaper)
{
    StartMeleeData start = { 0 };
    CHECK(setenv("MELEE_DESKTOP", "1", 1) == 0);
    CHECK(setenv("MELEE_DESKTOP_WINDOWS", use_windows ? "1" : "0", 1) == 0);
    CHECK(setenv("MELEE_DESKTOP_WALLPAPER", use_wallpaper ? "1" : "0", 1) == 0);
    stage_info.grkind = Gr_Kind_Last;
    start.rules.stkind = St_Kind_Last;
    memset(fixture_heads, 0, sizeof(fixture_heads));
    pc_desktop_begin_match(&start);
    load_fixture();
    pc_desktop_stage_ready();
    CHECK(pc_desktop_collision_fixed());
    CHECK(mpLib_8004D164() != &map);
    const int total = AURORA_DESKTOP_MAX_WINDOW_EDGES +
                      AURORA_DESKTOP_MAX_WALLPAPER_EDGES;
    CHECK(mpLib_8004D164()->line_count == map.line_count + total);
    CHECK(mpLib_8004D164()->vert_count == map.vert_count + 2 * total);
    CHECK(mpLib_8004D164()->joint_count == map.joint_count + total);
}

static void begin_window_fixture(void)
{
    begin_platform_fixture(true, false);
}

static void check_no_disabled_window_islands(void)
{
    unsigned visited = 0;
    for (mpIsland* island = mpIsland_80458E88.floors; island != NULL;
         island = island->next) {
        CHECK(++visited < 1000);
        if (island->line0 >= 2) {
            CHECK(mpLib_80054ED8(island->line0));
            CHECK(!(island->flags & mpIsland_B1));
        }
    }
}

static void test_production_window_motion(void)
{
    supplied_edge_count = 1;
    supplied_edges[0] = window_edge(100, 400, 700, 500);
    begin_window_fixture();
    int line = window_line_at(550, 500);
    CHECK(line >= 2);
    Vec3 rider = { world_x(550), world_y(500), 0.0f };
    Vec3 velocity;
    CHECK(mpGetSpeed(line, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);

    /* Translate the entire window, then feed the same cached OS snapshot on
     * the next simulation tick. Carry must happen once, not at every tick
     * until the next 15 Hz enumeration refresh. */
    supplied_edges[0] = window_edge(100, 500, 800, 455);
    pc_desktop_update_platforms();
    CHECK(window_line_at(650, 455) == line);
    CHECK(mpGetSpeed(line, &rider, &velocity));
    near_value(velocity.x, 100.0f * 240.0f / 1080.0f);
    near_value(velocity.y, 45.0f * 240.0f / 1080.0f);
    rider.x += velocity.x;
    rider.y += velocity.y;
    CollData standing = sweep(rider.x, rider.y, rider.y);
    standing.floor.index = line;
    CHECK(mpColl_800488F4(&standing));
    pc_desktop_update_platforms();
    CHECK(mpGetSpeed(line, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);

    /* Width changes alter the floor extent without stretching a rider. */
    supplied_edges[0].right = supplied_edges[0].window_right = 950;
    pc_desktop_update_platforms();
    CHECK(window_line_at(650, 455) == line);
    CHECK(mpGetSpeed(line, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);
    CHECK(window_line_at(900, 455) == line);

    /* A new covering window removes only the left part of this top edge.
     * The underlying window did not move; its rider must not move either. */
    supplied_edges[0].left = 600;
    supplied_edges[0].span_index = 1;
    pc_desktop_update_platforms();
    CHECK(window_line_at(650, 455) == line);
    CHECK(window_line_at(550, 455) == -1);
    CHECK(mpGetSpeed(line, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);

    /* Snapshot ordering and fragment ordinal are not collision identities. */
    supplied_edges[1] = supplied_edges[0];
    supplied_edges[0] = window_edge(200, 1100, 1400, 650);
    supplied_edge_count = 2;
    pc_desktop_update_platforms();
    CHECK(window_line_at(650, 455) == line);
    CHECK(window_line_at(1250, 650) != line);
    check_no_disabled_window_islands();

    AuroraDesktopWindowEdge fragment = supplied_edges[1];
    supplied_edges[1].right = 720;
    supplied_edges[1].span_index = 0;
    supplied_edges[2] = fragment;
    supplied_edges[2].left = 800;
    supplied_edges[2].span_index = 1;
    supplied_edge_count = 3;
    pc_desktop_update_platforms();
    CHECK(window_line_at(650, 455) >= 2);
    CHECK(window_line_at(850, 455) >= 2);
    CHECK(window_line_at(750, 455) == -1);
    supplied_edges[1] = fragment;
    supplied_edges[1].span_index = 0;
    supplied_edge_count = 2;
    pc_desktop_update_platforms();
    CHECK(window_line_at(750, 455) >= 2);
    check_no_disabled_window_islands();
    puts("PASS production snapshot translation, stationary carry, resize, clipping and reorder");
}

static void test_production_reference_quarantine(void)
{
    static Fighter fighter;
    static Item item;
    static HSD_GObj fighter_object, item_object;
    supplied_edge_count = 1;
    supplied_edges[0] = window_edge(300, 400, 700, 500);
    begin_window_fixture();
    const int fighter_line = window_line_at(550, 500);
    supplied_edges[1] = window_edge(301, 1000, 1300, 650);
    supplied_edge_count = 2;
    pc_desktop_update_platforms();
    const int item_line = window_line_at(1150, 650);
    CHECK(item_line != fighter_line);
    memset(&fighter, 0, sizeof(fighter));
    memset(&item, 0, sizeof(item));
    memset(&fighter_object, 0, sizeof(fighter_object));
    memset(&item_object, 0, sizeof(item_object));
    fighter.coll_data.floor.index = fighter_line;
    fighter.ground_or_air = GA_Ground;
    item.x378_itemColl.floor.index = item_line;
    fighter_object.user_data = &fighter;
    item_object.user_data = &item;
    fixture_heads[HSD_GOBJ_PLINK_FIGHTER] = &fighter_object;
    fixture_heads[HSD_GOBJ_PLINK_ITEM] = &item_object;

    supplied_edge_count = 0; /* Closed, minimized, hidden or fully covered. */
    pc_desktop_update_platforms();
    CHECK(!mpLib_80054ED8(fighter_line));
    CHECK(!mpLib_80054ED8(item_line));
    CHECK(!mpColl_800488F4(&fighter.coll_data));
    CHECK(mpIsland_8005AB54(fighter_line) == NULL);
    CHECK(mpIsland_8005AB54(item_line) == NULL);

    /* Present a full replacement snapshot while actors retain their old
     * floor for many ticks, as can occur with frozen/hitlagged actors. */
    supplied_edge_count = AURORA_DESKTOP_MAX_WINDOW_EDGES;
    for (unsigned i = 0; i < supplied_edge_count; ++i) {
        supplied_edges[i] = window_edge(1000 + i, 100, 300, 100 + 20 * i);
    }
    for (int tick = 0; tick < 8; ++tick) {
        pc_desktop_update_platforms();
        CHECK(!mpLib_80054ED8(fighter_line));
        CHECK(!mpLib_80054ED8(item_line));
        CHECK(!mpColl_800488F4(&fighter.coll_data));
    }
    unsigned enabled = 0;
    for (int i = 2; i < 2 + AURORA_DESKTOP_MAX_WINDOW_EDGES; ++i) {
        enabled += mpLib_80054ED8(i);
    }
    CHECK(enabled == AURORA_DESKTOP_MAX_WINDOW_EDGES - 2);
    check_no_disabled_window_islands();

    fighter.coll_data.floor.index = -1;
    item.x378_itemColl.floor.index = -1;
    pc_desktop_update_platforms();
    CHECK(mpLib_80054ED8(fighter_line));
    CHECK(mpLib_80054ED8(item_line));
    check_no_disabled_window_islands();
    memset(fixture_heads, 0, sizeof(fixture_heads));
    puts("PASS production removal, frozen fighter/item references, bounded pool and safe reuse");
}

static void test_production_disable_and_scene_reset(void)
{
    CHECK(setenv("MELEE_DESKTOP_WINDOWS", "0", 1) == 0);
    pc_desktop_update_platforms();
    for (int i = 2; i < mpLib_8004D164()->line_count; ++i) {
        CHECK(!mpLib_80054ED8(i));
    }
    CHECK(mpLib_80054ED8(0));
    check_no_disabled_window_islands();

    /* Disabling both optional sources still leaves the taskbar mode intact. */
    StartMeleeData start = { 0 };
    start.rules.stkind = St_Kind_Last;
    pc_desktop_begin_match(&start);
    load_fixture();
    CHECK(pc_desktop_collision_fixed());
    CHECK(mpLib_8004D164() == &map);
    CHECK(mpLib_8004D164()->line_count == 2);
    near_value(mpGetGroundCollVtx()[0].pos.x, world_x(0));
    pc_desktop_update_platforms();
    CHECK(mpLib_8004D164()->line_count == 2);

    /* A normal next scene must see authored data, never the private clone. */
    pc_desktop_end_match();
    CHECK(unsetenv("MELEE_DESKTOP") == 0);
    load_fixture();
    CHECK(!pc_desktop_collision_fixed());
    CHECK(mpLib_8004D164() == &map);
    CHECK(mpLib_8004D164()->line_count == 2);
    near_value(mpGetGroundCollVtx()[0].pos.x, -100.0f);
    near_value(mpGetGroundCollVtx()[1].pos.x, 100.0f);
    pc_desktop_update_platforms();
    near_value(mpGetGroundCollVtx()[0].pos.x, -100.0f);
    puts("PASS production toggle, taskbar-only mode, source preservation and scene reset");
}

static void test_wallpaper_generation_and_sources(void)
{
    static Fighter fighter;
    static HSD_GObj fighter_object;
    const int wallpaper_start = 2 + AURORA_DESKTOP_MAX_WINDOW_EDGES;
    supplied_edge_count = 1;
    supplied_edges[0] = window_edge(1, 400, 700, 500);
    supplied_wallpaper_count = 1;
    supplied_wallpaper[0] = wallpaper_edge(10, 0, 1100, 1400, 600, 600, 0);
    begin_platform_fixture(true, true);
    const int window_line = window_line_at(550, 500);
    const int wallpaper_line = window_line_at(1250, 600);
    CHECK(window_line >= 2 && window_line < wallpaper_start);
    CHECK(wallpaper_line >= wallpaper_start);
    CHECK(window_line != wallpaper_line);
    Vec3 rider = { world_x(1250), world_y(600), 0.0f };
    Vec3 velocity;
    CHECK(mpGetSpeed(wallpaper_line, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);
    pc_desktop_update_platforms();
    CHECK(window_line_at(1250, 600) == wallpaper_line);
    CHECK(window_line_at(550, 500) == window_line);

    memset(&fighter, 0, sizeof(fighter));
    memset(&fighter_object, 0, sizeof(fighter_object));
    fighter_object.user_data = &fighter;
    fighter.coll_data.floor.index = wallpaper_line;
    fixture_heads[HSD_GOBJ_PLINK_FIGHTER] = &fighter_object;
    supplied_wallpaper[0].generation = 11;
    supplied_wallpaper[0].top = 620;
    supplied_wallpaper[0].right_top = 620;
    supplied_wallpaper[0].source_top = 620;
    supplied_wallpaper[0].source_right_top = 620;
    pc_desktop_update_platforms();
    const int replacement = window_line_at(1250, 620);
    CHECK(replacement >= wallpaper_start && replacement != wallpaper_line);
    CHECK(!mpLib_80054ED8(wallpaper_line));
    CHECK(!mpGetSpeed(wallpaper_line, &rider, &velocity));
    CHECK(!mpColl_800488F4(&fighter.coll_data));
    CHECK(mpGetSpeed(replacement, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);
    CHECK(window_line_at(550, 500) == window_line);
    check_no_disabled_window_islands();

    /* Covering app windows can clip/split a wallpaper ledge without changing
     * the image generation. Those horizontal fragments never carry actors. */
    fighter.coll_data.floor.index = replacement;
    supplied_wallpaper[0].left = 1150;
    pc_desktop_update_platforms();
    CHECK(window_line_at(1250, 620) == replacement);
    CHECK(mpGetSpeed(replacement, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);
    supplied_wallpaper[1] = supplied_wallpaper[0];
    supplied_wallpaper[0].right = 1250;
    supplied_wallpaper[1].left = 1300;
    supplied_wallpaper_count = 2;
    pc_desktop_update_platforms();
    CHECK(window_line_at(1175, 620) >= wallpaper_start);
    CHECK(window_line_at(1350, 620) >= wallpaper_start);
    CHECK(window_line_at(1275, 620) == -1);
    supplied_wallpaper[0].right = 1400;
    supplied_wallpaper_count = 1;
    pc_desktop_update_platforms();
    const int merged = window_line_at(1275, 620);
    CHECK(merged >= wallpaper_start);
    CHECK(mpGetSpeed(merged, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);

    /* An inconsistent producer that changes the ledge height without a new
     * generation must still retire the old floor, never move its rider. */
    fighter.coll_data.floor.index = merged;
    supplied_wallpaper[0].top = 630;
    supplied_wallpaper[0].right_top = 630;
    supplied_wallpaper[0].source_top = 630;
    supplied_wallpaper[0].source_right_top = 630;
    pc_desktop_update_platforms();
    const int changed = window_line_at(1250, 630);
    CHECK(changed >= wallpaper_start && changed != merged);
    CHECK(!mpLib_80054ED8(merged));
    CHECK(mpGetSpeed(changed, &rider, &velocity));
    near_value(velocity.x, 0.0f);
    near_value(velocity.y, 0.0f);
    check_no_disabled_window_islands();

    supplied_wallpaper_count = 0;
    pc_desktop_update_platforms();
    CHECK(!mpLib_80054ED8(changed));
    CHECK(window_line_at(550, 500) == window_line);
    check_no_disabled_window_islands();
    memset(fixture_heads, 0, sizeof(fixture_heads));
    puts("PASS wallpaper identity, generation replacement, split/merge, zero carry and removal");
}

static void test_wallpaper_capacity_and_option(void)
{
    static Fighter fighter;
    static Item item;
    static HSD_GObj fighter_object, item_object;
    const int wallpaper_start = 2 + AURORA_DESKTOP_MAX_WINDOW_EDGES;
    supplied_edge_count = AURORA_DESKTOP_MAX_WINDOW_EDGES;
    supplied_wallpaper_count = AURORA_DESKTOP_MAX_WALLPAPER_EDGES;
    for (unsigned i = 0; i < supplied_edge_count; ++i) {
        supplied_edges[i] = window_edge(2000 + i, 100, 400, 100 + 20 * i);
    }
    for (unsigned i = 0; i < supplied_wallpaper_count; ++i) {
        supplied_wallpaper[i] = wallpaper_edge(20, i, 1100, 1400,
            100 + 20 * (int) i, 100 + 20 * (int) i, 0);
    }
    begin_platform_fixture(true, true);
    for (int i = 2; i < mpLib_8004D164()->line_count; ++i) {
        CHECK(mpLib_80054ED8(i));
    }
    memset(&fighter, 0, sizeof(fighter));
    memset(&item, 0, sizeof(item));
    memset(&fighter_object, 0, sizeof(fighter_object));
    memset(&item_object, 0, sizeof(item_object));
    fighter.coll_data.floor.index = wallpaper_start;
    item.x378_itemColl.floor.index = wallpaper_start + 1;
    fighter_object.user_data = &fighter;
    item_object.user_data = &item;
    fixture_heads[HSD_GOBJ_PLINK_FIGHTER] = &fighter_object;
    fixture_heads[HSD_GOBJ_PLINK_ITEM] = &item_object;
    for (unsigned i = 0; i < supplied_wallpaper_count; ++i) {
        supplied_wallpaper[i].generation = 21;
    }
    for (int tick = 0; tick < 6; ++tick) {
        pc_desktop_update_platforms();
        CHECK(!mpLib_80054ED8(wallpaper_start));
        CHECK(!mpLib_80054ED8(wallpaper_start + 1));
        for (int i = 2; i < wallpaper_start; ++i) {
            CHECK(mpLib_80054ED8(i));
        }
    }
    unsigned active = 0;
    for (int i = wallpaper_start; i < mpLib_8004D164()->line_count; ++i) {
        active += mpLib_80054ED8(i);
    }
    CHECK(active == AURORA_DESKTOP_MAX_WALLPAPER_EDGES - 2);
    check_no_disabled_window_islands();
    fighter.coll_data.floor.index = -1;
    item.x378_itemColl.floor.index = -1;
    pc_desktop_update_platforms();
    CHECK(mpLib_80054ED8(wallpaper_start));
    CHECK(mpLib_80054ED8(wallpaper_start + 1));
    CHECK(setenv("MELEE_DESKTOP_WALLPAPER", "0", 1) == 0);
    pc_desktop_update_platforms();
    for (int i = wallpaper_start; i < mpLib_8004D164()->line_count; ++i) {
        CHECK(!mpLib_80054ED8(i));
    }
    for (int i = 2; i < wallpaper_start; ++i) {
        CHECK(mpLib_80054ED8(i));
    }
    check_no_disabled_window_islands();
    memset(fixture_heads, 0, sizeof(fixture_heads));

    /* Wallpaper can be enabled independently of open-window platforms. */
    begin_platform_fixture(false, true);
    CHECK(window_line_at(250, 100) == -1);
    CHECK(window_line_at(1250, 100) >= wallpaper_start);
    check_no_disabled_window_islands();
    puts("PASS separate 32+32 capacity, wallpaper actor quarantine and independent options");
}

static unsigned wallpaper_island_count(void)
{
    unsigned result = 0, visited = 0;
    for (mpIsland* island = mpIsland_80458E88.floors; island != NULL;
         island = island->next) {
        CHECK(++visited < 1000);
        if (island->line0 >= 2 + AURORA_DESKTOP_MAX_WINDOW_EDGES) {
            ++result;
        }
    }
    return result;
}

static int hill_top(int x)
{
    if (x <= 600) return 800 - (x - 200) * 160 / 400;
    if (x <= 1000) return 640 + (x - 600) * 40 / 400;
    return 680 + (x - 1000) * 80 / 500;
}

static void test_wallpaper_connected_slopes(void)
{
    supplied_edge_count = 0;
    supplied_wallpaper_count = 5;
    supplied_wallpaper[0] = wallpaper_edge(30, 0, 200, 600, 800, 640, 42);
    supplied_wallpaper[1] = wallpaper_edge(30, 1, 600, 1000, 640, 680, 42);
    supplied_wallpaper[2] = wallpaper_edge(30, 2, 1000, 1500, 680, 760, 42);
    supplied_wallpaper[3] = wallpaper_edge(30, 3, 1500, 1700, 760, 760, 0);
    supplied_wallpaper[4] = wallpaper_edge(30, 4, 0, 200, 800, 800, 43);
    begin_platform_fixture(false, true);
    const int a = window_line_at(400, 720);
    const int b = window_line_at(800, 660);
    const int c = window_line_at(1250, 720);
    const int separate = window_line_at(1600, 760);
    CHECK(a >= 0 && b >= 0 && c >= 0 && separate >= 0);
    CHECK(mpLineGetPrev(a) == -1 && mpLineGetNext(a) == b);
    CHECK(mpLineGetPrev(b) == a && mpLineGetNext(b) == c);
    CHECK(mpLineGetPrev(c) == b && mpLineGetNext(c) == -1);
    CHECK(mpLineGetPrev(separate) == -1);
    CHECK(mpIsland_8005AB54(a) == mpIsland_8005AB54(b));
    CHECK(mpIsland_8005AB54(a) == mpIsland_8005AB54(c));
    CHECK(wallpaper_island_count() == 3);
    CHECK(mpLinesConnected(a, c));
    CHECK(mpLinesConnected(c, a));
    CHECK(!mpLinesConnected(c, separate));
    /* Exercise both native link encodings. The alternate links use runtime
     * flags and endpoint proximity, which must use the x64 CollLine stride. */
    CollLine* runtime_lines = mpGetGroundCollLine();
    runtime_lines[a].inner->next_id0 = -1;
    runtime_lines[a].inner->next_id1 = b;
    runtime_lines[b].inner->prev_id0 = -1;
    runtime_lines[b].inner->prev_id1 = a;
    CHECK(mpLinesConnected(a, c) && mpLinesConnected(c, a));
    runtime_lines[b].flags &= ~LINE_FLAG_ENABLED;
    CHECK(!mpLinesConnected(a, c));
    runtime_lines[b].flags |= LINE_FLAG_ENABLED;
    runtime_lines[a].inner->next_id0 = b;
    runtime_lines[a].inner->next_id1 = -1;
    runtime_lines[b].inner->prev_id0 = a;
    runtime_lines[b].inner->prev_id1 = -1;

    Vec3 normal;
    mpLineGetNormal(a, &normal);
    near_value(normal.x, -160.0f / sqrtf(400.0f * 400.0f + 160.0f * 160.0f));
    near_value(normal.y, 400.0f / sqrtf(400.0f * 400.0f + 160.0f * 160.0f));
    CollData walker = sweep(world_x(400), world_y(720), world_y(720));
    walker.floor.index = a;
    const int path[] = { 400, 550, 600, 650, 800, 950, 1000, 1250, 1450,
                        1250, 1000, 950, 800, 650, 600, 550, 400 };
    for (unsigned i = 0; i < sizeof(path) / sizeof(path[0]); ++i) {
        walker.prev_pos = walker.cur_pos;
        walker.cur_pos.x = world_x(path[i]);
        CHECK(mpColl_800488F4(&walker));
        near_value(walker.cur_pos.y, world_y(hill_top(path[i])) + 0.0001f);
        CHECK(walker.floor.normal.y > 0.9f);
    }

    CollData upward = sweep(world_x(400), world_y(720) - 10, world_y(720) + 10);
    CHECK(!mpColl_80044628_Floor(&upward, NULL, NULL, 0));
    CollData dropping = sweep(world_x(400), world_y(720) + 1, world_y(720) - 2);
    dropping.floor.index = a;
    mpUpdateFloorSkip(&dropping);
    CHECK(!mpColl_80044628_Floor(&dropping, NULL, NULL, 0));
    CollData crossing_drop = sweep(world_x(601), world_y(640) + 1, world_y(640) - 2);
    crossing_drop.prev_pos.x = world_x(599);
    crossing_drop.floor.index = a;
    mpUpdateFloorSkip(&crossing_drop);
    CHECK(!mpColl_80044628_Floor(&crossing_drop, NULL, NULL, 0));
    crossing_drop.x38 = mpColl_804D64AC - 1;
    CHECK(!mpColl_80044628_Floor(&crossing_drop, NULL, NULL, 0));
    CollData other_contour = sweep(world_x(1600), world_y(760) + 1, world_y(760) - 2);
    other_contour.floor_skip = c;
    CHECK(mpColl_80044628_Floor(&other_contour, NULL, NULL, 0));
    CHECK(other_contour.floor.index == separate);
    other_contour = sweep(world_x(100), world_y(800) + 1, world_y(800) - 2);
    other_contour.floor_skip = a;
    CHECK(mpColl_80044628_Floor(&other_contour, NULL, NULL, 0));
    CHECK(other_contour.floor.index != a);
    check_no_disabled_window_islands();
    puts("PASS connected sloped terrain, native normals, two-way grounded traversal and CPU islands");

    /* Window occlusion removes the actual visible part of a hill, including
     * its joins. The remaining fragments retain identity without stretching
     * or carrying their occupants. */
    supplied_wallpaper[0].right = 550;
    supplied_wallpaper[0].right_top = 660;
    supplied_wallpaper[1].left = 650;
    supplied_wallpaper[1].top = 645;
    pc_desktop_update_platforms();
    CHECK(window_line_at(400, 720) == a && window_line_at(800, 660) == b);
    CHECK(mpLineGetNext(a) == -1 && mpLineGetPrev(b) == -1);
    CHECK(mpLineGetNext(b) == c && mpLineGetPrev(c) == b);
    CHECK(!mpLinesConnected(a, c));
    CHECK(window_line_at(600, 640) == -1);
    CHECK(wallpaper_island_count() == 4);
    CollData gap_drop = sweep(world_x(800), world_y(660) + 1, world_y(660) - 2);
    gap_drop.floor_skip = a;
    CHECK(mpColl_80044628_Floor(&gap_drop, NULL, NULL, 0));
    CHECK(gap_drop.floor.index == b);
    gap_drop.x38 = mpColl_804D64AC - 1;
    CHECK(mpColl_80044628_Floor(&gap_drop, NULL, NULL, 0));
    CollData lost_floor = sweep(world_x(600), world_y(640), world_y(640));
    lost_floor.floor.index = a;
    CHECK(!mpColl_800488F4(&lost_floor));
    Vec3 rider = { world_x(400), world_y(720), 0 };
    Vec3 velocity;
    CHECK(mpGetSpeed(a, &rider, &velocity));
    near_value(velocity.x, 0); near_value(velocity.y, 0);

    supplied_wallpaper[0].right = 600;
    supplied_wallpaper[0].right_top = 640;
    supplied_wallpaper[1].left = 600;
    supplied_wallpaper[1].top = 640;
    AuroraDesktopWallpaperEdge temporary = supplied_wallpaper[0];
    supplied_wallpaper[0] = supplied_wallpaper[2];
    supplied_wallpaper[2] = temporary;
    pc_desktop_update_platforms();
    CHECK(window_line_at(400, 720) == a && window_line_at(800, 660) == b);
    CHECK(mpLineGetNext(a) == b && mpLineGetNext(b) == c);
    CHECK(mpLinesConnected(a, c) && wallpaper_island_count() == 3);
    check_no_disabled_window_islands();

    supplied_wallpaper[1] = supplied_wallpaper[4];
    supplied_wallpaper_count = 4;
    pc_desktop_update_platforms();
    CHECK(!mpLib_80054ED8(b));
    CHECK(mpLineGetNext(a) == -1 && mpLineGetPrev(c) == -1);
    CHECK(mpLineGetPrev(b) == -1 && mpLineGetNext(b) == -1);
    CHECK(wallpaper_island_count() == 4);
    CHECK(mpIsland_8005AB54(b) == NULL);
    check_no_disabled_window_islands();
    puts("PASS slope occlusion gaps, isolated drop-through, zero carry, reorder, rejoin and retired links");
}

static void test_wallpaper_fractional_clipping(void)
{
    static Fighter fighter;
    static HSD_GObj fighter_object;
    supplied_edge_count = 0;
    supplied_wallpaper_count = 1;
    supplied_wallpaper[0] = wallpaper_edge(40, 0, 200, 1400, 800, 680, 9);
    begin_platform_fixture(false, true);
    const int original = window_line_at(800, 740);
    CHECK(original >= 0);
    Vec3 rider = { world_x(800), world_y(740), 0 };
    Vec3 velocity;
    supplied_wallpaper[0].left = 201;
    supplied_wallpaper[0].right = 1399;
    /* Physical-pixel clipping rounds 799.9 and 680.1. Native geometry still
     * evaluates the original source line and retains its exact gradient. */
    supplied_wallpaper[0].top = 800;
    supplied_wallpaper[0].right_top = 680;
    for (int tick = 0; tick < 5; ++tick) {
        pc_desktop_update_platforms();
        CHECK(window_line_at(800, 740) == original);
        CHECK(mpGetSpeed(original, &rider, &velocity));
        near_value(velocity.x, 0); near_value(velocity.y, 0);
        Vec3 endpoint;
        mpLineGetV0Pos(original, &endpoint);
        near_value(endpoint.y, world_y(800) + 0.1f * 240.0f / 1080.0f);
        mpLineGetV1Pos(original, &endpoint);
        near_value(endpoint.y, world_y(680) - 0.1f * 240.0f / 1080.0f);
    }

    memset(&fighter, 0, sizeof(fighter));
    memset(&fighter_object, 0, sizeof(fighter_object));
    fighter.coll_data.floor.index = original;
    fighter_object.user_data = &fighter;
    fixture_heads[HSD_GOBJ_PLINK_FIGHTER] = &fighter_object;
    supplied_wallpaper[0] = wallpaper_edge(40, 0, 200, 1400, 800, 700, 9);
    pc_desktop_update_platforms();
    const int tilted = window_line_at(800, 750);
    CHECK(tilted >= 0 && tilted != original);
    CHECK(!mpLib_80054ED8(original));
    CHECK(!mpGetSpeed(original, &rider, &velocity));
    CHECK(mpLineGetPrev(original) == -1 && mpLineGetNext(original) == -1);
    CHECK(mpGetSpeed(tilted, &rider, &velocity));
    near_value(velocity.x, 0); near_value(velocity.y, 0);
    CHECK(wallpaper_island_count() == 1);

    /* Reject a corrupt snapshot whose visible endpoint no longer lies on
     * its declared immutable source; no out-of-bounds/ghost floor appears. */
    supplied_wallpaper[0].top = 700;
    pc_desktop_update_platforms();
    CHECK(!mpLib_80054ED8(tilted));
    CHECK(wallpaper_island_count() == 0);
    check_no_disabled_window_islands();
    memset(fixture_heads, 0, sizeof(fixture_heads));
    puts("PASS fractional clipping without slope drift, immutable source replacement and invalid input");
}

int main(void)
{
    CHECK(setvbuf(stdout, NULL, _IONBF, 0) == 0);
    CHECK(unsetenv("MELEE_DESKTOP") == 0);
    load_fixture();
    test_one_way_and_drop_through();
    test_motion_and_removal();
    test_moving_sweep_and_resize();
    test_production_window_motion();
    test_production_reference_quarantine();
    test_production_disable_and_scene_reset();
    test_wallpaper_generation_and_sources();
    test_wallpaper_capacity_and_option();
    test_wallpaper_connected_slopes();
    test_wallpaper_fractional_clipping();
    pc_desktop_end_match();
    for (size_t i = 0; i < allocation_count; ++i) {
        free(allocations[i]);
    }
    puts("Desktop collision tests passed.");
    return 0;
}
