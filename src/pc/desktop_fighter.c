/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_fighter.h"

#include "desktop_game.h"

#include <math.h>
#include <melee/ft/ft_0D31.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/gr/stage.h>
#include <melee/mp/mplib.h>

/* Attached throw animations deliberately bypass map collision and move the
 * victim's origin with a bone. A down throw can therefore put that origin
 * below the desktop's unusually close bottom boundary for a few frames.
 * Protect only a currently attached regular throw over real supporting
 * ground. No position/velocity is changed, and release, loss of support or a
 * side boundary crossing immediately removes this exception. */
static bool desktop_supported_throw(Fighter_GObj* gobj)
{
    Fighter* victim = GET_FIGHTER(gobj);
    Fighter* thrower;
    MapCollData* map;
    int floor, support;
    float distance;

    if (victim->motion_id < ftCo_MS_ThrownF ||
        victim->motion_id > ftCo_MS_ThrownlwWomen ||
        !isfinite(victim->cur_pos.x) ||
        victim->victim_gobj == NULL) {
        return false;
    }
    thrower = GET_FIGHTER(victim->victim_gobj);
    if (thrower == NULL || thrower->victim_gobj != gobj ||
        thrower->motion_id < ftCo_MS_ThrowF ||
        thrower->motion_id > ftCo_MS_ThrowLw ||
        thrower->ground_or_air != GA_Ground || thrower->is_sleeping ||
        !isfinite(thrower->cur_pos.x) || !isfinite(thrower->cur_pos.y) ||
        thrower->cur_pos.y <= Stage_GetBlastZoneBottomOffset() ||
        victim->cur_pos.x < Stage_GetBlastZoneLeftOffset() ||
        victim->cur_pos.x > Stage_GetBlastZoneRightOffset()) {
        return false;
    }

    map = mpLib_8004D164();
    floor = thrower->coll_data.floor.index;
    if (map == NULL || floor < 0 || floor >= map->line_count ||
        !mpLib_80054ED8(floor) || !(mpLineGetKind(floor) & CollLine_Floor)) {
        return false;
    }
    support = mpLib_8004DD90_Floor(floor, &thrower->cur_pos, &distance,
                                  NULL, NULL);
    if (support < 0 || !mpLib_80054ED8(support) ||
        !isfinite(distance) || fabsf(distance) > 1.0f) {
        return false;
    }
    support = mpLib_8004DD90_Floor(support, &victim->cur_pos, NULL,
                                  NULL, NULL);
    return support >= 0 && mpLib_80054ED8(support);
}

void pc_desktop_check_blast_zones_before_map(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (pc_desktop_collision_fixed() &&
        fp->cur_pos.y < Stage_GetBlastZoneBottomOffset() &&
        fp->cur_pos.x >= Stage_GetBlastZoneLeftOffset() &&
        fp->cur_pos.x <= Stage_GetBlastZoneRightOffset()) {
        return;
    }
    ftCo_800D3158(gobj);
}

/* Recovery only for a real, enabled Final Destination base floor.
 * Dynamic app-window platforms are excluded, and explicit drop-through is
 * respected. The native map callback always runs first; this handles the
 * rare frame whose crossing was missed while the taskbar sits by the KO edge. */
static bool desktop_recover_missed_taskbar(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    MapCollData* map = mpLib_8004D164();
    int first, last, i;
    if (!pc_desktop_collision_fixed() || map == NULL || fp->coll_cb == NULL ||
        fp->ground_or_air != GA_Air || fp->victim_gobj != NULL ||
        !isfinite(fp->prev_pos.y) || !isfinite(fp->cur_pos.y) ||
        !isfinite(fp->cur_pos.x) || fp->prev_pos.y < -0.25f ||
        fp->cur_pos.y >= 0.0f || fp->cur_pos.y >= fp->prev_pos.y ||
        fp->cur_pos.x < Stage_GetBlastZoneLeftOffset() ||
        fp->cur_pos.x > Stage_GetBlastZoneRightOffset()) return false;

    first = map->ranges[MapLineGroup_Floor].start;
    last = first + map->ranges[MapLineGroup_Floor].count;
    if (first < 0 || last > map->line_count) return false;
    for (i = first; i < last; ++i) {
        Vec3 probe = { fp->cur_pos.x, fp->cur_pos.y, 0.0f };
        float delta;
        if (i == fp->coll_data.floor_skip || !mpLib_80054ED8(i) ||
            !(mpLineGetKind(i) & CollLine_Floor)) continue;
        if (mpLib_8004DD90_Floor(i, &probe, &delta, NULL, NULL) < 0 ||
            !isfinite(delta) || delta < 0.0f ||
            fp->cur_pos.y + delta > fp->prev_pos.y + 0.25f) continue;
        fp->cur_pos.y += delta;
        fp->coll_data.cur_pos = fp->cur_pos;
        fp->coll_data.floor.index = i;
        fp->ground_or_air = GA_Ground;
        return true;
    }
    return false;
}

void pc_desktop_check_blast_zones_after_map(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!pc_desktop_collision_fixed() ||
        !(fp->cur_pos.y < Stage_GetBlastZoneBottomOffset()) ||
        desktop_supported_throw(gobj) ||
        desktop_recover_missed_taskbar(gobj)) {
        return;
    }
    ftCo_800D3158(gobj);
}
