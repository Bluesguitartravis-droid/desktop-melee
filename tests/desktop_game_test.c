/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ROM-free integration tests for desktop geometry and actual game AI code.
 * Real CPU initialization, target/ally filtering, match defaults, costume
 * tables, scene rule getters and blastzone getters are linked below. Only
 * hardware, graphics, archive-dependent attack setup and living-fighter/
 * collision fixture boundaries are stubbed; no AI selection is simulated.
 * Run with tools/test_desktop_game.sh (native GCC, no Dawn or game assets). */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aurora/desktop.h>
#include <pc/desktop_game.h>
#include <pc/widescreen.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/forward.h>
#include <melee/ft/ft_0877.h>
#include <melee/ft/ftcpuattack.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/kinds/ftCommon/ftCo_0A01.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/types.h>
#include <melee/gr/grbigblue.h>
#include <melee/gr/grcorneria.h>
#include <melee/gr/grinishie1.h>
#include <melee/gr/grvenom.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/lb/lb_00B0.h>
#include <melee/mn/types.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <melee/pl/forward.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>

/* libc assert.h declares a different __assert than the GameCube headers. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        abort(); \
    } \
} while (0)

StageInfo stage_info;
static AuroraDesktopGeometry supplied_geometry;
static bool geometry_available = true;
static CollVtx vertices[4];
static CollLine lines[2];
static CollJoint joints[1];
static MapCollData disc_collision;
static MapLine disc_lines[2];
static MapJoint disc_joint;
static float ground_scale;
static Vec3 camera_eye, camera_interest;
static int stage_visible;
static float quake_scale;
static HSD_GObj* fixture_heads[32];
HSD_GObj** HSD_GObjPLinkHead = fixture_heads;
static Fighter fighters[4];
static HSD_GObj fighter_objects[4];

bool aurora_desktop_get_geometry(AuroraDesktopGeometry* out)
{
    *out = supplied_geometry;
    return geometry_available;
}
void pc_log_line(const char* fmt, ...) { (void) fmt; }
CollVtx* mpGetGroundCollVtx(void) { return vertices; }
CollLine* mpGetGroundCollLine(void) { return lines; }
CollJoint* mpGetGroundCollJoint(void) { return joints; }
float Ground_801C0498(void) { return ground_scale; }
void Camera_SetStageVisible(int visible) { stage_visible = visible; }
void Camera_SetQuakeScale(float scale) { quake_scale = scale; }

void memzero(void* mem, ssize_t size)
{
    memset(mem, 0, (size_t) size);
}
float HSD_Randf(void) { return 0.5f; }
void ftCo_800B9704(Fighter* fighter) { (void) fighter; }
bool ft_80087A18(Fighter_GObj* fighter) { (void) fighter; return false; }
int ft_80087A80(Fighter_GObj* fighter) { (void) fighter; return 0; }
bool ftLib_IsDead(HSD_GObj* fighter) { (void) fighter; return false; }
bool grBigBlue_801EF844(enum_t line) { (void) line; return false; }
bool grInishie1_801FCAAC(int line) { (void) line; return false; }
bool grCorneria_801E2D90(enum_t line) { (void) line; return false; }
s32 grVenom_80206D10(s32 line) { (void) line; return false; }
bool mpCheckFloor(float ax, float ay, float bx, float by, float y_offset,
                  Vec3* vec_out, int* line_id_out, u32* flags_out,
                  Vec3* normal_out, int line_id_skip, int joint_id_skip,
                  int joint_id_only, bool (*callback)(Fighter_GObj*, int),
                  Fighter_GObj* fighter)
{
    (void) ay; (void) bx; (void) by; (void) y_offset;
    (void) line_id_skip; (void) joint_id_skip; (void) joint_id_only;
    (void) callback; (void) fighter;
    *vec_out = (Vec3) { ax, 0, 0 };
    *line_id_out = 0;
    *flags_out = 0;
    *normal_out = (Vec3) { 0, 1, 0 };
    return true;
}

void HSD_CObjSetOrtho(HSD_CObj* camera, float top, float bottom, float left,
                     float right)
{
    camera->projection_type = PROJ_ORTHO;
    camera->projection_param.ortho.top = top;
    camera->projection_param.ortho.bottom = bottom;
    camera->projection_param.ortho.left = left;
    camera->projection_param.ortho.right = right;
}
int HSD_CObjGetProjectionType(HSD_CObj* camera) { return camera->projection_type; }
void HSD_CObjGetOrtho(HSD_CObj* camera, float* top, float* bottom, float* left, float* right)
{
    *top = camera->projection_param.ortho.top;
    *bottom = camera->projection_param.ortho.bottom;
    *left = camera->projection_param.ortho.left;
    *right = camera->projection_param.ortho.right;
}
void HSD_CObjSetFrustum(HSD_CObj* camera, float top, float bottom, float left, float right)
{
    camera->projection_type = PROJ_FRUSTUM;
    camera->projection_param.frustum.top = top;
    camera->projection_param.frustum.bottom = bottom;
    camera->projection_param.frustum.left = left;
    camera->projection_param.frustum.right = right;
}
void HSD_CObjSetFov(HSD_CObj* camera, float fov) { camera->projection_param.perspective.fov = fov; }
void HSD_CObjSetFlags(HSD_CObj* camera, u32 flags) { camera->flags |= flags; }
void HSD_CObjSetInterest(HSD_CObj* camera, Vec3* interest)
{
    (void) camera;
    camera_interest = *interest;
}
void HSD_CObjSetEyePosition(HSD_CObj* camera, Vec3* eye)
{
    (void) camera;
    camera_eye = *eye;
}
void HSD_CObjSetNear(HSD_CObj* camera, float value) { camera->near = value; }
void HSD_CObjSetFar(HSD_CObj* camera, float value) { camera->far = value; }

static void near_value(float actual, float expected)
{
    if (fabsf(actual - expected) > 0.002f) {
        fprintf(stderr, "expected %.8g, got %.8g\n", expected, actual);
        abort();
    }
}

static void reset_fixture(float scale)
{
    static const float x[4] = { -85.5f, 85.5f, 50.0f, -50.0f };
    static const float y[4] = { 7.0f, 7.0f, -20.0f, -20.0f };
    memset(&stage_info, 0, sizeof(stage_info));
    memset(&disc_collision, 0, sizeof(disc_collision));
    memset(disc_lines, 0, sizeof(disc_lines));
    memset(&disc_joint, 0, sizeof(disc_joint));
    memset(joints, 0, sizeof(joints));
    ground_scale = scale;
    stage_info.grkind = Gr_Kind_Last;
    stage_visible = 1;
    quake_scale = 1.0f;
    disc_collision.vert_count = 4;
    disc_collision.line_count = 2;
    disc_collision.joint_count = 1;
    /* A nonzero range start detects accidental reads of disc-endian fields
     * as native integers, without needing a real archive. */
    disc_collision.ranges[MapLineGroup_Floor].start = 1;
    disc_collision.ranges[MapLineGroup_Floor].count = 1;
    disc_lines[0].v0_idx = 2;
    disc_lines[0].v1_idx = 3;
    disc_lines[1].v0_idx = 0;
    disc_lines[1].v1_idx = 1;
    lines[0].inner = &disc_lines[0];
    lines[1].inner = &disc_lines[1];
    joints[0].inner = &disc_joint;
    joints[0].bounding_min = (Vec2) { -85.5f * scale, -20.0f * scale };
    joints[0].bounding_max = (Vec2) { 85.5f * scale, 7.0f * scale };
    for (int i = 0; i < 4; ++i) {
        vertices[i].base_pos = (Vec2) { x[i], y[i] };
        vertices[i].pos = (Vec2) { x[i] * scale, y[i] * scale };
        vertices[i].prev_pos = vertices[i].pos;
    }
    /* The real MapCollData and MapLine declarations must store big endian. */
    const unsigned char* bytes = (const unsigned char*) &disc_collision;
    CHECK(bytes[4] == 0 && bytes[7] == 4);
    CHECK(bytes[0x10] == 0 && bytes[0x11] == 1);
    CHECK(bytes[0x12] == 0 && bytes[0x13] == 1);
    bytes = (const unsigned char*) &disc_lines[1];
    CHECK(bytes[2] == 0 && bytes[3] == 1);
}

