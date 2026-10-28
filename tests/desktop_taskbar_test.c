/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Reuse the ROM-free native-map fixture, not its test main. This links the
 * production desktop wrappers and Fighter_procMap with real mpLib/mpColl
 * collision. The original KO routine is unchanged by this fix; its entry is
 * a recording boundary here so no death animation or disc assets are needed. */
#define main desktop_collision_fixture_main
#include "desktop_collision_test.c"
#undef main

#include <melee/ft/fighter.h>
#include <melee/ft/ft_0D31.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/gr/stage.h>
#include <pc/desktop_fighter.h>
#include <sysdolphin/baselib/jobj.h>

static unsigned ko_checks;
static unsigned map_calls;
static unsigned checks_seen_during_map;
static float last_checked_y;
static Fighter victim, thrower;
static HSD_GObj victim_object, thrower_object;
static HSD_JObj victim_joint;

/* Only the final original KO dispatch is replaced. Tests assert when it can
 * run, while floor intersections and the map callback ordering are native. */
bool ftCo_800D3158(Fighter_GObj* gobj)
{
    last_checked_y = GET_FIGHTER(gobj)->cur_pos.y;
    ++ko_checks;
    return false;
}

void ftCommon_UnlockECB(Fighter* fp) { (void) fp; }
void ftKb_SpecialN_800F1D24(Fighter_GObj* gobj) { (void) gobj; }
void pl_80041280(int player, bool sub) { (void) player; (void) sub; }
void HSD_JObjSetMtxDirtySub(HSD_JObj* jobj) { (void) jobj; }
int grDynamicAttr_801CA284(Vec3* pos, int line)
{
    (void) pos;
    (void) line;
    return 0; /* The fixture has no stage hazards/material overrides. */
}

/* Debug-error metadata and model-derived ECB paths are deliberately unused:
 * abort if this fixture accidentally starts depending on unprovided assets. */
bool ftLib_IsFighter(HSD_GObj* gobj) { (void) gobj; CHECK(false); return false; }
s32 ftLib_IsSubFighter(HSD_GObj* gobj) { (void) gobj; CHECK(false); return 0; }
u8 ftLib_GetPlayerIndex(HSD_GObj* gobj) { (void) gobj; CHECK(false); return 0; }
ItemKind itGetKind(Item_GObj* gobj) { (void) gobj; CHECK(false); return 0; }
void lb_8000B1CC(HSD_JObj* jobj, Vec3* from, Vec3* to)
{
    (void) jobj;
    (void) from;
    (void) to;
    CHECK(false);
}
float pc_sinf(float value) { (void) value; CHECK(false); return 0.0f; }
float pc_cosf(float value) { (void) value; CHECK(false); return 0.0f; }

static void real_floor_collision(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    ++map_calls;
    checks_seen_during_map = ko_checks;
    if (mpColl_80044628_Floor(&fp->coll_data, NULL, NULL, 0)) {
        fp->cur_pos = fp->coll_data.contact;
        fp->ground_or_air = GA_Ground;
    }
}

static void empty_throw_collision(Fighter_GObj* gobj)
{
    (void) gobj;
    ++map_calls;
    checks_seen_during_map = ko_checks;
}

static void reset_fighters(float x, float from_y, float to_y)
{
    memset(&victim, 0, sizeof(victim));
    memset(&thrower, 0, sizeof(thrower));
    memset(&victim_object, 0, sizeof(victim_object));
    memset(&thrower_object, 0, sizeof(thrower_object));
    memset(&victim_joint, 0, sizeof(victim_joint));
    victim_object.user_data = &victim;
    thrower_object.user_data = &thrower;
    victim_object.hsd_obj = &victim_joint;
    victim_joint.flags = JOBJ_MTX_INDEP_SRT;
    victim.gobj = &victim_object;
    thrower.gobj = &thrower_object;
    victim.prev_pos = (Vec3) { x, from_y, 0.0f };
    victim.cur_pos = (Vec3) { x, to_y, 0.0f };
    victim.coll_data = sweep(x, from_y, to_y);
    victim.ground_or_air = GA_Air;
    victim.motion_id = ftCo_MS_DamageFlyLw;
    victim.coll_cb = real_floor_collision;
    ko_checks = map_calls = checks_seen_during_map = 0;
    mpUncheckBounding();
}

static void check_pipeline(void)
{
    pc_desktop_check_blast_zones_before_map(&victim_object);
    Fighter_procMap(&victim_object);
}

