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

void pc_desktop_check_blast_zones_after_map(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    if (!pc_desktop_collision_fixed() ||
        !(fp->cur_pos.y < Stage_GetBlastZoneBottomOffset()) ||
        desktop_supported_throw(gobj)) {
        return;
    }
    ftCo_800D3158(gobj);
}