static void init_cpu_fixture(void)
{
    StartMeleeData start = { 0 };
    setenv("MELEE_DESKTOP_WATCH", "1", 1);
    pc_desktop_configure_match(&start);
    unsetenv("MELEE_DESKTOP_WATCH");
    CHECK(start.rules.match_kind == MatchKind_Stock && start.rules.is_stock);
    CHECK(start.rules.is_vs && !start.rules.is_teams);
    gmVs_GetSceneController()->start = start.rules;
    memset(fighters, 0, sizeof(fighters));
    memset(fighter_objects, 0, sizeof(fighter_objects));
    memset(fixture_heads, 0, sizeof(fixture_heads));
    fixture_heads[HSD_GOBJ_PLINK_FIGHTER] = &fighter_objects[0];
    fighter_objects[0].next = &fighter_objects[1];
    for (int i = 0; i < 2; ++i) {
        Fighter* fighter = &fighters[i];
        CHECK(start.players[i].slot_type == Gm_PKind_Cpu);
        CHECK(start.players[i].cpu_kind == CpuKind_4);
        CHECK(start.players[i].cpu_level == 5 && start.players[i].stocks == 4);
        fighter->gobj = &fighter_objects[i];
        fighter_objects[i].user_data = fighter;
        fighter->player_idx = i;
        fighter->team = start.players[i].team;
        fighter->kind = i ? Ft_Kind_Falco : Ft_Kind_Fox;
        fighter->cur_pos = (Vec3) { i ? 40 : -40, 0, 0 };
        fighter->facing_dir = i ? -1 : 1;
        fighter->co_attrs.gravity = 0.13f;
        fighter->co_attrs.jump_v_initial_velocity = 3.68f;
        fighter->co_attrs.air_jump_v_multiplier = 1.0f;
        ftCo_800A101C(fighter, start.players[i].cpu_kind,
                      start.players[i].cpu_level, 0);
    }
}