static void begin_taskbar_fixture(void)
{
    StartMeleeData start = { 0 };
    CHECK(setenv("MELEE_DESKTOP", "1", 1) == 0);
    CHECK(setenv("MELEE_DESKTOP_WINDOWS", "0", 1) == 0);
    CHECK(setenv("MELEE_DESKTOP_WALLPAPER", "0", 1) == 0);
    stage_info.grkind = Gr_Kind_Last;
    start.rules.stkind = St_Kind_Last;
    pc_desktop_begin_match(&start);
    load_fixture();
    pc_desktop_stage_ready();
    CHECK(pc_desktop_collision_fixed());
}

static void test_fast_downward_collision_before_ko(void)
{
    begin_taskbar_fixture();
    near_value(Stage_GetBlastZoneBottomOffset(), -40.0f * 240.0f / 1080.0f);
    for (unsigned n = 0; n < 4; ++n) {
        const float speeds[] = { 12.0f, 30.0f, 100.0f, 240.0f };
        reset_fighters(70.0f, 1.0f, 1.0f - speeds[n]);
        const Vec3 original_velocity = { 0.0f, -speeds[n], 0.0f };
        victim.x8c_kb_vel = original_velocity;
        CHECK(victim.cur_pos.y < Stage_GetBlastZoneBottomOffset());
        check_pipeline();
        CHECK(map_calls == 1);
        CHECK(checks_seen_during_map == 0);
        CHECK(ko_checks == 0);
        near_value(victim.cur_pos.y, 0.0f);
        CHECK(victim.coll_data.floor.index == 0);
        CHECK(victim.ground_or_air == GA_Ground);
        CHECK(memcmp(&victim.x8c_kb_vel, &original_velocity,
                     sizeof(original_velocity)) == 0);
    }
    puts("PASS native map collision lands fast downward crossings before bottom KO");
}

static void test_unsupported_falls_and_no_callback(void)
{
    reset_fighters(70.0f, -4.0f, -20.0f);
    check_pipeline();
    CHECK(map_calls == 1 && checks_seen_during_map == 0 && ko_checks == 1);
    near_value(victim.cur_pos.y, -20.0f);

    reset_fighters(70.0f, 1.0f, -20.0f);
    victim.coll_cb = NULL;
    check_pipeline();
    CHECK(map_calls == 0 && ko_checks == 1);
    near_value(victim.cur_pos.y, -20.0f);

    /* Map still runs in hitlag. A stale sleeping body is omitted by both
     * native fighter phases; this hook does not give it a standalone tick. */
    reset_fighters(70.0f, -4.0f, -20.0f);
    victim.x2219_b5 = true;
    check_pipeline();
    CHECK(map_calls == 1 && ko_checks == 1);
    reset_fighters(70.0f, -4.0f, -20.0f);
    victim.is_sleeping = true;
    Fighter_procMap(&victim_object);
    CHECK(map_calls == 0 && ko_checks == 0);
    puts("PASS unsupported falls, missing callbacks, hitlag and sleeping paths");
}

static void attach_regular_throw(void)
{
    victim.motion_id = ftCo_MS_ThrownLw;
    victim.victim_gobj = &thrower_object;
    victim.coll_cb = empty_throw_collision;
    thrower.motion_id = ftCo_MS_ThrowLw;
    thrower.victim_gobj = &victim_object;
    thrower.ground_or_air = GA_Ground;
    thrower.cur_pos = (Vec3) { 55.0f, 0.0f, 0.0f };
    thrower.coll_data.floor.index = 0;
}

static void test_attached_throw_and_immediate_release(void)
{
    reset_fighters(70.0f, -10.0f, -20.0f);
    attach_regular_throw();
    check_pipeline();
    CHECK(map_calls == 1 && checks_seen_during_map == 0 && ko_checks == 0);
    near_value(victim.cur_pos.y, -20.0f); /* Do not move an animation bone. */
    victim.motion_id = ftCo_MS_DamageFlyLw;
    victim.victim_gobj = NULL;
    thrower.victim_gobj = NULL;
    check_pipeline();
    CHECK(ko_checks == 1);

    reset_fighters(70.0f, -10.0f, -20.0f);
    attach_regular_throw();
    thrower.victim_gobj = NULL;
    check_pipeline();
    CHECK(ko_checks == 1);

    reset_fighters(70.0f, -10.0f, -20.0f);
    attach_regular_throw();
    thrower.motion_id = ftCo_MS_Wait;
    check_pipeline();
    CHECK(ko_checks == 1);
    puts("PASS attached throw animation exception ends immediately on release/broken link");
}