static void check_cpu_targeting(bool desktop)
{
    StageBlastZone original_bounds = stage_info.blast_zone;
    init_cpu_fixture();
    for (int i = 0; i < 2; ++i) {
        Fighter* cpu = &fighters[i];
        Fighter* opponent = &fighters[1 - i];
        CHECK(cpu->cpu.kind == CpuKind_4);
        CHECK(!ftCo_IsAlly(cpu, opponent));
        CHECK(ftCo_800A4E8C(cpu, &cpu->cur_pos) == opponent);
        near_value(cpu->cpu.half_width, 10.0f);
        if (desktop) {
            float below = -Stage_GetBlastZoneBottomOffset();
            float above = Stage_GetBlastZoneTopOffset();
            CHECK(cpu->cpu.half_height > 0);
            CHECK(cpu->cpu.half_height <= 10.0f);
            CHECK(cpu->cpu.half_height < below);
            CHECK(cpu->cpu.half_height < above);
            if (below >= 20.0f && above >= 20.0f) {
                near_value(cpu->cpu.half_height, 10.0f);
            }
            if (below < 10.0f) {
                /* Reproduce preview2 with the real target filter: its
                 * original ten-unit margin excludes a grounded opponent. */
                cpu->cpu.half_height = 10.0f;
                CHECK(ftCo_800A4A40(cpu) == NULL);
                CHECK(ftCo_800A4E8C(cpu, &cpu->cur_pos) == NULL);
                /* The same initializer used on death/respawn must restore
                 * the corrected margin, not just initial match creation. */
                ftCo_800A101C(cpu, CpuKind_4, 5, 0);
                CHECK(cpu->cpu.half_height < below);
            }
        } else {
            near_value(cpu->cpu.half_height, 10.0f);
        }
        CHECK(!ftCo_IsAlly(cpu, opponent));
        CHECK(ftCo_800A4A40(cpu) == opponent);
        /* This actual nearest-enemy routine shares combat's bounds, ally,
         * invulnerability, sleeping and living-target filters. */
        CHECK(ftCo_800A4E8C(cpu, &cpu->cur_pos) == opponent);
        opponent->cur_pos.y = 60.0f; /* respawn platform height */
        CHECK(ftCo_800A4E8C(cpu, &cpu->cur_pos) == opponent);
        opponent->cur_pos.y = 0.0f;
        gmVs_GetSceneController()->start.is_teams = true;
        CHECK(ftCo_IsAlly(cpu, opponent));
        CHECK(ftCo_800A4E8C(cpu, &cpu->cur_pos) == NULL);
        gmVs_GetSceneController()->start.is_teams = false;
        CHECK(!ftCo_IsAlly(cpu, opponent));
        CHECK(ftCo_800A4E8C(cpu, &cpu->cur_pos) == opponent);
        opponent->is_sleeping = true;
        CHECK(ftCo_800A4E8C(cpu, &cpu->cur_pos) == NULL);
        opponent->is_sleeping = false;
    }
    CHECK(memcmp(&stage_info.blast_zone, &original_bounds,
                 sizeof(original_bounds)) == 0);
}

/* Independent orthographic world-to-physical-pixel projection. The game's
 * actual camera setters are inspected through their recorded arguments. */
static float pixel_x(const HSD_CObj* camera, float x)
{
    return (x - camera_eye.x - camera->projection_param.ortho.left) /
        (camera->projection_param.ortho.right -
         camera->projection_param.ortho.left) * supplied_geometry.width;
}
static float pixel_y(const HSD_CObj* camera, float y)
{
    return (camera->projection_param.ortho.top + camera_eye.y - y) /
        (camera->projection_param.ortho.top -
         camera->projection_param.ortho.bottom) * supplied_geometry.height;
}

/* Real game match setup, CPU target search, spawn and HUD regressions. */
static void check_four_player_targeting(void)
{
    StartMeleeData start = {0};
    float x[4], y, z;
    setenv("MELEE_DESKTOP_PLAYERS","4",1);
    setenv("MELEE_DESKTOP_WATCH","1",1);
    pc_desktop_configure_match(&start);
    CHECK(pc_desktop_player_count()==4);
    gmVs_GetSceneController()->start=start.rules;
    memset(fighters,0,sizeof(fighters));
    memset(fighter_objects,0,sizeof(fighter_objects));
    memset(fixture_heads,0,sizeof(fixture_heads));
    fixture_heads[HSD_GOBJ_PLINK_FIGHTER]=&fighter_objects[0];
    for (int i=0;i<4;++i) {
        Fighter* f=&fighters[i];
        CHECK(start.players[i].slot_type==Gm_PKind_Cpu);
        CHECK(pc_desktop_spawn_position(i,false,&x[i],&y,&z));
        CHECK(x[i]>vertices[0].pos.x && x[i]<vertices[1].pos.x);
        if (i) CHECK(x[i]>x[i-1]);
        near_value(y,0); near_value(z,0);
        float respawn;
        CHECK(pc_desktop_spawn_position(i,true,&respawn,&y,&z));
        near_value(respawn,x[i]); near_value(y,60);
        pc_desktop_hud_position(i,4,&respawn,&y);
        near_value(respawn,((float)i-1.5f)*18); near_value(y,16.5f);
        f->gobj=&fighter_objects[i]; fighter_objects[i].user_data=f;
        fighter_objects[i].next=i<3?&fighter_objects[i+1]:NULL;
        f->player_idx=i; f->team=start.players[i].team; f->kind=Ft_Kind_Fox;
        f->cur_pos=(Vec3){x[i],0,0}; f->facing_dir=i<2?1:-1;
        f->co_attrs.gravity=.13f; f->co_attrs.jump_v_initial_velocity=3.68f;
        f->co_attrs.air_jump_v_multiplier=1.0f;
        ftCo_800A101C(f,start.players[i].cpu_kind,start.players[i].cpu_level,0);
    }
    CHECK(!pc_desktop_spawn_position(4,false,&y,&y,&z));
    for (int i=0;i<4;++i) {
        Fighter* target=ftCo_800A4E8C(&fighters[i],&fighters[i].cur_pos);
        CHECK(target!=NULL && target!=&fighters[i]);
        CHECK(target>=fighters && target<fighters+4);
        // Reinitialization is the same path used on respawn.
        ftCo_800A101C(&fighters[i],CpuKind_4,5,0);
        CHECK(ftCo_800A4E8C(&fighters[i],&fighters[i].cur_pos)!=NULL);
    }
    setenv("MELEE_DESKTOP_PLAYERS","3",1);
    pc_desktop_configure_match(&start);
    CHECK(pc_desktop_player_count()==3);
    for (int i=0;i<3;++i) {
        CHECK(pc_desktop_spawn_position(i,false,&x[i],&y,&z));
        if (i) CHECK(x[i]>x[i-1]);
        pc_desktop_hud_position(i,3,&y,&z);
        near_value(y,((float)i-1)*18);
    }
    CHECK(start.players[3].slot_type==Gm_PKind_NA);
    unsetenv("MELEE_DESKTOP_PLAYERS");unsetenv("MELEE_DESKTOP_WATCH");
    pc_desktop_configure_match(&start);
    CHECK(pc_desktop_player_count()==2);
}