static void test_attached_throw_requires_real_support(void)
{
    reset_fighters(70.0f, -10.0f, -20.0f);
    attach_regular_throw();
    thrower.ground_or_air = GA_Air;
    check_pipeline();
    CHECK(ko_checks == 1);

    reset_fighters(70.0f, -10.0f, -20.0f);
    attach_regular_throw();
    thrower.cur_pos.y = 10.0f;
    check_pipeline();
    CHECK(ko_checks == 1);

    reset_fighters(70.0f, -10.0f, -20.0f);
    attach_regular_throw();
    thrower.coll_data.floor.index = -1;
    check_pipeline();
    CHECK(ko_checks == 1);

    reset_fighters(70.0f, -10.0f, -20.0f);
    attach_regular_throw();
    mpGetGroundCollLine()[0].flags &= ~LINE_FLAG_ENABLED;
    check_pipeline();
    CHECK(ko_checks == 1);
    mpGetGroundCollLine()[0].flags |= LINE_FLAG_ENABLED;

    /* Shorten just the fixture floor: the thrower remains supported, but a
     * victim thrown beyond its right edge must not gain bottom immunity. */
    reset_fighters(70.0f, -10.0f, -20.0f);
    attach_regular_throw();
    mpLineSetPos(0, -60.0f, 0.0f, 60.0f, 0.0f);
    check_pipeline();
    CHECK(ko_checks == 1);
    begin_taskbar_fixture();
    puts("PASS airborne, stale, disabled and off-edge throw support cannot prevent KO");
}

static void test_native_release_sweep_from_thrower(void)
{
    reset_fighters(70.0f, -15.0f, -20.0f);
    attach_regular_throw();
    check_pipeline();
    CHECK(ko_checks == 0);

    /* ftCo_800DDDE4 does not sweep from the old animated victim position.
     * It puts last_pos at the thrower's position + ECB midpoint, marks the
     * old ECB clear, then resolves the animated release position through
     * mpColl_800471F8. Exercise those exact native entry points with a fixed
     * synthetic ECB so no character model/throw animation is required. */
    CollData release = { 0 };
    mpColl_80041EE4(&release);
    mpColl_SetECBSource_Fixed(&release, &victim_object, 10.0f, 0.0f,
                             4.0f, 4.0f);
    release.last_pos = (Vec3) { thrower.cur_pos.x,
                               thrower.cur_pos.y + 5.0f, 0.0f };
    mpColl_80043670(&release);
    release.cur_pos = victim.cur_pos;
    CHECK(mpColl_800471F8(&release));
    CHECK(release.floor.index == 0);
    CHECK(release.cur_pos.y >= 0.0f);
    CHECK(release.cur_pos.y > Stage_GetBlastZoneBottomOffset());

    victim.cur_pos = release.cur_pos;
    victim.coll_data = release;
    victim.motion_id = ftCo_MS_DamageFlyLw;
    victim.victim_gobj = NULL;
    thrower.victim_gobj = NULL;
    check_pipeline();
    CHECK(ko_checks == 1); /* Ordinary pre-map check sees a safe position. */
    CHECK(last_checked_y > Stage_GetBlastZoneBottomOffset());
    puts("PASS native throw-release sweep restores a below-floor animation origin");
}

static void test_other_boundaries_and_ordinary_stage(void)
{
    reset_fighters(Stage_GetBlastZoneRightOffset() + 1.0f, 1.0f, -20.0f);
    pc_desktop_check_blast_zones_before_map(&victim_object);
    CHECK(ko_checks == 1);

    reset_fighters(Stage_GetBlastZoneLeftOffset() - 1.0f, 1.0f, -20.0f);
    pc_desktop_check_blast_zones_before_map(&victim_object);
    CHECK(ko_checks == 1);

    reset_fighters(70.0f, 1.0f, Stage_GetBlastZoneTopOffset() + 1.0f);
    pc_desktop_check_blast_zones_before_map(&victim_object);
    CHECK(ko_checks == 1);
    pc_desktop_check_blast_zones_after_map(&victim_object);
    CHECK(ko_checks == 1);

    pc_desktop_end_match();
    reset_fighters(70.0f, 1.0f, -20.0f);
    pc_desktop_check_blast_zones_before_map(&victim_object);
    CHECK(ko_checks == 1);
    pc_desktop_check_blast_zones_after_map(&victim_object);
    CHECK(ko_checks == 1);
    puts("PASS side/top timing and ordinary-stage KO path remain unchanged");
}

int main(void)
{
    CHECK(setvbuf(stdout, NULL, _IONBF, 0) == 0);
    test_fast_downward_collision_before_ko();
    test_unsupported_falls_and_no_callback();
    test_attached_throw_and_immediate_release();
    test_attached_throw_requires_real_support();
    test_native_release_sweep_from_thrower();
    test_other_boundaries_and_ordinary_stage();
    for (size_t i = 0; i < allocation_count; ++i) {
        free(allocations[i]);
    }
    puts("Desktop taskbar collision ordering tests passed.");
    return 0;
}