static void check_hud_camera_scale(void)
{
    HSD_CObj hud = {0};
    supplied_geometry = (AuroraDesktopGeometry) {0,0,5760,2160,0,2100,1920,2160};
    geometry_available = true;
    reset_fixture(1);
    StartMeleeData start = {0};
    start.rules.stkind = St_Kind_Last;
    pc_desktop_begin_match(&start);
    pc_desktop_remap_collision(&disc_collision);
    pc_desktop_stage_ready();

    hud.projection_type = PROJ_ORTHO;
    hud.projection_param.ortho.top = 24;
    hud.projection_param.ortho.bottom = -24;
    hud.projection_param.ortho.left = -32;
    hud.projection_param.ortho.right = 32;
    CHECK(pc_desktop_apply_hud_camera(&hud));
    near_value(hud.projection_param.ortho.top, 48.0f);
    near_value(hud.projection_param.ortho.bottom, -48.0f);
    near_value(hud.projection_param.ortho.left, -64.0f);
    near_value(hud.projection_param.ortho.right, 64.0f);

    hud.projection_type = PROJ_PERSPECTIVE;
    hud.projection_param.perspective.fov = 30.0f;
    CHECK(pc_desktop_apply_hud_camera(&hud));
    CHECK(hud.projection_param.perspective.fov > 30.0f);

    pc_desktop_end_match();
}

static void check_geometry(AuroraDesktopGeometry geometry, float scale)
{
    StartMeleeData start = { 0 };
    HSD_CObj camera = { 0 };
    CameraTransformState transform = { 0 };
    unsigned char source_data[sizeof(disc_collision)];
    unsigned char source_lines[sizeof(disc_lines)];
    unsigned char source_joint[sizeof(disc_joint)];
    float x, y, z;
    supplied_geometry = geometry;
    geometry_available = true;
    reset_fixture(scale);
    memcpy(source_data, &disc_collision, sizeof(source_data));
    memcpy(source_lines, disc_lines, sizeof(source_lines));
    memcpy(source_joint, &disc_joint, sizeof(source_joint));
    start.rules.stkind = St_Kind_Last;
    pc_desktop_begin_match(&start);
    CHECK(!pc_desktop_match_active() && !pc_desktop_collision_fixed());
    CHECK(!pc_desktop_apply_camera(&camera, &transform));
    pc_desktop_remap_collision(&disc_collision);
    CHECK(pc_desktop_collision_fixed() && !pc_desktop_match_active());

    /* Final Destination restores authored bounds after mpLibLoad. The
     * stage-ready hook must override those before the fighters are spawned. */
    stage_info.blast_zone = (StageBlastZone) { -246, 246, 188, -140 };
    stage_info.cam_info.cam_x_offset = 7;
    stage_info.cam_info.cam_y_offset = 11;
    pc_desktop_stage_ready();
    CHECK(pc_desktop_apply_camera(&camera, &transform));
    CHECK(camera.projection_type == PROJ_ORTHO);
    CHECK(camera.flags & PC_COBJ_FILL_FRAME);
    CHECK(camera.near > 0 && camera.far > camera_eye.z);
    near_value(camera_eye.x, camera_interest.x);
    near_value(camera_eye.y, camera_interest.y);
    near_value(camera_interest.z, 0);
    near_value(transform.position.y, camera_eye.y);
    near_value(transform.target_position.y, camera_eye.y);
    near_value(transform.interest.y, camera_interest.y);
    near_value(transform.target_interest.y, camera_interest.y);

    /* Screen corners and taskbar top must line up to much less than 1 pixel,
     * including nonzero/negative virtual desktop origins and centered bars. */
    near_value(pixel_x(&camera, stage_info.blast_zone.left), 0);
    near_value(pixel_x(&camera, stage_info.blast_zone.right), geometry.width);
    near_value(pixel_y(&camera, stage_info.blast_zone.top), 0);
    near_value(pixel_y(&camera, stage_info.blast_zone.bottom), geometry.height);
    near_value(pixel_x(&camera, vertices[0].pos.x), geometry.platform_left);
    near_value(pixel_x(&camera, vertices[1].pos.x), geometry.platform_right);
    near_value(pixel_y(&camera, vertices[0].pos.y), geometry.platform_top);
    near_value(pixel_y(&camera, vertices[1].pos.y), geometry.platform_top);
    near_value(stage_info.cam_info.cam_x_offset, 0);
    near_value(stage_info.cam_info.cam_y_offset, 0);
    CHECK(memcmp(&stage_info.cam_info.cam_bounds, &stage_info.blast_zone,
                  sizeof(stage_info.blast_zone)) == 0);
    for (int i = 0; i < 4; ++i) {
        near_value(vertices[i].prev_pos.x, vertices[i].pos.x);
        near_value(vertices[i].prev_pos.y, vertices[i].pos.y);
        near_value(vertices[i].base_pos.x * scale, vertices[i].pos.x);
        near_value(vertices[i].base_pos.y * scale, vertices[i].pos.y);
        CHECK(joints[0].bounding_min.x <= vertices[i].pos.x);
        CHECK(joints[0].bounding_max.x >= vertices[i].pos.x);
        CHECK(joints[0].bounding_min.y <= vertices[i].pos.y);
        CHECK(joints[0].bounding_max.y >= vertices[i].pos.y);
    }
    CHECK(joints[0].jobj == NULL);
    CHECK(memcmp(source_data, &disc_collision, sizeof(source_data)) == 0);
    CHECK(memcmp(source_lines, disc_lines, sizeof(source_lines)) == 0);
    CHECK(memcmp(source_joint, &disc_joint, sizeof(source_joint)) == 0);
    for (int slot = 0; slot < 2; ++slot) {
        CHECK(pc_desktop_spawn_position(slot, false, &x, &y, &z));
        CHECK(x > vertices[0].pos.x && x < vertices[1].pos.x);
        near_value(y, 0);
        near_value(z, 0);
        CHECK(pc_desktop_spawn_position(slot, true, &x, &y, &z));
        near_value(y, 60);
        pc_desktop_hud_position(slot, 2, &x, &y);
        near_value(x, slot ? 9 : -9);
        near_value(y, 16.5f);
    }
    pc_desktop_match_loaded();
    CHECK(pc_desktop_match_active() && !stage_visible && quake_scale == 0);
    check_cpu_targeting(true);
    check_four_player_targeting();
    pc_desktop_end_match();
    CHECK(!pc_desktop_match_active() && !pc_desktop_collision_fixed());
    /* Ordinary arenas retain their original margin and targeting behavior,
     * including immediately after a desktop match. */
    stage_info.blast_zone = (StageBlastZone) { -246, 246, 188, -140 };
    check_cpu_targeting(false);
}

static void check_inactive(bool requested, int stage, bool available)
{
    StartMeleeData start = { 0 };
    HSD_CObj camera, before_camera;
    CollVtx before_vertices[4];
    StageInfo before_stage;
    float x = 2, y = 3, z = 4;
    reset_fixture(1);
    setenv("MELEE_DESKTOP", requested ? "1" : "0", 1);
    start.rules.stkind = stage;
    geometry_available = available;
    memcpy(before_vertices, vertices, sizeof(vertices));
    before_stage = stage_info;
    memset(&camera, 0x42, sizeof(camera));
    before_camera = camera;
    pc_desktop_begin_match(&start);
    pc_desktop_remap_collision(&disc_collision);
    pc_desktop_stage_ready();
    pc_desktop_match_loaded();
    CHECK(!pc_desktop_match_active() && !pc_desktop_collision_fixed());
    CHECK(!pc_desktop_apply_camera(&camera, NULL));
    CHECK(!pc_desktop_spawn_position(0, false, &x, &y, &z));
    pc_desktop_hud_position(0, 2, &x, &y);
    CHECK(x == 2 && y == 3 && z == 4);
    CHECK(memcmp(&camera, &before_camera, sizeof(camera)) == 0);
    CHECK(memcmp(before_vertices, vertices, sizeof(vertices)) == 0);
    CHECK(memcmp(&stage_info, &before_stage, sizeof(stage_info)) == 0);
    CHECK(stage_visible && quake_scale == 1);
    pc_desktop_end_match();
}

static void check_options(void)
{
    const char* options[] = {
        "MELEE_DESKTOP_P1", "MELEE_DESKTOP_P2", "MELEE_DESKTOP_CPU",
        "MELEE_DESKTOP_WATCH", "MELEE_DESKTOP_STOCKS",
        "MELEE_DESKTOP_COSTUME1", "MELEE_DESKTOP_COSTUME2",
    };
    StartMeleeData start, before;
    for (unsigned i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        unsetenv(options[i]);
    }
    memset(&start, 0x55, sizeof(start));
    before = start;
    unsetenv("MELEE_DESKTOP");
    CHECK(!pc_desktop_requested());
    pc_desktop_configure_match(&start);
    CHECK(memcmp(&start, &before, sizeof(start)) == 0);
    setenv("MELEE_DESKTOP", "1", 1);
    CHECK(pc_desktop_requested());
    pc_desktop_configure_match(&start);
    CHECK(start.rules.stkind == St_Kind_Last && start.rules.is_stock);
    CHECK(start.rules.match_kind == MatchKind_Stock && start.rules.is_vs);
    CHECK(!start.rules.is_teams && start.rules.game_speed == 1.0f);
    CHECK(start.rules.item_freq == -1 && !start.rules.timer_enabled);
    CHECK(start.players[0].ckind == CKind_Fox);
    CHECK(start.players[1].ckind == CKind_Falco);
    CHECK(start.players[0].slot_type == Gm_PKind_Human);
    CHECK(start.players[1].slot_type == Gm_PKind_Cpu);
    /* gmvs translates explicit slots back to zero-based controller ports. */
    CHECK(start.players[0].slot - 1 == 0);
    CHECK(start.players[1].slot - 1 == 1);
    CHECK(start.players[0].stocks == 4 && start.players[1].cpu_level == 5);
    CHECK(start.players[1].cpu_kind == 4);
    for (int i = 2; i < GM_MAX_PLAYERS; ++i) {
        CHECK(start.players[i].slot_type == Gm_PKind_NA);
    }
    setenv("MELEE_DESKTOP_CPU", "0", 1);
    setenv("MELEE_DESKTOP_P1", "20", 1);
    setenv("MELEE_DESKTOP_P2", "20", 1);
    setenv("MELEE_DESKTOP_COSTUME1", "999", 1);
    setenv("MELEE_DESKTOP_COSTUME2", "999", 1);
    setenv("MELEE_DESKTOP_STOCKS", "999", 1);
    pc_desktop_configure_match(&start);
    CHECK(start.players[0].slot_type == Gm_PKind_Human);
    CHECK(start.players[1].slot_type == Gm_PKind_Human);
    CHECK(start.players[0].color == 3 && start.players[1].color == 0);
    CHECK(start.players[0].stocks == 99);
    setenv("MELEE_DESKTOP_WATCH", "1", 1);
    setenv("MELEE_DESKTOP_CPU", "99", 1);
    setenv("MELEE_DESKTOP_P1", "2junk", 1);
    setenv("MELEE_DESKTOP_P2", "999", 1);
    setenv("MELEE_DESKTOP_STOCKS", "-1", 1);
    pc_desktop_configure_match(&start);
    CHECK(start.players[0].slot_type == Gm_PKind_Cpu);
    CHECK(start.players[1].slot_type == Gm_PKind_Cpu);
    CHECK(start.players[0].cpu_level == 9 && start.players[1].cpu_level == 9);
    CHECK(start.players[0].ckind == CKind_Fox);
    CHECK(start.players[1].ckind == CKind_Playable_Count - 1);
    CHECK(start.players[0].stocks == 1);
    for (unsigned i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        unsetenv(options[i]);
    }
}

static void check_roster_options(void)
{
    StartMeleeData start={0};
    static const char* keys[]={"MELEE_DESKTOP_CONTROL1","MELEE_DESKTOP_CONTROL2","MELEE_DESKTOP_CONTROL3","MELEE_DESKTOP_CONTROL4"};
    static const char* chars[]={"MELEE_DESKTOP_P1","MELEE_DESKTOP_P2","MELEE_DESKTOP_P3","MELEE_DESKTOP_P4"};
    for (int size=2;size<=4;++size) {
        char value[2]={(char)('0'+size),0};setenv("MELEE_DESKTOP_PLAYERS",value,1);
        for (int mask=0;mask<(1<<size);++mask) {
            for (int i=0;i<4;++i) {
                char port[2]={(char)('1'+i),0};setenv(keys[i],mask&(1<<i)?port:"0",1);
            }
            pc_desktop_configure_match(&start);
            CHECK(pc_desktop_player_count()==size);
            for (int i=0;i<GM_MAX_PLAYERS;++i) {
                if (i>=size) CHECK(start.players[i].slot_type==Gm_PKind_NA);
                else {
                    CHECK(start.players[i].slot_type==((mask&(1<<i))?Gm_PKind_Human:Gm_PKind_Cpu));
                    CHECK(start.players[i].slot==i+1);
                }
            }
        }
    }
    // Four copies of the same fighter have distinct available costumes.
    for (int i=0;i<4;++i) setenv(chars[i],"20",1);
    pc_desktop_configure_match(&start);
    for (int i=0;i<4;++i)for(int j=0;j<i;++j)CHECK(start.players[i].color!=start.players[j].color);
    // Duplicate controller assignments cannot drive two fighters at once.
    setenv(keys[0],"1",1);setenv(keys[1],"1",1);
    pc_desktop_configure_match(&start);
    CHECK(start.players[0].slot_type==Gm_PKind_Human && start.players[1].slot_type==Gm_PKind_Cpu);
    // Controller ports can be assigned independently of fighter slot.
    setenv(keys[0],"0",1);setenv(keys[1],"1",1);setenv(keys[2],"2",1);setenv(keys[3],"0",1);
    pc_desktop_configure_match(&start);
    CHECK(start.players[1].slot==1 && start.players[2].slot==2);
    for (int i=0;i<4;++i){unsetenv(keys[i]);unsetenv(chars[i]);}
    setenv("MELEE_DESKTOP_PLAYERS","not-a-number",1);pc_desktop_configure_match(&start);CHECK(pc_desktop_player_count()==2);
    setenv("MELEE_DESKTOP_PLAYERS","99",1);pc_desktop_configure_match(&start);CHECK(pc_desktop_player_count()==4);
    setenv("MELEE_DESKTOP_PLAYERS","1",1);pc_desktop_configure_match(&start);CHECK(pc_desktop_player_count()==2);
    unsetenv("MELEE_DESKTOP_PLAYERS");pc_desktop_configure_match(&start);
}

int main(void)
{
    check_options();
    check_hud_camera_scale();
    check_roster_options();
    /* User's exact geometry; short/high-DPI bars; fallback 48px platform;
     * and a tall bar where the original ten-unit safety band still fits. */
    check_geometry((AuroraDesktopGeometry) { 0, 0, 1920, 1080, 0, 1040, 1920, 1080 }, 1);
    check_geometry((AuroraDesktopGeometry) { 0, 0, 1920, 1080, 0, 1064, 1920, 1080 }, 1);
    check_geometry((AuroraDesktopGeometry) { 0, 0, 3840, 2160, 0, 2136, 3840, 2160 }, 1);
    check_geometry((AuroraDesktopGeometry) { 0, 0, 1920, 1080, 0, 1032, 1920, 1080 }, 1);
    check_geometry((AuroraDesktopGeometry) { 0, 0, 1920, 1080, 0, 980, 1920, 1080 }, 1);
    check_geometry((AuroraDesktopGeometry) { -2560, -200, 2560, 1440, 500, 1360, 2060, 1440 }, 0.75f);
    check_geometry((AuroraDesktopGeometry) { 1920, 0, 3840, 2160, 0, 2052, 3840, 2160 }, 1.4f);
    /* Returning to the original display must not compound the prior scale. */
    check_geometry((AuroraDesktopGeometry) { 0, 0, 1920, 1080, 0, 1032, 1920, 1080 }, 1);
    check_inactive(false, St_Kind_Last, true);
    check_inactive(true, St_Kind_Test, true);
    check_inactive(true, St_Kind_Last, false);
    supplied_geometry.width = 0;
    check_inactive(true, St_Kind_Last, true);
    supplied_geometry = (AuroraDesktopGeometry) { 0, 0, 1920, 1080, -1, 1032, 1920, 1080 };
    check_inactive(true, St_Kind_Last, true);
    puts("desktop game: geometry, endian, lifecycle, controller, options and actual CPU target/ally/respawn regression tests passed");
    return 0;
}
